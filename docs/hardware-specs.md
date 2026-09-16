# XTEINK X4 Pro — Fiche Hardware & Drivers (référence projet MySafeFob, ex-standalone-TOTP)

> **Statut (2026-09-13)** : ✅ **Hardware entièrement bring-upé sur notre unité** —
> SoC/flash, bus I2C, RTC, jauge, touch GT911 (mapping 4 coins + pad Home validés),
> E-Ink UC8279 (affichage + orientation définitive validés), SD, frontlight, boutons,
> charge. **Bring-up hardware terminé.**
>
> Chaque section indique son niveau de preuve : ✅ validé sur notre unité / 🔧 confirmé FreeInk
> (pas re-testé chez nous) / ⚠️ à valider.

---

## 🔧 SoC (✅ validé — esptool + probe)

| Paramètre | Valeur |
|---|---|
| **Modèle** | ESP32-S3R8 (QFN56), revision v0.2 |
| **Cœurs** | 2 × Xtensa LX7, 240 MHz |
| **SRAM** | 512 KB |
| **PSRAM** | **8 Mo octal embarquée** (testée OK au boot) |
| **Flash** | **16 MB** quad, 3.3 V (détectée par esptool ; l'en-tête binaire du bootloader usine annonçait 2 MB — ignorer, taille réelle 16 MB) |
| **Cristal** | 40 MHz |
| **Wi-Fi MAC** | 7C:0C:5F:41:9E:8C |
| **Connectivité** | Wi-Fi 2.4 GHz + BLE (radio coupée hors sync temps — ADR-001) |
| **USB** | GPIO19 = D−, GPIO20 = D+ (USB natif S3, via pogo dock) — **ne pas repurpose** |
| **Flash/debug** | USB-Serial/JTAG natif via pogo dock → COMx, reset auto fonctionnel (`idf.py -p COMx flash monitor`) |

## 🗺️ Carte GPIO complète (vue consolidée)

| GPIO | Fonction | Direction | Notes |
|------|----------|-----------|-------|
| 0 | Bouton **Left** | IN pull-up | ⚠️ strapping pin — pas maintenu LOW au boot |
| 1 | Rail périphérique | OUT | **HIGH = ON**, tenu HIGH en permanence |
| 2 | Power-enable **touch** | OUT | **LOW = ON** (actif-low) — tenu LOW en fonctionnement |
| 3 | Bouton **Power** | IN pull-up | actif-LOW ; ne coupe pas l'alim (GPIO) |
| 4 | **RST touch** (GT911) | OUT | dance de reset, cf. § Touch |
| 5 | Power-enable **SD** | OUT | **LOW = ON** — pulse HIGH 80 ms → LOW 120 ms au mount |
| 6 | E-Ink **BUSY** | IN | **actif-LOW** (BUSY_N) |
| 7 | Bouton **Right** | IN pull-up | actif-LOW |
| 8 | Frontlight **cool** | OUT (LEDC) | PWM 25 kHz 10 bits, actif-HIGH |
| 9 | Frontlight **warm** | OUT (LEDC) | PWM 25 kHz 10 bits, actif-HIGH |
| 10 | **INT touch** (GT911) | IN/OUT | adresse au reset + mode config update (cf. § Touch) |
| 11 | E-Ink MOSI | OUT | SPI |
| 12 | E-Ink SCLK | OUT | SPI 10 MHz |
| 13 | E-Ink CS | OUT | |
| 14 | E-Ink RST | OUT | |
| 18 | E-Ink DC | OUT | |
| 19 / 20 | USB D− / D+ | — | ne pas repurpose (console) |
| 21 | **Charge** détection | IN | actif-HIGH (raw level = en charge) |
| 26-32 | Bus flash | — | **interdit** |
| 33-37 | Bus PSRAM | — | **interdit** |
| 38 | I2C **SCL** | OD | bus partagé, 400 kHz |
| 39 | I2C **SDA** | OD | bus partagé, 400 kHz |
| 40 | SD **DAT0** | SDMMC | slot 1, 1-bit |
| 41 | SD **CLK** | SDMMC | 40 MHz |
| 42 | SD **CMD** | SDMMC | |
| 43 / 44 | UART TX / RX | — | réservé debug |

## 🔌 Bus I2C #0 — SDA=39 / SCL=38 (✅ validé)

| Adresse | Périphérique | Statut |
|---------|--------------|--------|
| 0x51 | **BM8563** RTC | ✅ validé |
| 0x63 | **CW2017** fuel gauge | ✅ validé |
| 0x5D / 0x14 | **GT911** touch | ✅ validé (scan) |

> ⚠️ **Anomalie driver I2C IDF 5.4 constatée** : lectures d'**1 octet NACK** systématiquement
> (zero-byte probe et 1-byte read échouent ; ≥ 2 octets passent). Toute détection de
> présence doit lire ≥ 2 octets. GT911 invisible à un scan classique (adressage 16 bits).

## 📟 Écran E-Ink (✅ validé — UC8279 identifié, patterns affichés)

| Paramètre | Valeur |
|---|---|
| **Panèle** | 4.3" 800×480 B/W, landscape natif |
| **Contrôleur** | **UC8279** — identifié par `einkprobe` : réponse `00 0F 68` (CHIP_VER 0x0F, LUT_VER 0x68), batch UltraChip |
| **Adressage interne** | 800×600 adressées, fenêtre visible = **gates 120..599** (gateOffset 120) |
| **BUSY** | **actif-LOW** (0 = occupé, 1 = idle) |
| **Alimentation** | Booster interne — **aucun enable GPIO** |
| **SPI** | 2 (FSPI), 10 MHz, write-only, CS manuel ; DMA limité à 32 768 o/transaction → chunker à 16 Ko |

### Séquence UC8279 validée (réf. `_ext/Uc8279X4Driver.cpp`)

**Init** (après RST) :
```
PSR  (0x00) = 37 4D          ; REG=1 (LUT externe) à l'init
TRES (0x61) = 03 20 02 58    ; 800 x 600
GSST (0x65) = 00 00 00 00
PFS  (0x03) = 20
PLL  (0x30) = 0E             ; X4 Pro UNIQUEMENT (stock X4C : no-op)
GATE_SCAN (0xE1) = 02
; PAS de BTST ni PWS — PWR/VDCS/BTST restent panel-programmés (OTP/MTP)
```

**Stream d'un plan** (DTM1 0x10 = OLD / DTM2 0x13 = NEW), 100 octets/ligne :
```
120 lignes blanches (0xFF)        ; gates 0..119 non visibles
480 lignes framebuffer, ordre direct y=0→479, octets tels quels
pad blanc jusqu'à 600 gates
```

**Refresh FULL** (ordre exact — RE du firmware stock) :
```
CDI    (0x50) = 97            ; 1 octet SEUL (cdiBwFull) — pas de 2e octet !
CCSET  (0xE0) = 02
TSSET  (0xE5) = 1E            ; GC full
PON    (0x04) + attendre BUSY HIGH
PSR    (0x00) = 17 4D         ; 0x37 & 0xDF : REG clr → waveform OTP.
                              ; OBLIGATOIRE entre PON et DRF (PON recharge le MTP,
                              ; seuls les PSR post-PON sont latchés)
DRF    (0x12) + attendre départ (BUSY drop, 50 ms) puis fin (BUSY HIGH, timeout 8 s)
; pas de restore CDI après (absent du stock)
```

**Power off** : POF (0x02) + wait idle.

### Identification du batch (à faire AVANT toute init)
`einkprobe` (bit-bang SPI, cmd 0x70/0x71 half-duplex sur MOSI=11) → lire le registre
d'identification. Réponse `00 0F 68` = UC8279. Les variantes possibles par batch :
SSD1677 (unités originales) / UC8179 (batchs récents) / UC8279 (notre unité) — même
verre, même pinout, séquences différentes. Drivers de référence dans le freeink-sdk.

### ✅ Orientation (DÉFINITIF — mesures 2026-09-13)

**Config validée : scan MTP (PSR `0x17` post-PON, REG=0) + stream brut (mode 0)
= rotation 90° CW matérielle, identique au firmware stock.** Le panel physique
est natif **800×480 paysage**, monté en portrait 480×800 dans la liseuse — le
fb est donc à dessiner en natif paysage. Aucune transform logicielle nécessaire.

⚠️ **Ne JAMAIS écrire PSR `0x37` (REG=1) entre PON et DRF** : sur ce UC8279
(rev v0.2) le full GC ne termine jamais (BUSY figé LOW > 20 s, écran gris,
POF sans effet — reproduit du cold boot, mesures 11:21→11:58). Le DRF doit
impérativement scanner avec les registres MTP, comme le stock
(`psr0 & 0xDF = 0x17`).

Historique des mesures (contexte, ne pas ré-ouvrir) : avec REG=1 le scan était
instable entre 1er affichage (miroir X) et suivants (miroir X+Y) car PON
recharge le MTP ; l'échec 11:21 a d'abord été attribué à tort à une écriture
PSR avant le stream (vrai coupable : bug de tick `pdMS_TO_TICKS(5)` = 0 tick
→ task_wdt IDLE0, corrigé). Les transforms logiciels modes 1-3 de
`uc_stream_plane_mode` sont conservés pour référence mais obsolètes.

Outil : `einkuc2 <0-3> <0-6>`, pattern 6 = repères asymétriques natifs
(A 40×40 TL, B 120×60 TR, C 64×120 BL, D 104×100 BR, barre H 400×20).
**Validation 12:18** : mode 0 → repères exactement à leurs places en portrait
+ barre verticale attendue ; 3 affichages strictement identiques, DRF ~2-4 s
sans timeout. `einkuc 2` → tout noir plein écran (✅).

## 👆 Touch GT911 (✅ validé — points tactiles réels)

| Signal | GPIO | Notes |
|--------|------|-------|
| SDA/SCL | 39/38 | bus partagé, 400 kHz |
| INT | **10** | LOW au reset → adresse 0x5D ; **maintenu LOW au POR → mode CONFIG UPDATE** |
| RST | **4** | |
| Power | **GPIO2 actif-LOW** | tenu LOW en permanence en fonctionnement (config RAM = volatile) |

### Découvertes critiques (unité 2026-09)

1. **Pas de self-load** : 0x8047 (config version) lit **0x00** après toute dance conforme
   → OTP config vide d'usine. Le chip répond I2C (Product ID « 911 » @0x8140) mais ne
   scanne pas sans config. → **upload hôte obligatoire à chaque boot**.
2. **Mode CONFIG UPDATE requis pour écrire** : en mode normal, les écritures @0x8047
   sont acquittées puis **ignorées**. Séquence qui marche :
   ```
   RST low + INT low AVANT power-on de la rail (GPIO2)   ; POR sous reset, update mode
   rail on 50 ms → release RST → 60 ms                   ; INT TOUJOURS low
   écrire 185 octets @0x8047 (config + checksum @0x80FF)
   écrire 0x01 @0x8100 (config_fresh)                     ; INT low pendant TOUTE l'écriture
   relâcher INT (input+pullup)
   ```
3. **Checksum** : somme des 185 octets (0x8047..0x80FF) ≡ 0 (mod 256).
   (Réf. `_ext/GoodixFW.h` ; map registres `_ext/gt911_structs.h`. ⚠️ le checksum de
   `g911xOrig` est corrompu dans la source Staars — toujours recalculer.)
4. **Pas de persistance** : la config vit en RAM uniquement → ré-upload après tout
   reset RST ou coupure de rail GPIO2. Implémentation driver : `dance → lire 0x8047 →
   si 0x00 : upload → scan`.
5. **Coordonnées validées** : status 0x814E (bit7 ready, bit4 home, nibble count),
   points 0x8150 (8 o/pt, X-lo en byte 0). **Bruts portrait : X 0-480, Y 0-800** →
   **swapXY = true** pour l'affichage 800×480 (validé par points réels 00:02).
   **Test 4 coins 00:55 (ordre HG, HD, BD, BG)** : bruts (475,80) (476,660) (52,661)
   (48,58) → mapping définitif : **swapXY = true, invert_y = true** (post-swap,
   i.e. raw X inversé), invert_x = false. Vérifié : les 4 coins retombent exactement.
   **Reconfirmé 2026-09-15** (factory build 12:25, config hôte uploadée) :
   HG (475,17) HD (473,612) BD (49,640) BG (37,31) — mêmes proportions,
   même mapping (`raw_x` pilote l'axe Y écran inversé, `raw_y` pilote l'axe X
   écran direct). ⚠️ `touch.c` ne l'applique actuellement PAS sur `pt.x`/`pt.y`
   (un commentaire y affirme à tort que le brut est "déjà portrait") ; sans
   conséquence pour l'instant (seule la zone Home, en coords brutes, est
   utilisée) mais à corriger avant tout usage d'un tap positionnel en UI
   (Phase 8.4).
6. **Pièges** : `pdMS_TO_TICKS(2)/(8)` = 0 tick (tick FreeRTOS 10 ms) → RST glitch ;
   utiliser `esp_rom_delay_us`. Le POR doit se faire **RST asserté** sinon état/adresse
   incohérents. Détection : lire ≥ 2 octets (cf. anomalie I2C).
7. **Config en service** : version 0x81, X=480 (E0 01), Y=800 (20 03), base
   `g911xOrig` adaptée ; implémentation de référence : `cmd_touchcfg` (x4pro-probe).
8. **Home pad = POINT TACTILE ordinaire** avec notre config uploadée (le bit
   0x10 de 0x814E ne s'est jamais levé avec elle). Le SDK freeink (RE Ghidra
   du firmware OEM, `xteink-x4pro-support.md`) affirme que sur le firmware
   stock — qui utilise le **self-load**, jamais l'upload hôte — Home EST une
   vraie touche capacitive GT911 (`0x814E & 0x10`). Le driver factory
   (`touch.c`) teste les deux : la zone logicielle (fallback) ET le bit key
   (actif si le self-load réussit un jour).
   → **Zone logicielle recalibrée 2026-09-15** (log réel, factory build
   12:25, config hôte uploadée) : appuis répétés sur le pad Home physique →
   raw **(x=2..8, y=693..696)**, très stable → raw rx ∈ [0,70], ry ∈ [660,720].
   ⚠️ Remplace la mesure du 01:34 (36,479) qui ne correspondait plus à rien
   avec la config actuellement en usage — probablement une config ou un état
   du chip différent entre les deux sessions de mesure. Si l'upload hôte ou
   la dance change de nouveau, revalider cette zone avant de la considérer
   figée.
9. **Driver de référence utilisateur** : `_ext/touch_gt911/` (ESP3D) — bonne base
   pour le driver final (probing 0x5D/0x14, INT en hint IRQ + polling, swap/invert),
   à combiner avec l'upload config ci-dessus.

## 🕐 RTC BM8563 (✅ validé)

| Paramètre | Valeur |
|---|---|
| Adresse I2C | 0x51 |
| Backup | Batterie principale — **tient l'heure à travers les flashs** (validé) |
| Lecture | regs 0x02-0x08 en BCD (cf. `cmd_rtc`) |

## 🔋 Jauge CW2017 (✅ validé)

| Paramètre | Valeur |
|---|---|
| Adresse I2C | 0x63 |
| VCELL | regs 0x02/0x03, 14-bit, **mV = (raw·5 + 8) >> 4** — 4365 mV mesurés, formule validée |
| SoC | reg 0x04 — 100 % mesuré (profil BATINFO usine chargé) ; renvoie 0 % sans profil |
| Charge | GPIO21, actif-HIGH |

## 💡 Frontlight dual warm/cool (✅ validé)

| Canal | GPIO | LEDC | Polarité |
|-------|------|------|----------|
| Cool/blanc | 8 | ch 4 | actif-HIGH |
| Warm | 9 | ch 5 | actif-HIGH |

PWM 25 kHz, 10 bits. Gamma cool/warm validés (montée progressive).

## 💾 SD card — SDMMC natif 1-bit (✅ validé)

| Signal | GPIO |
|--------|------|
| CLK | 41 |
| CMD | 42 |
| DAT0 | 40 |
| Power | 5 (actif-LOW, pulse au mount) |

Slot 1, 40 MHz, pull-ups internes. **Carte 16 Go montée, FAT32 lisible, répertoires
stock visibles (XTCACHE/XTDATA), signature MBR 55AA lue** — cf. `cmd_sd`.

## 🔘 Boutons (✅ validés — digitaux, actif-LOW, INPUT_PULLUP)

| Bouton | GPIO | Notes |
|--------|------|-------|
| Left | 0 | ⚠️ strapping pin : OK à runtime, pas maintenu au reset |
| Right | 7 | |
| Power | 3 | ne coupe pas l'alimentation (simple GPIO) |
| Home | — | via GT911 (§ Touch — à valider) |

## 📋 Drivers à produire (Phase 8)

| Driver | Périphérique | Interface | Priorité | Base de code |
|--------|-------------|-----------|----------|--------------|
| `eink_driver` | UC8279 (+ auto-detect batch) | SPI 10 MHz | P0 | séquence § E-Ink (validée) |
| `touch_driver` | GT911 + upload config | I2C 400k | P0 | `touch_gt911/` + `cmd_touchcfg` |
| `pmic/rails` | GPIO1/2/5 | GPIO | P0 (prérequis) | `set_rails` probe |
| `rtc_driver` | BM8563 | I2C | P0 (TOTP) | `cmd_rtc` probe |
| `button_driver` | 3 boutons + Home | GPIO/GT911 | P1 | `cmd_btn` probe |
| `sd_driver` | SDMMC 1-bit | SDMMC | P1 (update/backup) | `cmd_sd` probe |
| `battery_driver` | CW2017 + charge | I2C + GPIO21 | P2 | `cmd_gauge`/`cmd_charge` |
| `backlight_driver` | LED warm/cool | LEDC 25k | P2 | `cmd_light` probe |

## ✅ Validation effectuée (état 2026-09-13)

- [x] esptool : S3R8 rev v0.2, PSRAM 8 Mo, flash 16 MB, MAC
- [x] Flash path : USB natif via pogo dock, reset auto
- [x] Rails GPIO1=H / GPIO2=L / GPIO5=L (prérequis universel)
- [x] Bus I2C 39/38 : BM8563 @0x51, CW2017 @0x63, GT911 @0x5D/0x14
- [x] RTC BM8563 : lecture + backup à travers flashs
- [x] CW2017 : VCELL 4365 mV, SoC 100 %, formule validée ; charge GPIO21
- [x] E-Ink UC8279 : identification + init + patterns 0-5 affichés (FULL GC)
- [x] Touch GT911 : upload config + scan validé (points 480×800 bruts)
- [x] Boutons Left/Right/Power (GPIO) ; frontlight cool/warm ; SD 16 Go FAT32
- [x] Touch : test 4 coins → **swapXY=true, invert_y(post-swap)=true** (00:55)
- [x] Touch : ~~key zone Home pad~~ → **Home = point tactile (36,479) brut**,
      zone logicielle driver (rx<70, ry 380-580) — validé 01:34
- [x] E-Ink : scan **stable** + orientation **définitive** (validé 12:18) :
      PSR `0x17` (REG=0, scan MTP) entre PON et DRF + stream brut = rotation
      90° CW = orientation stock. **Interdit : PSR `0x37` (REG=1) au DRF →
      full GC bloque (BUSY LOW > 20 s, écran gris)**. Aucun transform logiciel.

## 📦 Partitions stock 16 MB (référence — dump FreeInk)

| Label | Offset | Taille |
|---|---|---|
| nvs | 0x009000 | 0x5000 |
| otadata | 0x00E000 | 0x2000 |
| app0 | 0x010000 | 0x7E0000 |
| app1 | 0x7F0000 | 0x7E0000 |
| spiffs | 0xFD0000 | 0x14000 |
| coredump | 0xFE4000 | 0x1C000 |

Stock = dual OTA, boot sur **app1** : app0 @ 0x10000 = variante Arduino erronée
(`ESP32S3_X4_TL`), app1 @ 0x7F0000 = le vrai firmware X4 Pro (`ESP32S3_X4_TL_SSD1677`,
`XTEink::SSD1677_800x480` / `GT911Driver`). Notre probe (factory @ 0x10000) a écrasé le
début d'app0 — qui n'était PAS le vrai firmware. **app1 probablement toujours intact.**
(phy_init @ 0xF000 chevauche l'ancien otadata — récupération stock hors scope, décision
no-backup 2026-09-12.)

---

*Sources : `freeink-sdk/docs/xteink-x4pro-support.md` (MIT, confirmed on hardware),
`_ext/Uc8279X4Driver.cpp`, `_ext/GoodixFW.h`, `_ext/touch_gt911/`, esptool, probe x4pro
(`test_apps/x4pro-probe`). Journal détaillé : `docs/test-log.md`.*
