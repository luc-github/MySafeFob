#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""flash_mgr.py — flashes variants from installer/.

The installer/<variant>/ directory is fed by the postbuild cmake steps.
Each variant has a JSON flash map (<variant>.json, format
{meta, files:[{file, offset}]}) — single source of truth, regenerated
from the folder's actual contents (never stale), ALSO consumed by a
possible web installer.

Usage:
  python tools/flash_scripts/flash_mgr.py                  # interactive + memory
  python tools/flash_scripts/flash_mgr.py --list
  python tools/flash_scripts/flash_mgr.py --variant x4pro_app --port COM5
  python tools/flash_scripts/flash_mgr.py --variant x4pro_factory --port COM5
  python tools/flash_scripts/flash_mgr.py --board x4pro --port COM5  # = x4pro_app
  python tools/flash_scripts/flash_mgr.py --variant x4pro_app --app-only --port COM5
  python tools/flash_scripts/flash_mgr.py --generate --variant x4pro_app

Modes:
  x4pro_app       full install: firmware + factory + bootloader (hook)
                  + partitions + otadata (boots straight into the app)
  x4pro_factory   rescue: factory + bootloader + partitions + otadata zero
                  -> GUARANTEED boot into the recovery menu
  --app-only      firmware only (fast dev loop, otadata kept as-is)
  --erase-flash   erases the whole flash before writing

Memory: .flash_mgr_prefs.json remembers variant/port/baud/erase — Enter
reruns the last flash. Port detection via pyserial (IDF venv).
"""
import argparse
import json
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
SRC_ROOT = SCRIPT_DIR.parent.parent
BOARDS_ROOT = SRC_ROOT / "boards"
PREFS_FILE = SCRIPT_DIR / ".flash_mgr_prefs.json"

ESPTOOL_EXE = Path(r"C:\Espressif\tools\python\v5.5.5\venv\Scripts\esptool.exe")

# Bootloader offset per chip (0x1000 = ESP32 classic ONLY) — the trap:
# flashing the S3 at 0x1000 causes a boot loop ("invalid header").
_BOOTLOADER_OFFSET_BY_CHIP = {
    "esp32": 0x1000, "esp32s2": 0x1000,
    "esp32s3": 0x0, "esp32c3": 0x0, "esp32c6": 0x0,
    "esp32c5": 0x2000, "esp32h2": 0x0, "esp32h4": 0x2000,
    "esp32p4": 0x2000,
}

# Fallbacks 16 Mo MySafeFob (ADR-007) — utilises UNIQUEMENT si la table de
# partitions binaire est absente du dossier. La table parsee fait foi.
_FALLBACK_TABLE = {"otadata": 0x10000, "factory": 0x20000, "app0": 0x120000}

FILE_SPECS = [
    ("bootloader",   r"bootloader_(\d+)MB\.bin",
     lambda m, t, chip: _BOOTLOADER_OFFSET_BY_CHIP.get(chip, 0x1000)),
    ("partitions",   r"partitions_(\d+)mb\.bin",      lambda m, t, chip: 0xC000),
    ("ota_data",     r"ota_data_initial_(\d+)MB\.bin",
     lambda m, t, chip: t.get("otadata", _FALLBACK_TABLE["otadata"])),
    ("firmware",     r"firmware_(\d+)MB_.+\.bin",
     lambda m, t, chip: t.get("app0", _FALLBACK_TABLE["app0"])),
    ("factory",      r"factory_(\d+)MB\.bin",
     lambda m, t, chip: t.get("factory", _FALLBACK_TABLE["factory"])),
]

DEFAULT_BAUD = 921600
BAUD_RATES = [115200, 230400, 460800, 921600, 1500000, 2000000]


# ---------------------------------------------------------------------------
# Table de partitions (verite terrain pour les offsets)
# ---------------------------------------------------------------------------

_PARTITION_MAGIC = b"\xaa\x50"


def parse_partition_table(bin_path):
    """Parse une table de partitions ESP-IDF binaire -> {label: offset}."""
    table = {}
    try:
        data = Path(bin_path).read_bytes()
    except OSError:
        return table
    for start in range(0, len(data) - 32 + 1, 32):
        entry = data[start:start + 32]
        if entry[0:2] != _PARTITION_MAGIC:
            break
        _m, _t, _st, offset, _sz = struct.unpack_from("<HBBLL", entry, 0)
        label = entry[12:28].split(b"\x00", 1)[0].decode("ascii", "replace")
        if label:
            table[label] = offset
    return table


def _load_partition_table_for(variant_dir, files):
    for filename in files:
        if re.fullmatch(r"partitions_(\d+)mb\.bin", filename, re.IGNORECASE):
            return parse_partition_table(Path(variant_dir) / filename)
    return {}


# ---------------------------------------------------------------------------
# flash_params du board (meta : chip, flash_mode, freq...)
# ---------------------------------------------------------------------------

def auto_flash_params(variant_name):
    """boards/<board>/flash_params.json dont le nom matche le variant."""
    if not BOARDS_ROOT.is_dir():
        return None
    for board in sorted(os.listdir(BOARDS_ROOT)):
        p = BOARDS_ROOT / board / "flash_params.json"
        if p.is_file() and (variant_name == board
                            or variant_name.startswith(f"{board}_")):
            return p
    return None


def load_flash_params(path):
    if path and Path(path).is_file():
        return json.loads(Path(path).read_text(encoding="utf-8"))
    return {}


# ---------------------------------------------------------------------------
# Entrees de flash
# ---------------------------------------------------------------------------

def build_entries(variant_dir, chip=None):
    files = sorted(os.listdir(variant_dir))
    table = _load_partition_table_for(variant_dir, files)
    entries = []
    for _label, pattern, offset_fn in FILE_SPECS:
        for filename in files:
            if re.fullmatch(pattern, filename, re.IGNORECASE):
                entries.append({"file": filename,
                                "offset": hex(offset_fn(None, table, chip))})
                break
    return entries


def missing_components(entries):
    present = set()
    for e in entries:
        for label, pattern, _fn in FILE_SPECS:
            if re.fullmatch(pattern, e["file"], re.IGNORECASE):
                present.add(label)
                break
    return [label for label, _p, _fn in FILE_SPECS if label not in present]


def warn_missing(entries, variant_dir):
    missing = missing_components(entries)
    if missing:
        print(f"\nATTENTION : composant(s) absent(s) : {', '.join(missing)}")
        print(f"  Non trouve(s) dans {variant_dir}.")
        print("  Le device peut ne pas booter — rebuild du variant factory "
              "d'abord (build_mgr).")
    return missing


# ---------------------------------------------------------------------------
# Flash map JSON (regenerée a chaque usage — jamais stale)
# ---------------------------------------------------------------------------

def sync_json(variant_dir, variant_name, flash_params_path=None):
    json_path = Path(variant_dir) / f"{variant_name}.json"
    existing, meta = None, {}
    if json_path.is_file():
        try:
            existing = json.loads(json_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            existing = None
        if isinstance(existing, dict):
            meta = existing.get("meta", {})

    params = load_flash_params(flash_params_path
                               or auto_flash_params(variant_name))
    if params:
        params = {k: v for k, v in params.items() if k != "offsets"}
        meta = {**meta, **params}

    entries = build_entries(variant_dir, chip=meta.get("chip"))
    if not entries:
        return entries, meta

    data = {"meta": meta, "files": entries} if meta else entries
    if data != existing:
        json_path.write_text(json.dumps(data, indent=2) + "\n",
                             encoding="utf-8")
        print(f"{'Créé' if existing is None else 'Actualisé'} : {json_path}")
    return entries, meta


def generate_json(variant_dir, variant_name, flash_params_path=None):
    entries, meta = sync_json(variant_dir, variant_name, flash_params_path)
    if not entries:
        sys.exit(f"ERREUR : aucun binaire reconnu dans {variant_dir}")
    print(f"\nFlash map : {Path(variant_dir) / (variant_name + '.json')}")
    if meta:
        print(f"  meta: chip={meta.get('chip')} mode={meta.get('flash_mode')} "
              f"freq={meta.get('flash_freq')}")
    for e in entries:
        print(f"  {e['offset']:<12}  {e['file']}")
    warn_missing(entries, variant_dir)
    return entries


# ---------------------------------------------------------------------------
# esptool
# ---------------------------------------------------------------------------

def esptool_cmd():
    if ESPTOOL_EXE.exists():
        return [str(ESPTOOL_EXE)]
    return [sys.executable, "-m", "esptool"]


# ---------------------------------------------------------------------------
# Ports serie
# ---------------------------------------------------------------------------

def list_serial_ports():
    try:
        import serial.tools.list_ports
        return [(p.device, p.description)
                for p in sorted(serial.tools.list_ports.comports(),
                                key=lambda p: p.device)]
    except ImportError:
        return []


# ---------------------------------------------------------------------------
# Flash
# ---------------------------------------------------------------------------

def flash(variant_dir, variant_name, port, baud, erase=False, app_only=False):
    entries, meta = sync_json(variant_dir, variant_name)
    if app_only:
        entries = [e for e in entries
                   if re.fullmatch(FILE_SPECS[3][1], e["file"], re.IGNORECASE)]
        if not entries:
            sys.exit("ERREUR : pas de firmware_* dans ce variant (--app-only)")

    chip = meta.get("chip", "esp32s3")
    flash_mode = meta.get("flash_mode", "qio")
    flash_freq = meta.get("flash_freq", "80m")
    flash_size = "detect"
    for e in entries:
        m = re.search(r"(\d+)MB", e["file"], re.IGNORECASE)
        if m:
            flash_size = f"{m.group(1)}MB"
            break

    cmd = esptool_cmd() + [
        "--chip", chip, "--port", port, "--baud", str(baud),
        "--before", meta.get("before", "default_reset"),
        "--after", meta.get("after", "hard_reset"),
        "write_flash",
        "--flash_mode", flash_mode, "--flash_freq", flash_freq,
        "--flash_size", flash_size,
    ]
    if erase:
        cmd.append("--erase-all")
    for e in entries:
        cmd += [e["offset"], str(Path(variant_dir) / e["file"])]

    print(f"\nFlash : {variant_name}{' (erase all)' if erase else ''}"
          f"{' (app seule)' if app_only else ''}")
    print(f"Chip: {chip}  Port: {port}  Baud: {baud}  Flash: {flash_size}  "
          f"Mode: {flash_mode}  Freq: {flash_freq}\nFichiers :")
    for e in entries:
        print(f"  {e['offset']:<12}  {e['file']}")
    if not app_only:
        warn_missing(entries, variant_dir)
    print()

    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as e:
        sys.exit(f"\nERREUR : flash echoue (exit {e.returncode}).\n"
                 f"Verifier : device en download mode, bon port.")


# ---------------------------------------------------------------------------
# Decouverte + interactif
# ---------------------------------------------------------------------------

def default_installer_dir():
    return str(SRC_ROOT / "installer")


def discover_variants(installer_dir):
    if not Path(installer_dir).is_dir():
        return []
    return sorted(d.name for d in Path(installer_dir).iterdir() if d.is_dir())


def load_prefs():
    try:
        return json.loads(PREFS_FILE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def save_prefs(prefs):
    try:
        PREFS_FILE.write_text(json.dumps(prefs, indent=2) + "\n",
                              encoding="utf-8")
    except OSError:
        pass


def pick(prompt, items, default_index=None):
    if default_index is not None and not (0 <= default_index < len(items)):
        default_index = None
    for i, item in enumerate(items, 1):
        marker = "  <-- last" if i - 1 == default_index else ""
        print(f"  {i}) {item}{marker}")
    suffix = f" [{default_index + 1}]" if default_index is not None else ""
    while True:
        try:
            raw = input(f"\n{prompt}{suffix}: ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nAborted.")
            sys.exit(0)
        if not raw and default_index is not None:
            return default_index
        if raw.isdigit() and 1 <= int(raw) <= len(items):
            return int(raw) - 1
        print(f"  Entrer un nombre entre 1 et {len(items)}.")


def pick_port(default_port=None, default_index=None):
    while True:
        print("\nDetection des ports serie...")
        ports = list_serial_ports()
        default_idx = None
        if ports:
            for i, (dev, _d) in enumerate(ports, 1):
                if dev == default_port:
                    default_idx = i
                    break
            if default_idx is None and default_index \
                    and 1 <= default_index <= len(ports):
                default_idx = default_index
        if ports:
            print("\nPorts disponibles :\n")
            for i, (dev, desc) in enumerate(ports, 1):
                marker = "  <-- default" if i == default_idx else ""
                print(f"  {i}) {dev}  ({desc}){marker}")
            print("  R) Rafraichir    M) Saisie manuelle")
            opts = f"1-{len(ports)}, R, M"
        else:
            print("  Aucun port detecte (pyserial absent ?).")
            print("  R) Rafraichir    M) Saisie manuelle")
            opts = "R, M"
        prompt = (f"\nPort [{opts}]"
                  + (f" ({default_idx}: {ports[default_idx - 1][0]})"
                     if default_idx else ""))
        try:
            raw = input(f"{prompt}: ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nAborted.")
            sys.exit(0)
        if not raw and default_idx:
            return ports[default_idx - 1][0], default_idx
        if raw.lower() == "r":
            continue
        if raw.lower() == "m":
            try:
                return input("Port (ex COM17) : ").strip(), None
            except (EOFError, KeyboardInterrupt):
                print("\nAborted.")
                sys.exit(0)
        if raw.isdigit() and ports and 1 <= int(raw) <= len(ports):
            return ports[int(raw) - 1][0], int(raw)


def interactive(installer_dir):
    print(f"\n{'=' * 60}\n  MySafeFob Flash Manager — mode interactif"
          f"\n{'=' * 60}\n")
    prefs = load_prefs()

    variants = discover_variants(installer_dir)
    if not variants:
        sys.exit(f"ERREUR : aucun variant dans {installer_dir}\n"
                 f"  Lancer d'abord : python tools/build_scripts/build_mgr.py")

    # Entrees niveau CARTE : l'install complet d'une carte = son variant
    # <board>_app (bootloader + partitions + otadata + factory + app en UN
    # seul flash). Le rescue factory seul reste dispo mais sort du flux.
    boards = []
    if BOARDS_ROOT.is_dir():
        for b in sorted(os.listdir(BOARDS_ROOT)):
            if (BOARDS_ROOT / b).is_dir() and any(
                    v == b or v.startswith(b + "_") for v in variants):
                boards.append(b)

    scope_items, scope_variants = [], []
    for b in boards:
        if f"{b}_app" in variants:
            scope_items.append(f"Carte {b} — install COMPLET (recommande)")
            scope_variants.append(f"{b}_app")
        if f"{b}_factory" in variants:
            scope_items.append(f"Carte {b} — rescue factory seul")
            scope_variants.append(f"{b}_factory")
    scope_items += variants
    scope_variants += variants

    last = prefs.get("variant")
    default_idx = scope_variants.index(last) if last in scope_variants else 0
    idx = pick("Variant", scope_items, default_idx)
    variant_name = scope_variants[idx]
    variant_dir = Path(installer_dir) / variant_name

    port, port_index = pick_port(prefs.get("port"), prefs.get("port_index"))

    default_baud = prefs.get("baud", DEFAULT_BAUD)
    baud_idx = pick("Baud", BAUD_RATES,
                    BAUD_RATES.index(default_baud)
                    if default_baud in BAUD_RATES else None)
    baud = BAUD_RATES[baud_idx]

    default_erase = bool(prefs.get("erase", False))
    try:
        raw = input(f"\nErase flash complet avant ecriture ? "
                    f"{'[Y/n]' if default_erase else '[y/N]'}: ").strip().lower()
    except (EOFError, KeyboardInterrupt):
        print("\nAborted.")
        sys.exit(0)
    do_erase = default_erase if not raw else raw in ("y", "yes")

    save_prefs({"variant": variant_name, "port": port,
                "port_index": port_index, "baud": baud, "erase": do_erase})
    flash(variant_dir, variant_name, port, baud, erase=do_erase)


# ---------------------------------------------------------------------------
# Entree
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(
        description="Flash manager MySafeFob (installer/ + flash map JSON)")
    ap.add_argument("--installer-dir", default=str(default_installer_dir()))
    ap.add_argument("--variant")
    ap.add_argument("--board",
                    help="Carte complete = install complet (<board>_app)")
    ap.add_argument("--variant-dir")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--generate", action="store_true")
    ap.add_argument("--flash-params")
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    ap.add_argument("--erase-flash", action="store_true")
    ap.add_argument("--app-only", action="store_true",
                    help="Firmware seul (boucle dev, otadata conserve)")
    args = ap.parse_args()

    if args.list:
        variants = discover_variants(args.installer_dir)
        if not variants:
            sys.exit(f"Aucun variant dans {args.installer_dir}")
        print(f"Variants disponibles ({args.installer_dir}) :\n")
        for v in variants:
            print(f"  {v}")
        return

    if args.variant_dir:
        variant_dir = Path(args.variant_dir).resolve()
        variant_name = variant_dir.name
    elif args.board:
        variant_name = f"{args.board}_app"
        variant_dir = Path(args.installer_dir) / variant_name
        if not variant_dir.is_dir():
            sys.exit(f"ERREUR : pas d'install complet pour la carte "
                     f"'{args.board}' ({variant_name} absent de "
                     f"{args.installer_dir}) — build_mgr d'abord ?")
    elif args.variant:
        variant_dir = Path(args.installer_dir) / args.variant
        variant_name = args.variant
    elif not args.generate and not args.port:
        interactive(args.installer_dir)
        return
    else:
        ap.error("Specifier --variant NAME (ou --variant-dir PATH), ou --list")

    if not variant_dir.is_dir():
        sys.exit(f"ERREUR : dossier variant introuvable : {variant_dir}")

    if args.generate or not args.port:
        generate_json(variant_dir, variant_name, args.flash_params)
    else:
        flash(variant_dir, variant_name, args.port, args.baud,
              erase=args.erase_flash, app_only=args.app_only)


if __name__ == "__main__":
    main()
