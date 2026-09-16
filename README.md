# MySafeFob (MSF) — firmware `src/`

Squelette du firmware applicatif (Phase 8). Air-gapped password manager +
TOTP authentificator pour XTEINK X4 Pro (ESP32-S3). Décisions figées :
voir `docs/ROADMAP.md` (ADR-001 à ADR-008), `docs/FEATURES.md`,
`docs/INTERFACES.md`.

## Structure

```
src/
├── CMakeLists.txt          # racine — sélection board -DMSF_BOARD=x4pro
├── sdkconfig.defaults      # réglages communs tous boards
├── main/                   # app principale (hello world + console REPL)
├── components/
│   ├── totp_engine/        # moteur TOTP RFC 6238 (copié du test validé Phase 4)
│   ├── secret_store/       # keystore chiffré — CONTRAT + stub (tâche 8.2)
│   └── esp3d_log/          # logging applicatif (portage PiBot — hooks -> UI)
├── boards/
│   ├── x4pro/              # board réelle (config + README, bring-up complet)
│   │   ├── partitions.csv  #   table 16 MB propre a la board (ADR-007) :
│   │   │                   #   offsets fixes, contractuels
│   │   ├── factory/        #   sous-projet AUTONOME par board (alignement
│   │   │                   #   PiBot) : app recovery + bootloader hook.
│   │   │                   #   Drivers hardware dedies (UC8279/GT911/SDMMC) —
│   │   │                   #   ../docs/FACTORY.md. Chaque future board porte
│   │   │                   #   SA factory (pas de core partage).
│   │   ├── build_scripts/  #   build_one.py <app|factory|all> [--clean]
│   │   │                   #   (portage PiBot — env IDF auto-configure)
│   │   ├── flash_scripts/  #   .bat one-click : flash_all / flash_app /
│   │   │                   #   flash_factory / erase_flash [COMx]
│   │   ├── cmake/          #   postbuild.cmake -> alimente installer/
│   │   │                   #   + copy_if_exists.cmake (artefacts factory)
│   │   └── factory/cmake/  #   postbuild factory -> installer/x4pro_factory/
│   └── m5paper_mono/       # stub documentation (ADR-008, pas de code)
├── tools/
│   ├── build_scripts/      # build_mgr.py (multi-boards, interactif + memoire)
│   │                       # + gen_ota_initial.py (otadata -> boot app0)
│   └── flash_scripts/      # flash_mgr.py (interactif + memoire, flash map
│                           # JSON par variant, consommable par web installer)
└── installer/              # généré par les postbuild (gitignoré)
    ├── x4pro_app/          #   firmware + factory + bootloader hook + pt + otadata
    │                       #   (+ x4pro_app.json = flash map)
    └── x4pro_factory/      #   rescue : factory + bootloader hook + pt + otadata 0
```

**Règle structurelle** (alignement PiBot) : tout ce qui dépend du hardware
vit sous `boards/<board>/`. La factory est un outil de recovery — elle doit
rester minimale, autonome et testée telle quelle : on assume la duplication
(gfx/menu) entre boards plutôt qu'un core partagé qui pourrait casser
plusieurs boards d'un coup. Même logique pour build_scripts/flash_scripts.

## Quickstart (IDF 5.5.5 — aucun environnement à activer)

**Une seule interface, une seule mémoire** (portage PiBot) :

```bash
cd src

# BUILD — interactif avec mémoire (Enter = relancer le dernier choix)
python tools/build_scripts/build_mgr.py
# ou en CLI :
python tools/build_scripts/build_mgr.py --build_all          # factory d'abord
python tools/build_scripts/build_mgr.py --build_variant app  # ou factory

# FLASH — interactif avec mémoire (variant/port/baud détectés)
python tools/flash_scripts/flash_mgr.py
# ou en CLI :
python tools/flash_scripts/flash_mgr.py --variant x4pro_app --port COM5
python tools/flash_scripts/flash_mgr.py --variant x4pro_app --app-only --port COM5
python tools/flash_scripts/flash_mgr.py --variant x4pro_factory --port COM5
```

**Ou double-clic** sur les `.bat` de `boards/x4pro/flash_scripts/`
(port en argument, défaut COM5) — même moteur flash_mgr.

**Avec l'extension VS Code ESP-IDF** : ouvrir `src/` (app) ou
`src/boards/x4pro/factory/` (factory) → Build / Flash / Monitor.
⚠ Le bouton Flash de l'app flashe le **bootloader sans hook** (pas de
recovery) — le bootloader correct arrive via `flash_all` / `flash_factory`.

**Sous Git Bash** : idf.py 5.5 refuse MSYS → les outils ci-dessus gèrent
(l'env est nettoyé en interne), ou utiliser les .bat, ou
`boards/x4pro/factory/msf_build.bat` (wrapper cmd).

Puis `idf.py -p COM5 monitor` : banner MySafeFob + console `msf> `
(commandes `help`, `about`, `totpselftest`).

## Sélection de board

```bash
idf.py -DMSF_BOARD=m5paper_mono build    # (stub : pas de board config encore)
```

Le board par défaut est `x4pro`. Voir `boards/<name>/README.md`.

## Contrats critiques (ne pas casser)

| Élément | Valeur | Où |
|---|---|---|
| Backup otadata | offset `0xB000`, magic `0xAA55AA55` | `boards/x4pro/factory/bootloader_components/custom_bootloader/hooks.c` + `boards/x4pro/factory/main/main.c` |
| Table partitions | offset `0xC000`, factory @`0x20000` | `boards/x4pro/partitions.csv` + `boards/x4pro/sdkconfig.defaults` |
| Magics store/export | `MSF1` (blob), `MSFS` (clair interne), `MSFEX1` (export SD) | `docs/INTERFACES.md` + futur `secret_store.c` |
| Recovery boot | bouton **Power = GPIO3** maintenu ≥ 10 s (GPIO0 = strapping ; combo Power+Right abandonné, ADR-009 amendement 2026-09-16) | `hooks.c` |
| Log applicatif | esp3d_log (niveau `-DMSF_LOG_LEVEL=`, défaut 3=debug) ; drivers/board restent sur `ESP_LOG*` | `components/esp3d_log/`, `CMakeLists.txt` racine |
| UI app | **FreeInkUI** (porté ESP-IDF natif depuis `freeink-sdk`, MIT) remplace LVGL — voir ADR-010 (`docs/ROADMAP.md`) | `docs/ROADMAP.md` §ADR-010, prototype `references/test_apps/freeinkui-poc/` |

## Points ouverts connus

1. **~~Portage S3 / 5.5.5~~ RÉSOLU au build factory du 2026-09-13** : hooks
   inconditionnels (pas de Kconfig), signatures `esp_rom_spiflash_*` OK,
   bootloader 0x5520 < 0xB000. Détail : `docs/FACTORY.md` §3.3.
   Reste à valider **sur hardware** : séquence backup/erase/restore otadata
   (flash la factory + booter avec Power maintenu ≥ 10 s).
2. **Factory app = compile OK (5.5.5) mais non testée sur hardware** :
   restore otadata, menu recovery e-ink, flash SD vers app0 (OTA, seul
   slot app — ADR-011) et factory (écriture directe). Navigation :
   Left/Right physiques + pad Home
   (zone tactile validée par mesure) ; Power = select de secours.
3. **`secret_store` = stub** : implémentation Argon2id + AES-256-GCM en
   tâche 8.2, après calibration des paramètres sur S3.
