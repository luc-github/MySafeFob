# MySafeFob — Factory & Bootloader de recovery (doc technique)

> **Origine** : portage du mécanisme éprouvé du projet **PiBot CNC Pendant**
> (`Luc-Pibot-cnc-pendant-firmware`, docs `docs/Factory/bootloader_technical_doc.md`
> et `factory_app_technical_doc.md`, LGPL-2.1+, même auteur). Ce document
> adapte ces docs au X4 Pro et **met en évidence les différences**.
> Décision de portage : ADR-007 / ADR-008 / ADR-009 (`docs/ROADMAP.md`).

---

## 1. Différences PiBot → MySafeFob (synthèse)

| Domaine | PiBot (ESP32 classic) | MySafeFob (ESP32-S3 X4 Pro) |
|---|---|---|
| Trigger recovery bootloader | BTN3 = GPIO17 | **Power = GPIO3** tenu ≥ 10 s (GPIO0 strapping, GPIO3 strapping JTAG — combo Power+Right abandonné, cf. ADR-009 amendement 2026-09-16) |
| Feedback recovery | Buzzer (bit-bang PWM) | **Logs rom uniquement** (pas de buzzer sur le X4 Pro) |
| Affichage | ILI9341 240×320 RGB565, écriture directe LCD | **UC8279 800×480 1 bpp, framebuffer + full refresh** (rotation 90° CW matérielle) |
| gfx | flush par zone vers le LCD | **framebuffer 48 Ko DRAM**, `gfx_flush()` = full refresh e-ink (~2-4 s) |
| Touch | FT6336U @0x38, polling simple | **GT911 @0x5D/0x14, upload config 185 o obligatoire à chaque boot**, zone Home logicielle |
| Navigation | BTN1/2/3 physiques + zones tactiles + encodeur | **Left/Right physiques + pad Home = select**, Power = select secours. **Pas d'encodeur** |
| SD | SPI (SDSPI) | **SDMMC natif 1-bit slot 1** (CLK=41, CMD=42, DAT0=40) + power pulse GPIO5 |
| Cibles de flash SD | app0/app1 + `ui_resources` | app0 (OTA, seul slot app — ADR-011) + **factory (écriture directe — le recovery se met à jour lui-même)**. Pas de partition resources |
| Fichier firmware | `/sdcard/esp3dfw.bin` | `/sdcard/msf-fw.bin` |
| Snapshot écran (→ SD) | Oui (option) | **Non porté** (doc PiBot §Snapshot, non applicable au 1 bpp e-ink) |
| Chemin logiciel vers factory | `[ESP444]FACTORY` / écran tactile | **Power tenu ≥ 10 s** (app ou réveil, ADR-009 amendé 2026-09-16) — `power_mgr_switch_to_factory()` (§4) |
| IDF | 5.4.x | **5.5.5** (portage hooks validé au build 2026-09-13, §3.3) |
| Flash / partitions | 8 Mo, PT offset 0xC000, factory 320 Ko | **16 Mo, PT offset 0xC000, factory 1 Mo** (même offset de table) |
| Veille | non (LCD toujours alimenté) | **Deep sleep + wake GPIO3** (ADR-009) ; e-ink bistable = image conservée éteint |

Ce qui reste **identique** au PiBot : le mécanisme backup/erase/restore de
l'otadata, les constantes contractuelles, la logique de flash OTA, la
structure du sous-projet factory, et le CMakeLists du bootloader component.

---

## 2. Le mécanisme otadata (rappel — identique au PiBot)

Le "flag d'active" des apps ne vit pas dans les partitions app mais dans
**otadata** (2 entrées de 32 o : `seq` + `ota_state` + crc ; entrée au seq
le plus haut valide = slot actif). La partition `factory` n'a **aucune**
entrée otadata : c'est le **fallback** quand otadata est vide/invalidé.

```
Boot USB / wake
  │
  ├── Hook : GPIO3 (Power) NON maintenu ≥ 10 s → boot normal (otadata → app0)
  │
  └── Hook : GPIO3 (Power) maintenu ≥ 10 s
        │
        ├── 1. Backup otadata (2 entrées) @0xB000 + magic 0xAA55AA55
        ├── 2. Erase otadata (2 secteurs)
        ├── 3. Reset logiciel
        │         └── Bootloader : otadata vide → boot factory
        │               └── Factory : restore otadata depuis le backup
        │                     └── Power-off → retour à la bonne app
```

Pourquoi le backup : sans lui, effacer otadata détruirait l'info « slot
actif + compteur seq » → la chaîne OTA (alternance par parité de seq) serait
cassée. Le restore recopie les entrées octet pour octet (CRC inclus) ; le
magic effacé empêche le re-restore en boucle ; un backup vide (entrées 0xFF,
device jamais flashé en OTA) est détecté et ignoré.

**Constantes contractuelles** (identiques dans `hooks.c` et
`boards/x4pro/factory/main/main.c`, vérifiées par grep) :

```
OTADATA_BACKUP_OFFSET   0xB000     secteur libre : après bootloader S3 (~0x7000),
                                   avant la table des partitions (0xC000),
                                   hors partitions (NVS @0xD000), aligné 4 Ko
BACKUP_MAGIC            0xAA55AA55 @0x40 dans le secteur de backup
BACKUP_MAGIC_OFFSET     0x40
OTADATA_OFFSET          0x10000
```

Règles de choix du secteur de backup et schéma détaillé : voir la doc PiBot
originale §"Otadata Backup Layout" (même logique, recalculée pour le layout
16 Mo — marge plus grande qu'en 8 Mo).

---

## 3. Bootloader hook

Source : `src/boards/x4pro/factory/bootloader_components/custom_bootloader/hooks.c`
(portage direct du PiBot). La factory vit SOUS le board car elle dépend de
son hardware — chaque board porte sa factory (règle structurelle, cf.
`src/README.md` §Structure).

### 3.1 Ce qui change vs PiBot

- **Buzzer supprimé** : `buzzer_tone()/beep_*()` retirés ; acquittement =
  `esp_rom_printf` (gated par `FACTORY_LOG_LEVEL`, comme le PiBot).
- **Bouton = GPIO3 / Power** (actif-LOW, pull-up, anti-rebond 3/5 identique,
  seuil 10 s). Combo Power+Right (GPIO7) abandonné le 2026-09-16 : hardware
  ne réveille jamais le device quand les deux boutons sont tenus ensemble
  (cf. ADR-009 amendement). `RECOVERY_BUTTON_PIN` migré de GPIO7 à GPIO3.
- **Strapping S3** : GPIO0 (boot mode), GPIO3 (JTAG source), GPIO45/46.
  Ne jamais utiliser GPIO0 dans le hook. GPIO3 est utilisé en connaissance
  de cause (déjà le pin de wake EXT1 pour Power) — à valider : la console
  USB reste utilisable après un hold GPIO3 (point de validation 8c).
- **`CONFIG_IDF_TARGET="esp32s3"`**, PSRAM octal dans le sdkconfig factory.

### 3.2 Budget bootloader

Même logique que le PiBot : `CONFIG_PARTITION_TABLE_OFFSET=0xC000` (44 Ko
de marge bootloader). Le bootloader S3 peut être plus gros (features sécu) —
**vérifier la taille à la 1re compile** ; le PiBot documente le dépassement
et la procédure (`rm -rf build/bootloader && idf.py build`).

### 3.3 Points de portage S3 — validés au build du 2026-09-13

Tous les points ci-dessous ont été vérifiés par la 1re compile factory
réussie en IDF 5.5.5 (binaire `mysafefob-factory.bin`, 0x5A1F0, 65 % libre
dans la partition 1 Mo) :

- [x] Signatures `esp_rom_spiflash_read/write/erase_sector` sur S3 / 5.5.5
- [x] `gpio_ll_*` et `esp_rom_gpio_pad_select_gpio` — inchangés, compilent
- [x] `esp_rom_software_reset_system()` — même signature, compile
- [x] Taille du bootloader : **0x5520 < 0xB000** OK (marge confortable)
- [x] Hooks bootloader : **pas d'option Kconfig en 5.5.5** — mécanisme
  inconditionnel (weak hooks `bootloader_hooks.h` appelés si définis).
  Symbole `bootloader_after_init` confirmé dans `bootloader.elf`.
  `CONFIG_BOOTLOADER_HOOKS=y` retiré de `sdkconfig.defaults` (option morte).

**Renommages de composants constatés en 5.5.5** (corrigés au build) :
- `esp_flash` → composant `spi_flash` (REQUIRES du factory)
- `esp_vfs_fat` → absorbé dans `fatfs` (header `esp_vfs_fat.h` inchangé,
  plus aucune dépendance gérée requise)
- `esp_console` → composant `console` (REQUIRES de l'app)
- Nouvelle contrainte `gen_esp32part` : partition `nvs` read/write ≥ 0x3000
  → `partitions.csv` passe nvs de 0x2000 à **0x3000** (offsets otadata
  @0x10000 et backup @0xB000 inchangés — contrats préservés)

**API cassées constatées côté app (corrigées au build)** :
- REPL console : `esp_console_repl_start()` **supprimée** —
  `esp_console_new_repl_uart()` spawn le thread REPL elle-même (5.5)
- Deep sleep wake-up GPIO : S3 n'a **pas** `SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP`
  (wake-up GPIO digital inexistant) → **EXT1** obligatoire. GPIO3 est un
  RTC IO : `esp_sleep_enable_ext1_wakeup(BIT3, ESP_EXT1_WAKEUP_ANY_LOW)`,
  cause de reveil = `ESP_SLEEP_WAKEUP_EXT1` (pas `ESP_SLEEP_WAKEUP_GPIO`)
- `add_compile_definitions()` après `project()` ne se propage pas aux
  composants IDF → `idf_build_set_property(COMPILE_DEFINITIONS ... APPEND)`

**Binaires produits (build du 2026-09-13)** : factory 0x5A1F0 (65 % libre
sur 1 Mo), app 0x49EA0 (71 % libre), bootloader 0x5160 (58 % libre).
LVGL 9.2.2 récupéré via component manager (`managed_components/lvgl__lvgl`).

### 3.4 Rebuild du bootloader

Le bootloader est un sous-build séparé : après modification de `hooks.c`,
forcer `rm -rf build/bootloader` ou `idf.py bootloader-flash` (PiBot
§Troubleshooting — identique ici).

**⚠️ PIÈGE (découvert 2026-09-16, a coûté plusieurs cycles de debug pour
rien) : `hooks.c` n'est compilé QUE par le build du projet `factory`, jamais
par celui de l'app.** ESP-IDF ne détecte `bootloader_components/` que s'il
est un enfant direct de `PROJECT_SOURCE_DIR` du projet en cours de build
(`components/bootloader/subproject/CMakeLists.txt` :
`set(PROJECT_EXTRA_COMPONENTS "${PROJECT_SOURCE_DIR}/bootloader_components")`).
Ce dossier vit sous `boards/x4pro/factory/bootloader_components/` — donc
**seul** `cd boards/x4pro/factory && ./msf_build.bat build` (ou `idf.py
build` depuis ce dossier) le voit et le compile. Lancer `./msf_build.bat
build` depuis la RACINE du repo compile bien un `build/bootloader/
bootloader.bin`, mais c'est le bootloader STOCK d'ESP-IDF, dont
`bootloader_after_init` resout vers le stub faible de
`components/bootloader/subproject/main/bootloader_start.c` — **aucune trace
de hooks.c dedans**, même si le build "réussit" sans erreur (rien ne le
signale). Vérifiable après coup avec
`nm build/bootloader/bootloader.elf | grep bootloader_after_init` : si ça
pointe vers `libmain.a(bootloader_start.c.obj)`, c'est le stub, pas le hook.

Le build racine (`postbuild.cmake`) **copie** ensuite (ne reconstruit PAS)
`boards/x4pro/factory/build/bootloader/bootloader.bin` (avec le hook) vers
`installer/x4pro_app/bootloader_16MB.bin` **si et seulement si** ce build
factory existe déjà sur disque — sans jamais vérifier qu'il est à jour avec
les sources actuelles. Modifier `hooks.c` puis relancer uniquement le build
racine (même un clean complet, `rm -rf build`) **ne rafraîchit donc rien** :
`installer/x4pro_app/bootloader_16MB.bin` reste l'ancien binaire, silencieusement.

**Procédure correcte après toute modif de `hooks.c`** :
1. `cd boards/x4pro/factory && ./msf_build.bat build` (rebuild le VRAI bootloader avec le hook)
2. `cd ../../.. && ./msf_build.bat build` (rebuild l'app + rafraîchit `installer/x4pro_app/` par copie)
3. Flasher `installer/x4pro_app/bootloader_16MB.bin` (ou directement
   `boards/x4pro/factory/build/bootloader/bootloader.bin`, identique) à
   l'offset `0x0` — c'est le SEUL bootloader qui compte, qu'on teste
   l'app ou la factory (bootloader partagé, flashé une seule fois).

Un changement de defaut CMake (ex. `option(... OFF)` → `option(... ON)`
dans `bootloader_components/custom_bootloader/CMakeLists.txt`) ne s'applique
pas non plus tant que le cache CMake existant du projet factory n'est pas
invalidé : `rm -rf boards/x4pro/factory/build` avant l'étape 1 si un
`option()` a changé.

**⚠️ Autre piège (observé 2026-09-16) : le premier boot juste après un
flash peut sembler cassé (pas de réveil) alors que le firmware est bon.**
Le reset envoyé par `esptool` en fin de flash (bascule RTS/DTR) n'est pas
toujours électriquement équivalent à un power-on propre — l'état RTC/pad
peut rester légèrement instable pour ce tout premier boot, en particulier
depuis qu'on manipule directement le routage RTC_IO de GPIO3 dans le hook
(§3, `rtcio_ll_function_select`). Un 2e reset (par ex. celui déclenché par
la simple connexion d'un moniteur série) suffit à repartir sur un état
stable. **Toujours faire un reset supplémentaire juste après chaque flash,
avant de commencer les tests.**

---

## 4. Chemin logiciel vers la factory (à implémenter côté app, tâche 8c)

Équivalent MSF du `[ESP444]FACTORY` PiBot : **Power tenu ≥ 10 s**, depuis
l'app éveillée ou depuis le réveil (ADR-009, amendé 2026-09-16 — combo
Power+Right abandonné, hardware ne réveille jamais avec les deux boutons
tenus ensemble). Le handler côté app (`power_mgr_switch_to_factory()`)
doit reproduire la séquence du hook (cf. PiBot `esp444.cpp`) :

```
1. esp_flash_read() : backup des 2 entrées otadata @0xB000 + magic
   (adresse hors partition → protection dangerous-write à gérer :
   esp_flash_set_dangerous_write_protection() / sdkconfig, cf. PiBot)
2. esp_ota_set_boot_partition(factory)   → efface otadata
3. esp_restart()
     └── bootloader : otadata vide → factory → restore (comme le hook)
```

Le stub `src/components/power_mgr/` porte le contrat ; l'implémentation
arrive avec les drivers board (8c).

---

## 5. Factory app

Source : `src/boards/x4pro/factory/main/` (13 fichiers). Flot identique au PiBot §
Architecture : restore otadata **avant tout**, puis init écran/entrées,
menu, actions.

### 5.1 Séquence au boot

1. `restore_otadata_from_backup()` (logique PiBot, constantes §2)
2. `eink_init()` — séquence UC8279 validée (PSR 0x37 à l'init, PSR 0x17
   entre PON et DRF — **jamais 0x37 au DRF**, full GC bloqué, cf.
   `docs/hardware-specs.md`)
2b. `splash_show()` (**nouveau 2026-09-15**, ADR-010 amendé) — affiche
   `resources/splash.png` (converti en bitmap 1bpp par `tools/gen_splash.py`
   → `main/splash_bitmap.h`) via `FreeInkUI::DisplayTarget`, copie figée
   propre à la factory (`components/freeinkui/`, jamais partagée avec l'app).
   Couvre le temps d'init entrées/SD ci-dessous d'une image fixe plutôt que
   de laisser l'utilisateur voir le menu lui-même se faire flasher par son
   propre full refresh — voir `splash.h`/`splash.cpp` (seul fichier de la
   factory qui touche à FreeInkUI, isolé du reste écrit en C).
3. `buttons_init()`, `touch_init()` (best-effort, non bloquant)
4. Probe SD (`msf-fw.bin`) — plus de sondage app1 (retiré, ADR-011)
5. Boucle : `button_wait_press(50)` + poll Home pad

### 5.2 Navigation (différence PiBot)

| Entrée | Rôle |
|---|---|
| Left (GPIO0) | haut |
| Right (GPIO7) | bas |
| **Pad Home** (zone tactile GT911 `raw_x<70, raw_y 660-720`, recalibrée 2026-09-15 — l'ancienne mesure 380-580 du 01:34 ne correspondait plus à la config hôte uploadée) | **select** |
| Power (GPIO3) | select de secours (recovery utilisable sans touch) |

Le touch n'est pas re-dancé entre les polls (bug PiBot/probe connu :
re-resetter à chaque lecture empêchait le scan).

### 5.3 Actions de flash SD

| Item | Mécanisme |
|---|---|
| `SD -> app0` | `esp_ota_begin/write/end` + `esp_ota_set_boot_partition` + reboot (identique PiBot ; seul slot app depuis ADR-011) |

**`SD -> factory` retiré (2026-09-15)** : écrire la partition `factory`
pendant qu'elle s'exécute depuis elle-même (XIP) expose à un brick sans
filet de recovery en cas d'interruption en plein vol (coupure, bug). La
factory est traitée comme le bootloader : mise à jour **uniquement par
USB/serial** (`flash_mgr.py --variant x4pro_factory`), jamais en
self-service sur le terrain. Voir ROADMAP.md, amendement ADR-007/ADR-011.
| `SD -> factory` | **Nouveauté MSF** : `esp_partition_erase_range` + `esp_partition_write` directe, puis reboot sur factory. Le recovery se met à jour lui-même (cohérent ADR-003 : mise à jour SD uniquement) |

Conventions de fichier identiques au PiBot : `msf-fw.bin` → renommé
`msf-fw.ok` (succès) ou `msf-fw.bad` (échec).

Progression : redraw complet à chaque palier de 10 % (un full refresh e-ink
dure déjà ~2-4 s ; ~10 redraws pour un flash de 2 Mo — acceptable pour un
recovery). **Différence PiBot** : pas de redraw partiel possible (e-ink) ;
le PiBot redessinait par zones sur LCD.

### 5.4 Contraintes e-ink (vs LCD PiBot)

- Un seul `gfx_flush()` par écran (framebuffer complet).
- Pas d'animation, pas de feedback visuel d'appui rapide (le full refresh
  est plus lent qu'un appui) — le feedback est le redraw suivant.
- Sélection menu = **inversion vidéo** (fill noir + texte blanc).
- `eink_power_off()` avant tout `esp_restart()` sortant (image conservée).

### 5.5 sdkconfig factory (identique PiBot + points propres)

```ini
CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED=y   # effacement du backup @0xB000
CONFIG_PARTITION_TABLE_OFFSET=0xC000         # DOIT matcher l'app principale
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="../partitions.csv"  # relatif a boards/<board>/factory/ -> boards/<board>/partitions.csv
```

Budget stack/heap : le framebuffer e-ink (48 Ko) vit en **DRAM interne**
(zéro PSRAM requise, choix validé par le probe) ; la factory tient
largement dans sa partition de 1 Mo.

---

## 6. Layout flash 16 Mo (ADR-007, amendé ADR-011 : app1 retiré)

```
0x001000  Bootloader (custom, hooks S3)
0x00B000  ← OTADATA BACKUP (contrat hooks.c / main.c)
0x00C000  Table des partitions (CONFIG_PARTITION_TABLE_OFFSET)
0x00D000  NVS          0x3000
0x010000  otadata      0x2000
0x020000  factory      0x100000  (1 Mo)
0x120000  app0 (ota_0) 0x200000  (2 Mo)
0x320000  secrets      0x240000  (2,25 Mo — blob keystore chiffré, ADR-002 ;
                                  agrandi de 0x40000 par l'espace libéré
                                  par app1, ADR-011)
0x560000  coredump     0x10000
```

Le flash se fait via `flash_mgr.py` (interactif avec mémoire) ou les
`.bat` de `boards/x4pro/flash_scripts/` :
- `x4pro_app` — install complet (firmware + factory + bootloader hook +
  partitions + otadata avec seq=1) → boot direct sur l'app ;
- `x4pro_factory` — rescue (factory + bootloader + partitions + otadata
  zero) → boot GARANTI sur le menu recovery.

Les dossiers `installer/<variant>/` sont alimentes par les postbuild cmake
de chaque projet ; chacun porte une flash map JSON (`<variant>.json`,
format `{meta, files}`) — source unique de verite, consommee par
flash_mgr.py ET reutilisable par un web installer.

---

## 7. Troubleshooting (adapté du PiBot)

| Symptôme | Piste |
|---|---|
| Bootloader trop grand | `CONFIG_BOOTLOADER_LOG_LEVEL` → WARN/NONE ; vérifier marge 0xB000 |
| Bootloader pas reconstruit après modif hooks.c | `rm -rf build/bootloader` / `idf.py bootloader-flash` |
| Factory boote en boucle | Restore otadata non effectué : vérifier magic backup, log "secteur backup efface" |
| Pas de restore après power cycle | PiBot §"otadata not restored" : vérifier `CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED=y` (abort silencieux sinon) |
| Écran gris + BUSY bloqué | PSR 0x37 écrit au DRF : power cycle USB 30 s, corriger la séquence (§5.1 / hardware-specs) |
| Touch mort dans la factory | Upload config GT911 (0x8047==0x00 → dance POR + 185 o) — cf. `touch.c`, ne pas re-dancer entre polls |
| Crash SD (`LoadProhibited` tlsf_malloc) | PiBot : heap trop basse pour SD+OTA ; ici 512 Ko SRAM S3, surveiller `esp_get_free_heap_size` en debug |

---

## 8. Non porté du PiBot (volontairement)

- **Snapshot system** (`docs/Factory/Snapshot system.md`) : capture d'écran
  LCD → SD pour la doc. Non applicable : e-ink 1 bpp, et la doc se fait
  autrement (photos). Référence gardée dans le repo PiBot.
- **Encodeur rotatif** : pas de hardware sur le X4 Pro.
- **Buzzer** : pas de hardware.
- **Backends log SD/telnet/web** (esp3d_log) : air-gap, backend serial
  suffit pour le dev.

---

*Sources : doc PiBot originale (`Luc-Pibot-cnc-pendant-firmware/docs/Factory/`,
MIT/LGPL selon fichiers), bring-up X4 Pro (`docs/hardware-specs.md`,
`test_apps/x4pro-probe`), ADR-007/008/009 (`docs/ROADMAP.md`).*
