#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""msf_backup.py — MySafeFob encrypted backups (.msfx) on the PC.

The device only reads and writes encrypted .msfx files (ADR-005 amendment
2026-10-11). This tool is where the readable format lives:

  decrypt  backup.msfx [-o accounts.json]   .msfx -> JSON (to check or edit)
                                            (also a factory rescue copy
                                            RESCUEnn.MSR: asks the device PIN)
  encrypt  accounts.json [-o file.msfx]     JSON -> .msfx (to import on the device)

Typical edit-on-PC round trip: export on the device, `decrypt`, edit the
JSON, `encrypt`, copy the .msfx to the card's MSF/ folder, import it on the
device in Merge mode. The JSON holds every secret in clear: delete it when
done (and empty the recycle bin).

To edit the JSON comfortably, open tools/msf_editor.html in a browser (offline,
single file: browse, search, edit, add/remove accounts, live TOTP code,
device limits checked, then Save JSON).

JSON format (also accepted by `encrypt` for a first fill or a migration):

  { "format": "mysafefob", "version": 1,
    "accounts": [
      { "name": "GitHub", "id": "customer 1234", "login": "octocat",
        "password": "...", "pin": "", "note": "github.com", "extra": "",
        "totp": { "key": "JBSWY3DPEHPK3PXP", "digits": 6, "period": 30 },
        "recovery": ["A1B2-C3D4", {"code": "E5F6-G7H8", "used": true}] },
      { "name": "Google", "totp": "otpauth://totp/Google:luc?secret=...&period=30" }
    ] }

Only "name" is required. "totp" is an object or an otpauth:// URI (the
export format of most TOTP apps; SHA1 only, like the device). A recovery
code is a string (available) or {"code", "used"}. Limits are the device's
(secret_store.h): a field that does not fit is an error, never truncated.

File format: INTERFACES.md §3 (plaintext header authenticated as AES-GCM
additional data, Argon2id key, the store's TLV records).

Needs: pip install argon2-cffi cryptography
"""
import argparse
import base64
import datetime
import getpass
import json
import os
import struct
import sys
import urllib.parse

try:
    from argon2.low_level import Type, hash_secret_raw
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
except ImportError:
    sys.exit("Missing libraries: pip install argon2-cffi cryptography")

# ---- device limits (components/secret_store/include/secret_store.h) ------
MAX_ACCOUNTS = 250   # = boards/x4pro/store_config.cmake MSF_STORE_MAX_ACCOUNTS
NAME_MAX, USER_MAX, PASSWORD_MAX, PIN_MAX, NOTE_MAX = 48, 64, 128, 12, 64
IDENT_MAX, EXTRA_MAX = 64, 64   # "id" (customer / account number...), "extra" (2026-10-13)
TOTP_KEY_MAX, RCV_MAX, RCV_CODE_MAX = 103, 32, 16
PASS_MIN, PASS_MAX = 8, 64

# ---- file format ----------------------------------------------------------
HEADER = struct.Struct("<4sBBBBH16s12sqHHI")   # backup_header_t, packed, 54 bytes
MAGIC, VERSION, KDF_ARGON2ID = b"MSFX", 1, 2
KDF_M_MIB, KDF_T, KDF_P = 2, 3, 1               # same as the device (secret_store.c)
TAG_LEN = 16

REC_ACCOUNT = 0x30
F_ID, F_NAME = 0x01, 0x02
F_TOTP_KEY, F_DIGITS, F_PERIOD = 0x10, 0x11, 0x12
F_USER, F_PASSWORD, F_PIN, F_NOTE = 0x20, 0x21, 0x22, 0x23
F_IDENT, F_EXTRA = 0x24, 0x25
F_RCV_MASK, F_RCV_CODE = 0x40, 0x41


class BackupError(Exception):
    pass


def derive_key(passphrase, salt, m_mib, t, p):
    return hash_secret_raw(passphrase.encode("utf-8"), salt, time_cost=t, memory_cost=m_mib * 1024,
                           parallelism=p, hash_len=32, type=Type.ID, version=19)


# ---- records <-> TLV ------------------------------------------------------

def tlv(tag, value):
    return struct.pack("<BH", tag, len(value)) + value


def serialize(accounts):
    out = bytearray(b"MSFS\x01")
    for i, a in enumerate(accounts, 1):
        body = bytearray(tlv(F_ID, struct.pack("<H", i)))
        body += tlv(F_NAME, a["name"].encode())
        if a["totp_key"]:
            body += tlv(F_TOTP_KEY, a["totp_key"].encode())
            body += tlv(F_DIGITS, bytes([a["digits"]]))
            body += tlv(F_PERIOD, bytes([a["period"]]))
        for tag, key in ((F_USER, "login"), (F_PASSWORD, "password"), (F_PIN, "pin"), (F_NOTE, "note"),
                         (F_IDENT, "id"), (F_EXTRA, "extra")):
            if a[key]:
                body += tlv(tag, a[key].encode())
        if a["recovery"]:
            mask = sum(1 << k for k, (_, used) in enumerate(a["recovery"]) if used)
            body += tlv(F_RCV_MASK, struct.pack("<I", mask))
            for code, _ in a["recovery"]:
                body += tlv(F_RCV_CODE, code.encode())
        out += tlv(REC_ACCOUNT, bytes(body))
    return bytes(out)


def deserialize(data):
    if len(data) < 5 or data[:4] != b"MSFS" or data[4] != 1:
        raise BackupError("unknown record format")
    accounts, pos = [], 5
    while pos + 3 <= len(data):
        tag, rlen = struct.unpack_from("<BH", data, pos)
        pos += 3
        if pos + rlen > len(data):
            raise BackupError("damaged records")
        if tag == REC_ACCOUNT:
            a = {"name": "", "login": "", "password": "", "pin": "", "note": "", "id": "", "extra": "", "totp_key": "",
                 "digits": 6, "period": 30, "mask": 0, "codes": []}
            p, end = pos, pos + rlen
            while p + 3 <= end:
                ft, fl = struct.unpack_from("<BH", data, p)
                p += 3
                v = data[p:p + fl]
                text = v.decode("utf-8", errors="replace")
                if ft == F_NAME: a["name"] = text
                elif ft == F_TOTP_KEY: a["totp_key"] = text
                elif ft == F_DIGITS and fl == 1: a["digits"] = v[0]
                elif ft == F_PERIOD and fl == 1: a["period"] = v[0]
                elif ft == F_USER: a["login"] = text
                elif ft == F_PASSWORD: a["password"] = text
                elif ft == F_PIN: a["pin"] = text
                elif ft == F_NOTE: a["note"] = text
                elif ft == F_IDENT: a["id"] = text
                elif ft == F_EXTRA: a["extra"] = text
                elif ft == F_RCV_MASK and fl == 4: a["mask"] = struct.unpack("<I", v)[0]
                elif ft == F_RCV_CODE: a["codes"].append(text)
                p += fl
            accounts.append(a)
        pos += rlen
    return accounts


# ---- .msfx ----------------------------------------------------------------

def seal(accounts, passphrase, created):
    plain = serialize(accounts)
    salt, nonce = os.urandom(16), os.urandom(12)
    header = HEADER.pack(MAGIC, VERSION, KDF_ARGON2ID, KDF_T, KDF_P, KDF_M_MIB, salt, nonce, created,
                         len(accounts), 0, len(plain))
    key = derive_key(passphrase, salt, KDF_M_MIB, KDF_T, KDF_P)
    return header + AESGCM(key).encrypt(nonce, plain, header)   # ciphertext | tag


# Factory rescue copy (RESCUEnn.MSR): one store slot as on flash, slot_header_t
# of secret_store.c (GCM AAD) | ciphertext | tag; key = Argon2id(device PIN,
# header salt and parameters). Store format 1: 46-byte header; format 2
# (2026-10-12, 250 accounts): the same fields padded to 64 bytes.
SLOT_HEADER = struct.Struct("<4sBBBBH16s12sII")
SLOT_MAGIC = b"MSF1"
SLOT_HEADER_LEN = {1: 46, 2: 64}


def is_rescue(data):
    return data[:4] == SLOT_MAGIC


def open_rescue(data, pin):
    if len(data) < SLOT_HEADER.size + TAG_LEN:
        raise BackupError("damaged rescue copy")
    magic, version, algo, t, p, m_mib, salt, nonce, _seq, ct_len = SLOT_HEADER.unpack_from(data)
    if version not in SLOT_HEADER_LEN or algo != KDF_ARGON2ID:
        raise BackupError("unknown store format")
    hlen = SLOT_HEADER_LEN[version]
    if len(data) != hlen + ct_len + TAG_LEN:
        raise BackupError("damaged rescue copy (size does not match its header)")
    if not (len(pin) == 6 and pin.isdigit()):
        raise BackupError("the device PIN is 6 digits")
    key = derive_key(pin, salt, m_mib, t, p)
    try:
        plain = AESGCM(key).decrypt(nonce, data[hlen:], data[:hlen])
    except Exception:
        raise BackupError("wrong PIN, or the file was altered") from None
    return deserialize(plain), 0


def open_backup(data, passphrase):
    if len(data) < HEADER.size + TAG_LEN:
        raise BackupError("not a MySafeFob backup")
    (magic, version, algo, t, p, m_mib, salt, nonce, created, count, _reserved,
     ct_len) = HEADER.unpack_from(data)
    if magic != MAGIC or version != VERSION or algo != KDF_ARGON2ID:
        raise BackupError("not a MySafeFob backup, or a newer format")
    if len(data) != HEADER.size + ct_len + TAG_LEN:
        raise BackupError("damaged file (size does not match its header)")
    key = derive_key(passphrase, salt, m_mib, t, p)
    try:
        plain = AESGCM(key).decrypt(nonce, data[HEADER.size:], data[:HEADER.size])
    except Exception:
        raise BackupError("wrong passphrase, or the file was altered") from None
    accounts = deserialize(plain)
    if len(accounts) != count:
        raise BackupError("damaged file (record count)")
    return accounts, created


# ---- JSON <-> records -----------------------------------------------------

def to_json(accounts, created):
    out = []
    for a in accounts:
        j = {"name": a["name"]}
        for key in ("id", "login", "password", "pin", "note", "extra"):
            if a[key]:
                j[key] = a[key]
        if a["totp_key"]:
            j["totp"] = {"key": a["totp_key"], "digits": a["digits"], "period": a["period"]}
        if a["codes"]:
            j["recovery"] = [{"code": c, "used": True} if a["mask"] & (1 << k) else c
                             for k, c in enumerate(a["codes"])]
        out.append(j)
    doc = {"format": "mysafefob", "version": 1}
    if created > 0:
        doc["created"] = datetime.datetime.fromtimestamp(created, datetime.timezone.utc).strftime(
            "%Y-%m-%dT%H:%M:%SZ")
    doc["accounts"] = out
    return doc


def check_text(where, value, limit, errors, warnings):
    if not isinstance(value, str):
        errors.append(f"{where}: must be text")
        return ""
    if len(value.encode("utf-8")) > limit:
        errors.append(f"{where}: {len(value.encode('utf-8'))} bytes, the device keeps at most {limit}")
    if any(ord(c) < 0x20 or ord(c) > 0x7E for c in value):
        warnings.append(f"{where}: non-ASCII characters are stored but not displayed by the device")
    return value


def parse_otpauth(uri, where, errors):
    u = urllib.parse.urlparse(uri)
    q = urllib.parse.parse_qs(u.query)
    if u.scheme != "otpauth" or u.netloc != "totp":
        errors.append(f"{where}: only otpauth://totp/ URIs are supported")
        return None
    if q.get("algorithm", ["SHA1"])[0].upper() != "SHA1":
        errors.append(f"{where}: only SHA1 TOTP is supported by the device")
        return None
    label = urllib.parse.unquote(u.path.lstrip("/"))
    issuer = q.get("issuer", [label.split(":")[0] if ":" in label else label])[0]
    return {"key": q.get("secret", [""])[0], "digits": int(q.get("digits", ["6"])[0]),
            "period": int(q.get("period", ["30"])[0]), "issuer": issuer}


def from_json(doc):
    """Validated records ready to serialize, or BackupError listing every problem."""
    errors, warnings, accounts, names = [], [], [], set()
    items = doc.get("accounts") if isinstance(doc, dict) else None
    if not isinstance(items, list):
        raise BackupError('the JSON needs an "accounts" list')
    if len(items) > MAX_ACCOUNTS:
        errors.append(f"{len(items)} accounts, the device holds at most {MAX_ACCOUNTS}")
    for n, j in enumerate(items, 1):
        where = f"account {n}"
        if not isinstance(j, dict):
            errors.append(f"{where}: must be an object")
            continue
        totp = j.get("totp")
        if isinstance(totp, str):
            totp = parse_otpauth(totp, where, errors)
        name = j.get("name") or (totp or {}).get("issuer", "")
        where = f'account {n} "{name}"'
        a = {"name": check_text(f"{where} name", name, NAME_MAX, errors, warnings),
             "login": check_text(f"{where} login", j.get("login", ""), USER_MAX, errors, warnings),
             "password": check_text(f"{where} password", j.get("password", ""), PASSWORD_MAX, errors, warnings),
             "pin": check_text(f"{where} pin", j.get("pin", ""), PIN_MAX, errors, warnings),
             "note": check_text(f"{where} note", j.get("note", ""), NOTE_MAX, errors, warnings),
             "id": check_text(f"{where} id", j.get("id", ""), IDENT_MAX, errors, warnings),
             "extra": check_text(f"{where} extra", j.get("extra", ""), EXTRA_MAX, errors, warnings),
             "totp_key": "", "digits": 6, "period": 30, "recovery": []}
        if not a["name"]:
            errors.append(f"account {n}: no name")
        elif a["name"].lower() in names:
            errors.append(f"{where}: the name appears twice (names are case-insensitive)")
        names.add(a["name"].lower())
        if a["pin"] and not a["pin"].isdigit():
            errors.append(f"{where} pin: digits only")
        if totp:
            if not isinstance(totp, dict):
                errors.append(f"{where} totp: an object or an otpauth:// URI")
            else:
                key = str(totp.get("key", "")).replace(" ", "").upper().rstrip("=")
                try:
                    raw = base64.b32decode(key + "=" * (-len(key) % 8))
                    if not 1 <= len(raw) <= 64:
                        raise ValueError
                except Exception:
                    errors.append(f"{where} totp key: not valid Base32 (1-64 bytes)")
                a["totp_key"] = key
                a["digits"], a["period"] = totp.get("digits", 6), totp.get("period", 30)
                if a["digits"] not in (6, 7, 8):
                    errors.append(f"{where} totp digits: 6 to 8")
                if not isinstance(a["period"], int) or not 1 <= a["period"] <= 255:
                    errors.append(f"{where} totp period: 1 to 255 seconds")
                if a["digits"] not in (6, 8) or a["period"] not in (30, 60):
                    warnings.append(f"{where} totp: {a['digits']} digits / {a['period']} s is accepted, but the "
                                    "device's edit screen only offers 6 or 8 digits and 30 or 60 s")
                if len(key) > TOTP_KEY_MAX:
                    errors.append(f"{where} totp key: longer than {TOTP_KEY_MAX} characters")
        rcv = j.get("recovery", [])
        if len(rcv) > RCV_MAX:
            errors.append(f"{where}: {len(rcv)} recovery codes, at most {RCV_MAX}")
        for k, c in enumerate(rcv[:RCV_MAX], 1):
            code, used = (c.get("code", ""), bool(c.get("used"))) if isinstance(c, dict) else (c, False)
            code = check_text(f"{where} recovery code {k}", code, RCV_CODE_MAX, errors, warnings)
            if code:
                a["recovery"].append((code, used))
        accounts.append(a)
    for w in warnings:
        print("warning:", w, file=sys.stderr)
    if errors:
        raise BackupError("the JSON cannot be imported:\n  " + "\n  ".join(errors))
    return accounts


# ---- commands ---------------------------------------------------------------

def ask_passphrase(confirm):
    p = getpass.getpass("Backup passphrase: ")
    if confirm:
        if not PASS_MIN <= len(p) <= PASS_MAX:
            raise BackupError(f"the passphrase must be {PASS_MIN} to {PASS_MAX} characters")
        if getpass.getpass("Again: ") != p:
            raise BackupError("the two passphrases differ")
    return p


def cmd_decrypt(args):
    with open(args.file, "rb") as f:
        data = f.read()
    if is_rescue(data):
        print("Factory rescue copy: protected by the device PIN only.")
        accounts, created = open_rescue(data, getpass.getpass("Device PIN: "))
    else:
        accounts, created = open_backup(data, ask_passphrase(False))
    out = args.output or os.path.splitext(args.file)[0] + ".json"
    with open(out, "w", encoding="utf-8") as f:
        json.dump(to_json(accounts, created), f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(f"{len(accounts)} account(s) -> {out}")
    print("This file holds every secret in clear: delete it when done.")


def cmd_encrypt(args):
    with open(args.file, encoding="utf-8") as f:
        doc = json.load(f)
    accounts = from_json(doc)
    now = datetime.datetime.now()
    out = args.output or now.strftime("MSF-%Y%m%d-%H%M.msfx")
    if os.path.exists(out):
        raise BackupError(f"{out} already exists, choose another name with -o")
    data = seal(accounts, ask_passphrase(True), int(now.timestamp()))
    with open(out, "wb") as f:
        f.write(data)
    print(f"{len(accounts)} account(s) -> {out}")
    print("Copy it to the SD card's MSF folder, then Settings > Security > Backup > Import.")


def main():
    ap = argparse.ArgumentParser(description="MySafeFob encrypted backups (.msfx) <-> JSON.")
    sub = ap.add_subparsers(dest="command", required=True)
    d = sub.add_parser("decrypt", help=".msfx -> JSON (secrets in clear!)")
    d.add_argument("file")
    d.add_argument("-o", "--output")
    e = sub.add_parser("encrypt", help="JSON -> .msfx")
    e.add_argument("file")
    e.add_argument("-o", "--output")
    args = ap.parse_args()
    try:
        {"decrypt": cmd_decrypt, "encrypt": cmd_encrypt}[args.command](args)
    except (BackupError, OSError, json.JSONDecodeError) as err:
        sys.exit(f"Error: {err}")


if __name__ == "__main__":
    main()
