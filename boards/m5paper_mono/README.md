# Board `m5paper_mono` — M5Stack M5PaperMono (STUB, ADR-008)

> **Statut** : documentation uniquement. **Aucun code avant d'avoir le
> hardware en main** (hors stock au moment de la décision, 2026-09-13).
> L'UI converge avec le X4 Pro vers du **480×800 portrait** — c'est la
> board portuaire naturelle du projet.

## Specs (récoltées du shop M5Stack, 2026-09-13)

| Élément | Valeur |
|---|---|
| Référence | M5PaperMono, SKU C153, 65 $ (out of stock) |
| SoC | ESP32-S3R8, 16 Mo flash, 8 Mo PSRAM octal, Wi-Fi 2.4 GHz |
| E-paper | **SSD1677 480×800 natif portrait**, 4 niveaux de gris |
| Touch | **FT6336G** (driver standard, contrairement au GT911 du X4 Pro) |
| RTC | RX8130CE |
| Batterie | 1150 mAh (via M5PM1), M5IOE1 IO expander |
| Boutons | 2 user + power ; microSD ; USB-C |
| Extras (jamais utilisés — air-gap) | NFC ST25R3916, LoRa SX1262, micro PDM, buzzer, IMU BMI270, RGB LED |

## Notes de portage anticipé

- Le probe `test_apps/x4pro-probe/` contient déjà un chemin driver SSD1677
  (commandes `eink_ssd`, busy=**HIGH** — polarité inverse du UC8279) : c'est
  la base du futur driver e-ink de cette board.
- E-paper natif **portrait** : pas de rotation matérielle à gérer (simplifie
  le driver vs X4 Pro).
- Les extras radio (NFC/LoRa) restent désactivés par le modèle air-gapped.

## Pour activer cette board plus tard

1. `boards/m5paper_mono/board_config.cmake` + `sdkconfig.defaults`
   (calques de `boards/x4pro/`).
2. `boards/m5paper_mono/flash_params.json` + README mis à jour avec les
   mesures réelles.
3. Drivers : SSD1677 (base `eink_ssd` du probe), FT6336G (driver existant
   dans l'écosystème IDF), RX8130CE.
