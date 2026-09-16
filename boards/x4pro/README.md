# Board `x4pro` — XTEINK X4 Pro Developer Edition

Board principale du projet MySafeFob. **Bring-up hardware complet et validé**
(2026-09-13) — la référence unique des pins/drivers est
[`docs/hardware-specs.md`](../../../docs/hardware-specs.md) (à la racine du
repo). Ce README ne fait que résumer et pointer.

## Résumé hardware

| Élément | Valeur |
|---|---|
| SoC | ESP32-S3R8 (2×LX7 @240 MHz, 512 KB SRAM, **8 Mo PSRAM octal**) |
| Flash | 16 MB quad, 3.3 V |
| E-paper | 4.3" 800×480 B/W, contrôleur **UC8279** (SPI 10 MHz, BUSY actif-LOW) |
| Touch | **GT911** I2C @0x5D (upload config obligatoire à chaque boot) |
| RTC | **BM8563** @0x51 (backup batterie, tient l'heure à travers les flashs) |
| Gauge | **CW2017** @0x63 + charge sur GPIO21 |
| Frontlight | dual warm/cool, LEDC 25 kHz 10 bits (GPIO8/9, actif-HIGH) |
| SD | SDMMC natif 1-bit (CLK=41, CMD=42, DAT0=40, power GPIO5 actif-LOW) |
| Boutons | Left=GPIO0 (⚠ strapping), Right=GPIO7, Power=GPIO3 — actif-LOW |
| Home | zone tactile logicielle GT911 (point brut ~(36,479)) |
| Rails | GPIO1 HIGH permanent (périphs), GPIO2 LOW (touch), GPIO5 LOW (SD) |

## Contraintes critiques (validées, ne pas rouvrir)

- **E-ink** : entre PON et DRF, PSR doit être `0x17` (REG=0, scan MTP).
  **JAMAIS** `0x37` (REG=1) au DRF → full GC bloqué (BUSY LOW > 20 s,
  écran gris). Orientation = rotation 90° CW matérielle, framebuffer natif
  800×480 paysage, stream brut, aucun transform logiciel.
- **GT911** : pas de self-load (config OTP vide) → upload hôte de 185 octets
  @0x8047 (checksum) + 0x01 @0x8100 à chaque boot, en mode CONFIG UPDATE
  (POR sous reset RST=GPIO4 avec INT=GPIO10 LOW). Config volatile : ré-upload
  après tout reset. Mapping : swapXY=true, invert_y(post-swap)=true.
- **I2C** (anomalie IDF 5.4) : toute lecture de présence doit lire ≥ 2 octets.
- **pdMS_TO_TICKS < 10 ms = 0 tick** → utiliser `esp_rom_delay_us` dans
  les séquences timing critiques (reset touch notamment).

## Driver de référence

Le probe `test_apps/x4pro-probe/` contient le code de bring-up validé :
commandes `einkucinit`/`einkuc`/`einkuc2` (UC8279), `touchcfg`/`touchinfo`
(GT911 + upload config), `cmd_rtc`, `cmd_gauge`, `cmd_sd`, `cmd_btn`,
`set_rails`. Les drivers finaux de la tâche 8c en sont extraits.

## Toolchain

Cible IDF **5.5.5** (install en cours côté utilisateur). Le probe tourne en
5.4.3 — portage hooks bootloader S3 à vérifier à la 1re compilation (ADR-007).
