# Journal de test — MySafeFob (ex-standalone-TOTP)

Format défini dans `docs/TEST-PLAN.md` §7. Un bloc par session.

---

## Session 2026-09-07 — Phase 3 (Python)

### Setup
- Service : GitHub (`totp-user-test`)
- Outil : `test_apps/totp_reference.py`
- Référence : Authy (même secret)

### Résultats
| Test | Résultat | Notes |
|------|----------|-------|
| T1 | ✅ PASS | 5+ codes générés, tous identiques à Authy |
| T2 | ✅ PASS | Connexion GitHub réussie avec le code du script |
| T3 | ✅ PASS | Drift = 0 s, transitions 30 s synchrones avec Authy (surveillance continue) |
| T3-bis | ✅ PASS | Décompte 30 s + auto-génération : bascule au même instant qu'Authy, mêmes valeurs |
| T4 | ✅ PASS | Ancien code rejeté après expiration |

### Problèmes rencontrés
- Aucun

### Actions
- Phase 3 validée → passage à la Phase 4 (ESP-IDF)

---

## Session 2026-09-07 — Phase 4 (ESP32, app de test ESP-IDF)

### Setup
- Board : ESP32 dev board générique (cible `esp32`), flash 4 MB détectée
- ESP-IDF : v5.4.3, GCC 14.2.0 xtensa
- Projet : `test_apps/esp32-totp-test` (REPL `console`, prompt `totp>`)
- Secret : `JBSWY3DPEHPK3PXP` (bidon, identique au script Python)
- Heure : saisie manuelle via `st <timestamp>` (pas de RTC batterie)

### Résultats
| Test | Résultat | Notes |
|------|----------|-------|
| E1 | ✅ PASS | Self-tests boot : Base32 2/2, vecteurs RFC 6238 6/6 (incl. T=20000000000 → 65353130) |
| E4 | ✅ PASS | Codes corrects pour le temps de l'ESP32 ; offset de sync mesuré : **26 s constant** (délai copier-coller PowerShell → moniteur), pas de dérive en session |
| E2/E3 | ✅ PASS | Vrai secret GitHub saisi via `s`, codes identiques à Authy en continu → code valide par construction ; login GitHub jugé redondant (il testerait la fenêtre serveur, pas notre moteur) |

### Problèmes rencontrés
1. **Build** : tableau `base32_dec` surdimensionné (272 éléments) → remplacé par designated initializer C99.
2. **Build** : `-Werror=format-truncation` sur `snprintf("%0*u")` → formatage manuel chiffre par chiffre.
3. **Build** : `bool` sans `#include <stdbool.h>` → ajouté.
4. **Runtime** : `fgets` imbriqués sur stdin UART retournent NULL (saisie secret/temps impossible) → migration de l'app de test sur le REPL `esp_console` (composant nommé `console` en IDF 5.4.x), arguments en ligne unique (`s <secret>`, `st <ts>`).
5. **Affichage** : `T=20000000000` tronqué en `-1474836480` par cast `(long)` 32-bit dans le log → corrigé en `(long long)` (cosmétique, temps bien 64-bit).

### Mesures
- Offset de sync manuel : 26 s, constant sur plusieurs essais → stabilité de l'horloge ESP32 confirmée ; erreur entièrement attribuable au délai copier-coller.
- Contournement mesuré : `st <timestamp + 26>` → offset résiduel ~0 s.

### Actions
- **Phase 4 validée complète** (E1–E4 + E2/E3 par comparaison Authy). Le moteur TOTP est officiellement validé sur ESP32.
- Prochaine étape : Phase 5 (bring-up X4 Pro) — en attente de réception du device.

---

---

## Session 2026-09-12 — Phase 5 : réception X4 Pro

### Setup
- Device : XTEINK X4 Pro Developer Edition reçu (commande xteink.com du 2026-08-29)
- SoC : ESP32-S3 confirmé (badge/outil) — conforme aux specs presse
- Taille flash : non vérifiée (esptool) au moment de la réception

### Résultats
| Étape | Résultat | Notes |
|-------|----------|-------|
| Réception | ✅ | SoC S3 conforme ; accessoires/boîte à inventorier (dock pogo ?) |
| Identification esptool | ✅ | ESP32-S3 (QFN56) rev v0.2, PSRAM 8 Mo embarquée, flash 16 MB quad, cristal 40 MHz, MAC 7c:0c:5f:41:9e:8c |
| Chemin de flash | ✅ | **USB natif S3 (USB-Serial/JTAG) via pogo dock → COM5**, reset auto fonctionnel |
| Probe flashé + boot | ✅ | PSRAM 8 Mo détectée et testée OK, REPL USB-Serial/JTAG opérationnel (prompt `probe>`) |
| i2cguess (23 paires) | ⚠️ Rien trouvé | Cause identifiée : bus I2C sans rails d'alim — GPIO1 (rail périph.) jamais activé |
| Pinout officiel | ✅ | **FreeInk SDK** (`docs/xteink-x4pro-support.md`, confirmed on hardware) : bus I2C SDA=39/SCL=38, GT911 0x5D (power GPIO2 actif-LOW, INT=10, RST=4), BM8563 0x51, CW2017 **0x63** (corrigé vs 0x62), E-Ink SPI 12/11/13/18/14/6, SDMMC 41/42/40 + GPIO5, frontlight 8/9, boutons 0/7/3 |

### Décisions
- **Pas de backup du firmware stock** (utilisateur, 2026-09-12) : risque accepté, justifié — retour au stock hors scope, toute l'info du dump OEM déjà publiée par FreeInk (pinout, séquences, BATINFO CW2017, partitions). app1 probablement encore intact si recovery jamais nécessaire.

### Actions
- Probe enrichi : commandes `rails` (GPIO1=H, GPIO2=L, GPIO5=L) et `rtc` (lecture BM8563)
- hardware-specs.md réécrit avec le pinout confirmé FreeInk
- Prochaine validation : `rails` → `i2cscan 39 38` (attendu 0x51 + 0x63 + 0x5D) → `rtc`

---

## Session 2026-09-12 (soir) — Phase 5 : bus I2C validé

### Setup
- Probe `x4pro-probe` enrichi : `i2cscan` transactionnel (lecture 2 octets reg 0x00, 100 ms), `gauge` (CW2017), `touchid` (GT911 reset dance)
- IDF v5.4.3, flash via COM5 (USB natif S3 + pogo dock)

### Résultats
| Étape | Résultat | Notes |
|-------|----------|-------|
| rails (GPIO1=H, GPIO2=L, GPIO5=L) | ✅ | requis avant tout scan |
| i2cscan 39 38 | ✅ | **0x14 (GT911), 0x51 (BM8563), 0x63 (CW2017)** — 3 devices |
| gauge (CW2017 @0x63) | ✅ | VERSION reg = 0x0F, **VCELL = 4365 mV**, SoC = 100 % (batterie pleine ; BATINFO usine chargé) |
| touchid (GT911) | ✅ | Product ID « 911 » rev 0x00 **@0x14** |

### Découvertes hardware (à intégrer aux specs)
1. **GT911 à 0x14 sur notre unité**, pas 0x5D — la dance de reset INT-low n'a pas basculé l'adresse ; 0x14 est l'adresse Goodix par défaut. Le driver doit tenter 0x14 en priorité puis 0x5D.
2. **Lecture 1 octet NACK sur le driver I2C maître IDF 5.4** (zero-byte `i2c_master_probe` ET lecture 1 octet échouent, lectures ≥ 2 octets passent — confirmé : BM8563 lu en 7 octets OK alors que probe 1 octet montrait un bus vide). Toute présence-détection doit lire ≥ 2 octets.
3. Faux positifs de scan sur les paires {0,2} et {4,0} : GPIO0 (bouton Left), GPIO2 (power touch), GPIO4 (RST touch) sont des pins de contrôle — les scanner comme bus I2C perturbe le vrai bus. Ne pas en tenir compte.
4. SoC CW2017 = 100 % avec VCELL 4365 mV → profil BATINFO chargé en usine, la formule mV = (raw×5+8)>>4 est correcte.

### Problèmes rencontrés
- `gauge`/`touchid` non reconnus après premier reflash : fonctions présentes mais **oubli d'enregistrement** dans le bloc `app_main` → corrigé.
- Scan transactionnel 0 device : probe 1 octet / 25 ms NACK systématiquement → passé à 2 octets / 100 ms.

### Actions
- Bus I2C **entièrement validé** : RTC ✅, gauge ✅, touch ✅
- Reste Phase 5 : E-Ink (premier affichage), SD, boutons, frontlight, charge
- Source communautaire repérée : firmware CrossPoint (crosspoint-reader) supporte le X4Pro (SSD1677 + GT911 confirmés), construit sur le même freeink-sdk → référence pour les séquences d'init

---

---

## Session 2026-09-12 (nuit) — Phase 5 : E-Ink UC8279 identifié + touch GT911 fonctionnel

### Setup
- Probe `x4pro-probe` (IDF v5.4.3, flash COM5)
- Références : clone du driver FreeInk `Uc8279X4Driver.cpp` (`_ext/`), doc FreeInk `xteink-x4pro-support.md` (`_ext/`), table config Staars/GT911_ESP32 `GoodixFW.h` (`_ext/`), driver maison utilisateur `_ext/touch_gt911/` (ESP3D, Sunton 8048S050C — pas de table registre, s'appuie sur self-load)

### Résultats
| Étape | Résultat | Notes |
|-------|----------|-------|
| einkprobe (bit-bang identification) | ✅ | `VER = 00 0F 68 00 00` → **UC8279** (CHIP_VER 0x0F, LUT_VER 0x68), batch UltraChip — pas un SSD1677 ni UC8179 |
| einkucinit + einkuc 0-5 (séquence UC8179) | ⚠️ partiel | affichage OK mais séquence erronée (PSR 3F/0A, BTST, lignes inversées, pas de gate offset) |
| einkucinit + einkuc 0-5 (séquence UC8279 corrigée) | ✅ | patterns affichés FULL GC sans timeout BUSY |
| touch muet malgré dance doc (10/10/100) | ❌ | `touchinfo` : **0x8047 = 0x00**, résolution 0×0, tout zéro → self-load jamais effectué |
| dance corrigée (POR sous reset + µs exacts) | ❌ | 0x8047 toujours 0x00 → **cette unité ne self-loadera jamais** (OTP config vide d'usine) |
| écriture config en mode normal | ❌ | I2C ACK mais ignoré (0x8047 relit 0x00 après reset) |
| écriture config en mode CONFIG UPDATE (INT low au POR) | ✅ | `[A]` relecture : `81 E0 01 20 03` ; `[B]/[B2]` : stable en RAM ≥ 1 s |
| persistance config (power-cycle) | ❌ | `[C]` : 0x00 après coupure rail → **pas de flash config inscriptible** sur cette variante |
| **scan tactile avec config RAM, sans reset** | ✅ | `[C2]` : points réels remontent, X ∈ 0-480, Y ∈ 0-800 (portrait brut), ex. (404,657), (2,696), (50,11) |

### Découvertes clés (à intégrer aux specs)

**E-Ink UC8279** (réf. exacte `Uc8279X4Driver.cpp`, `_ext/`) :
1. Init : PSR `0x37 0x4D`, TRES 800×600, GSST 0, PFS 0x20, **PLL (0x30) = 0x0E — X4 Pro uniquement**, gate scan 0x02. **Pas de BTST ni PWS** (PWR/VDCS restent en OTP/MTP).
2. Stream plan : **120 lignes blanches d'abord** (gateOffset, gates 0-119 non visibles), puis 480 lignes **ordre direct** y=0→479 octets tels quels (ROWREV/XMIRROR désactivés — "hardware-confirmed upright"), puis pad blanc jusqu'à 600.
3. Refresh FULL : CDI (0x50) = **1 octet 0x97** (cdiBwFull) ; CCSET 0x02 ; TSSET 0x1E ; **PON + wait idle ; PSR (0x00) = `0x17 0x4D` (0x37 & 0xDF, REG clr → OTP) ENTRE PON et DRF** (PON recharge le MTP : seuls les PSR post-PON sont latchés) ; DRF 0x12 ; attente BUSY. Pas de restore CDI idle.
4. Ancien port UC8179 : le 2e octet CDI (0x07) envoyé était interprété comme DSLP (deep sleep) — parasite éliminé.

**GT911** :
5. **Self-load impossible sur cette unité** : 0x8047 lit 0x00 après toute dance conforme → OTP config vide. Le firmware stock fonctionnait… ou le touch stock était mort — jamais testé avant flash.
6. **Mode CONFIG UPDATE obligatoire pour écrire la config** : INT low au POR (RST low + INT low avant power-on rail), INT maintenu bas pendant l'écriture, relâché après. En mode normal les écritures @0x8047 sont acquittées puis ignorées.
7. **Checksum** : somme des 185 octets (0x8047..0x80FF) ≡ 0 (mod 256). Formule validée contre la table Lenovo de `GoodixFW.h` ; celle de `g911xOrig` est **corrompue dans le fichier Staars** (total 0x2C) — toujours recalculer.
8. **Pas de persistance** : la config vit en RAM uniquement tant que la rail GPIO2 tient. Architecture retenue : **upload à chaque boot** (dance → si 0x8047==0x00 → upload config 480×800 + fresh → scan). Ré-upload après tout reset RST/coupure rail.
9. **Bugs piégeux** : `pdMS_TO_TICKS(2)`/`(8)` = **0 tick** (tick FreeRTOS 10 ms) → impulsion RST glitch ; utiliser `esp_rom_delay_us`. Le POR doit se faire **RST asserté** sinon adresse/état incohérents (chip vu à 0x14 au lieu de 0x5D).
10. **Pad Home non fonctionnel pour l'instant** : bit 0x10 jamais remonté — les registres key zone (0x8093+) sont à 0 dans la config adaptée. À configurer (key map + key area) ou le pad remonte peut-être comme point normal — géométrie du pad à confirmer.

### Mesures
- Config adaptée : version 0x81, X=480 (0x01E0), Y=800 (0x0320), base `g911xOrig`, checksum recalculé 0x04.
- Points bruts observés : (404,657), (348,417), (394,662), (2,696), (316,313), (81,613), (50,11) — plage cohérente 480×800.

### Actions
- **Touch GT911 fonctionnel** (scan + points) → reste : test protocolaire 4 coins (fixer swapXY/flipX/flipY), key zone Home, intégration upload-config dans l'init du driver
- **E-Ink UC8279** séquence corrigée dans `eink_test.c` → reste : valider orientation du repère (bloc noir 200×100 haut-gauche attendu)
- Mettre à jour `hardware-specs.md` (sections UC8279 + GT911 update-mode)
- La commande `touchcfg` reste l'outil de référence pour rejouer l'upload ; à intégrer dans `gt911_begin` ensuite

---

## Session 2026-09-13 (00:55) — Orientation E-Ink + 4 coins + Home pad

### Contexte
Suite du bring-up. Trois validations manuelles demandées (orientation du repère
`einkuc 0`, touch protocolaire 4 coins, pad Home) — firmware probe inchangé
(commandes `einkucinit`/`einkuc 0`/`touchcfg` de la veille).

### Résultats

**1. Orientation E-Ink — ⚠️ scan transposé (rotation 90° CW), pas un miroir.**
Repère `einkuc 0` (fb : bloc 200×100 haut-gauche + bande horizontale centrale) :
- le bloc apparait **100 large × 200 haut en haut à DROITE**
- la bande fb horizontale apparait **VERTICALE**
→ deux mesures indépendantes (aspect inversé + verticalité) imposent
`fb(x,y) → user(799-y, x)`. Le batch FreeInk stream lignes directes « upright »,
donc notre unité diffère (rotation logicielle côté stock, ou sous-variante panel).
En suspens : ce mapping ne rend pas adressable user px 0..319 (gates 600..919
inexistantes) — soit la fenêtre visible est limitée (à vérifier : tout noir =
plein écran ou carré 480×480 ?), soit l'init registres est incomplète pour ce batch.

**2. Touch 4 coins — mapping DÉFINITIF.** Ordre des touches : HG, HD, BD, BG.
Bruts GT911 : (475,80) (476,660) (52,661) (48,58).
→ **swapXY = true, invert_y (post-swap) = true, invert_x = false.**
Vérifié : les 4 coins retombent exactement sur leurs positions physiques.

**3. Pad Home — aucun événement** (ni point, ni bit 0x10). Confirmé : c'est une
GT911 **touch key** (RE Crosspoint : le stock lit 0x814E & 0x10), key zones
0x8093+ à zéro dans notre config → pad muet jusqu'à configuration des key areas.

### Code ajouté (probe x4pro-probe)

| Ajout | Fichier | But |
|---|---|---|
| `eink_uc_show_pattern_mode(p, mode)` + `uc_stream_plane_mode()` | `eink_test.c` | modes stream 0=brut / 1=miroirX / 2=rot90CW |
| pattern 6 (repères asymétriques 4 coins + barre H) | `eink_test.c` | discriminer orientation visuellement |
| commande `einkuc2 <mode> <pattern>` | `main.c` | tester les 3 modes sans reflash |
| commande `touchkeys <k0..k3> <area> [sec]` | `main.c` | key enable (0x804D bit4) + 4 candidats X + keyArea Y, écoute live 0x814E-0x815D |

Hypothèses GT911 key (validées par struct + tables GoodixFW.h) : key[4] @idx76-79
= X des touches (1 octet, unité probable res/256), keyArea @idx80 = Y partagée,
levels 0x40/0x30, keySens 0x55/0x50, keyRestrain 0x27 ; defaults candidats bezel
bas paysage (raw X 0..48 → ~X/2, Y centre 400 → 128).

### Tests en attente (prochain flash)
1. `einkucinit` puis `einkuc2 0 6`, `einkuc2 1 6`, `einkuc2 2 6` → décrire positions
   des repères pour chaque mode ; `einkuc2 2 2` → tout noir plein écran ou carré ?
2. `touchkeys` (defaults) → appuyer sur le pad Home → le bit key 0x10 se lève ?
   Sinon itérer : `touchkeys 21 21 21 21 128`, puis autres X/area.

### Complément 01:34 — pad Home résolu (point tactile, pas une key)

Re-test `touchcfg` + appui sur le pad Home : le pad remonte un **point tactile
normal** à brut **(36,479)** (user ≈ (550,443) bezel bas), n=1 puis n=2 (zone de
contact large). Le bit key 0x10 ne se lève JAMAIS.

**Conclusion** : sur cette unité le pad Home n'utilise PAS la mécanique GT911
key (contrairement au RE Crosspoint sur une autre variante). Architecture retenue :
**zone logicielle** dans le driver (raw rx ∈ [0,70], ry ∈ [380,580] → Home,
débounce, accepter n≥1). La commande `touchkeys` reste codée mais non nécessaire.
(Le non-détecté de 00:55 = artefact pression/timing.)

Reste ouvert : orientation E-Ink (`einkuc2`, en attente de flash).

### Complément 02:01 — l'orientation N'EST PAS statique : le latch PSR change le scan

Test `einkuc2 1 6` en boot frais (rails → einkucinit) : repères en **rotation 180°**
(small square BR, horizontal rect BL, vertical rect TR, big square TL) — identique
à la session d'hier. Or le mode 1 logiciel = miroir X, et le mode brut hier avait
prouvé un hardware miroir X → composition attendue = image droite. **Contradiction
impossible avec un hardware statique.**

Reconstruction à partir des 4 sessions :
- **1er affichage après init** : scan = miroir X + offset gates 120 (repères mirés
  horizontalement, aspects conservés, frame plein écran).
- **Affichages suivants** (même boot ou boot suivant) : scan = miroir X **+ miroir Y**
  (rot180) + offset 120 conservé ; le « tout noir » 60/40 = 75/25 théorique
  (gates blanches du pad poussées en bas par le reverse de lignes).

Cause racine : après PON, le PSR était latché en **mode OTP** (`0x17 0x4D`,
REG clr → réglages MTP) — le scan bascule alors sur les registres MTP du panel,
différents de la config hôte. Le 1er affichage scannait encore avec la config
hôte (miroir X) ; tous les suivants avec le MTP (rot180). D'où l'incohérence
totale des tests en rafale d'hier soir.

**Fix codé** (`eink_test.c`, `eink_uc_show_pattern_mode`) : réécriture COMPLÈTE
des registres d'init (PSR `0x37 0x4D` REG=1 + TRES + GSST + PFS + PLL + gate-scan)
entre PON et DRF, à chaque affichage. Chaque DRF scanne donc avec la même config
hôte explicite, indépendante du MTP.

**Test de validation** (après reflash) : `einkucinit` puis `einkuc2 1 6` **deux
fois de suite** → les deux écrans doivent être identiques et droits (A petit
carré haut-gauche, B rectangle horizontal haut-droit, C vertical bas-gauche,
D gros carré bas-droit, barre horizontale centrale). Puis `einkuc 2` → tout noir
plein écran.

### Session 11:08 — stabilité acquise, transform final = miroir Y logiciel

Après reflash avec le fix « réécriture registres hôtes entre PON et DRF » :
- `einkuc 2` → **tout noir plein écran** ✓ (full-bleed, refresh complet)
- `einkuc2 1 6` **deux fois de suite** → écrans **identiques** : gros carré HG,
  vertical HD, horizontal BG, petit carré BD, ligne verticale centrée
  (= rotation 180°). Inversion des couleurs pendant le refresh puis retour
  stable — comportement GC16 normal.

**Modèle complet validé** (écriture RAM et scan ont chacun leur flip selon
l'état hôte/OTP) :

| write-state | scan-state | net hardware |
|---|---|---|
| hôte | OTP (hier 1er affichage) | miroir X |
| OTP | OTP (hier suivants) | miroir Y |
| hôte | hôte (fix actuel) | miroir Y |

→ Avec la config hôte stable : **hardware = miroir Y** ⇒ le transform logiciel
qui donne l'image droite = **miroir Y** (ordre des lignes inversé, octets tels
quels). Ajouts codés : mode 3 (miroir Y) dans `uc_stream_plane_mode` + écriture
PSR `0x37` **avant le stream** (verrouille le chemin d'écriture W_host quelle
que soit l'historique).

**Validation en attente** : `einkuc2 3 6` deux fois de suite → image droite
identique aux deux écrans (A petit carré HG, B horizontal HD, C vertical BG,
D gros carré BD, barre horizontale centrale).


### Session 12:18 — ✅ ORIENTATION DÉFINITIVE : scan MTP (PSR 0x17), mode 0 = image droite

Chaîne d'événements de la matinée (triage complet, à ne pas ré-ouvrir) :

1. **11:21** — crash avec `einkuc2 3 6` : écran gris, task_wdt sur IDLE0.
   Cause réelle n°1 (corrigée) : `uc_wait_idle`/`wait_busy` utilisaient
   `vTaskDelay(pdMS_TO_TICKS(2))` et `(5)` = **0 tick** (tick FreeRTOS 10 ms)
   → IDLE0 affamé pendant les longues attentes BUSY. Corrigé → délais ≥ 10 ms.
   Fausse piste : l'écriture PSR avant le stream (retirée puis jugée non
   impliquée — c'est le DRF qui compte, pas le stream).
2. **11:39** — watchdog guéri mais **timeout BUSY DRF systématique** (même le
   mode 1 validé à 11:08). Timeout porté 8→20 s + récupération POF + messages
   power cycle.
3. **11:58** — identique après **power cycle complet** (débranché 30 s,
   reboot froid) → exclut un état résiduel. Relecture du driver stock
   (`_ext/Uc8279X4Driver.cpp`) : **seul écart** = stock écrit PSR `0x37 & 0xDF`
   = **`0x17` (REG=0 → scan avec registres MTP)** entre PON et DRF ; nous
   écrivions `0x37` (REG=1 → registres hôtes). Bascule sur 0x17.
4. **12:11** — `einkuc2 0 6` : refresh **termine proprement (~2-4 s)**.
   Image = miroir X de la description attendue → nouvelle hypothèse de
   transform à recaler.
5. **12:18** — recalage par les coordonnées réelles du pattern 6 (fb dessiné
   en **natif 800×480 paysage** ; la liseuse est tenue en portrait 480×800) :
   ce que l'utilisateur voit en mode 0 = **rotation 90° CW pure, sans miroir**
   = exactement le comportement du firmware stock. Mode 1 vérifié (miroir X
   logiciel, obsolète). **3 affichages strictement identiques** → scan stable.

**Verdict final** :
- DRF = toujours scan MTP (PSR `0x17` post-PON). ⚠️ PSR `0x37` (REG=1) au
  DRF = full GC qui ne termine JAMAIS sur ce UC8279 v0.2 (BUSY LOW > 20 s,
  écran gris, POF sans effet, reproduit du cold boot). Verrou codé +
  documenté dans `hardware-specs.md`.
- fb en **natif paysage 800×480**, stream brut (gates 0-119 blancs, 480
  lignes fb, pad blanc) ; aucun transform logiciel.
- Le fb portrait rapporté par le pattern 6 hier (miroir X / miroir Y) était
  un artefact de la config REG=1 instable — tout l'arbre de décision
  miroir est obsolète.
- **Bring-up hardware Phase 5 terminé.** Reste : frontlight couleurs non
  testées en détail (P2, hors scope TOTP).


### Session 12:38 — ✅ Orientation doublement confirmée (test flèche)

`einkuc2 0 7` (nouveau pattern 7 : flèche native +x + carré natif TL) :
**flèche noire pointant vers le BAS + carré en haut-DROITE** = exactement la
rotation 90° CW attendue. Reproduit le verdict 12:18 avec un repère non
symétrique. **Bring-up hardware + orientation : clos.**


### Session 15:39 — 🔴 Boot loop factory (TG0WDT, Saved PC ROM) — DIAG EN COURS

Flash `x4pro_factory` via flash_mgr → boot loop :
`rst:0x7 (TG0WDT_SYS_RST)`, `Saved PC:0x400454d5` (ROM), plante
pendant la phase `load:` du ROM loader → le second-stage n'est jamais
lancé. Zéro log bootloader (console sur UART0, non connectée sur le
dock USB natif).

Hypothèses et faits :
- Bootloader identique entre les deux variantes (cmp OK) → l'écart
  est dans ce qui est chargé après, ou dans le flash mode.
- **Seul DIO est prouvé sur cette unité** (x4pro-probe, qui bootait).
  QIO imposé dans les sdkconfig.defaults — jamais validé jusqu'ici.
- Hook bootloader exclu comme cause directe (attente max ~5,4 s <
  WDT 9 s).

Correctifs appliqués (sdkconfig.defaults racine + factory) :
DIO, `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`,
`CONFIG_BOOTLOADER_LOG_LEVEL_DEBUG=y`, LOG DEBUG (max+default).
⚠️ sdkconfig gelé → clean_board obligatoire avant rebuild.

Suite : clean → rebuild → reflash factory → relever les logs.


#### Résolution 16:45 — ✅ 3 causes empilées identifiées et corrigées

1. **esptool patche l'en-tête flash mode au flash** : `flash_mgr` passait
   `--flash-mode qio` (stale `boards/x4pro/flash_params.json`) → le
   bootloader DIO fraîchement buildé repartait en QIO sur le flash chip.
   → `flash_params.json` passé en `dio`. Le build était DIO depuis le
   début (byte d'en-tête 0x02, vérifié contre le bootloader probe de
   référence — mapping esptool : qio=0, dio=2).
2. **sdkconfig gelé** (factory 11:36, app 11:41) : les defaults console
   USB-Serial/JTAG + LOG DEBUG ignorés par olddefconfig. Le clean
   interactif ne supprimait PAS le sdkconfig → clean_variant corrigé
   dans `common.py` (+ template blueprint). Effet de bord : le build 16:20
   a enfin réellement appliqué la console USB → cassé la compile de
   `main.c` (REPL UART inconditionnelle) → REPL rendue conditionnelle
   (`esp_console_new_repl_usb_serial_jtag` vs `_uart`).
3. **Flash mode mapping** : qio=0x00 / dio=0x02 (esptool) — le "QIO" lu
   dans l'en-tête au premier dump était en fait DIO (table inversée).

**État :** builds factory+app verts, binaires DIO, flash map meta=dio,
sdkconfigs régénérés (console USB-Serial/JTAG + DEBUG). Reste : reflash +
validation boot sur device.


### Session 17:00 — 🟠 Première validation factory sur device (4 retours)

Boot factory OK (DIO + logs USB). Découverte du flux complet :
- **"crash" power = pas un crash** : `RTC_SW_CPU_RST` = esp_restart() après
  sélection "Boot app0" → otadata restauré → **l'app démarre** (PSRAM 8 Mo
  OK). Le rescue ADR-007 fonctionne de bout en bout.
- **Paysage au lieu de portrait** : le probe avait conclu mode 0 (stream
  brut) = orientation correcte — le fb mémoire est paysage natif, monté
  portrait. La factory dessinait en coords fb → contenu tourné. Fix gfx :
  UI en coords portrait 480x800, put_pixel transpose fb_x = uy,
  fb_y = 479 - ux (miroir X INCLUS — fb(0,0) s'affiche haut-DROITE,
  mesure flèche 12:38 ; sans miroir le texte serait inversé).
- **Touch mort** : code fidèle à la séquence validée, MAIS les logs
  factory étaient compilés hors (ENABLE_FACTORY_DEBUG_LOG=OFF) → aucune
  visibilité. Passé ON (diag). Prochain flash dira "touch: OK/ABSENT".
- **Refresh full GC + inversion** à chaque navigation : LUT MTP (REG=0)
  = waveform GC16 obligatoire. Partiel = host LUT (REG=1) — figeait dans
  le probe. À expérimenter dans le probe AVANT de porter (méthodo).

Fix appliqués : gfx transpose portrait, touch mapping portrait
(pt.x=raw_x, pt.y=raw_y), logs factory ON. Build factory vert,
flash map meta=dio. Test suivant : flash variant x4pro_factory.

### Session 18:20 — 🔧 Splash statique app (Phase 8c slice 1)

Demande : page statique pour savoir où l'on est en quittant la factory.
- Composant `boards/x4pro/app/` : eink.c + font8x16.c copiés de la
  factory (éprouvés), splash.c avec gfx minimale et la transpose portrait
  validée. Rails : periph ON, touch/SD OFF (BSP complet en 8.4).
  Après refresh : POF (image persistante à conso nulle, principe F-19).
- `board_config.cmake` : EXTRA_COMPONENT_DIRS (avant project()).
- **Piège weak symbol** : hook faible dans main.c = jamais tiré de
  l'archive statique (le faible satisfait la référence, nm montrait W).
  Corrigé : déclaration externe seule, implémentation forte obligatoire
  (link error explicite sinon). nm montre T. Build app vert.

### Session 21:00 — ✅ Splash validé sur device + police x2

- Splash app vu sur hardware (portrait OK, POF OK, REPL OK après).
- Appui long power "changement d'écran" = factory → sélection "Boot app0"
  → otadata restauré → reboot sur l'app + splash. **Comportement nominal**
  (RTC_SW_CPU_RST = esp_restart volontaire, pas un crash).
- Police x2 (16x32) factory + splash (demande utilisateur : 8x16 trop
  petite). Layout menu ajusté (30 car. max/ligne, item 60 px, footer
  raccourci). Builds factory+app verts.

### Session 21:45 — 🔧 Deep sleep : réveil normal + réveil vers factory

Demande : support du deep sleep pour valider les deux flux de réveil.
- `power_mgr` : `power_mgr_init()` log la cause de réveil (EXT1 = bouton
  Power / cold boot) et configure le wake EXT1 sur GPIO3 (RTC IO, ANY_LOW).
- `main.c` : init power_mgr en premier (log wake toujours présent) ;
  réveil par Power = splash sauté (contrat ADR-009, straight-to-app) ;
  commande REPL `sleep` = écran de veille puis `power_mgr_shutdown()`.
- `splash.c` : `board_sleep_screen_show()` — écran "EN VEILLE / Power =
  reveil", POF après refresh. Combo factory volontairement absent
  (DECISIONS §16). Un échec e-ink ne bloque PAS la mise en veille.
- Flux réveil → factory : géré par le hook bootloader (GPIO7 maintenu) —
  l'app ne voit jamais ce cas, test = Power+Right au réveil.
- Écueils : composant `esp_log` renommé `log` en IDF 5.5 ; ICE transitoire
  du GCC xtensa sur esp_lcd_panel_rgb.c (segfault ira — disparaît au
  retry ninja) ; déclaration board_sleep_screen_show après cmd_sleep =
  implicit declaration (-Werror).
- Build app vert (installer/x4pro_app frais). À tester : flash x4pro_app,
  `sleep` en REPL, réveil Power (log EXT1, pas de splash), réveil
  Power+Right (boot factory).

### Session 21:45 — 🔧 Retour test 21:21 : diagnostic + fix boutons/layout

Analyse du log utilisateur (fragment) :
- Le "deepsleep screen illisible / fonts bizarres" (point 3) = le SPLASH
  APP vu PENDANT le full refresh GC16 (phase d'inversion 3-4 s) après
  "Boot app0" depuis la factory. L'app n'avait AUCUN handler bouton :
  power ne faisait rien (points 4-5 = app running, pas un vrai deep sleep).
- Root cause points 4-5 : le deep sleep n'etait accessible que par la
  commande REPL `sleep` — le bouton power n'etait pas gere dans l'app.

Fix appliques (build factory + app verts 21:36) :
- Factory : "Active: factory" + "Default: app0" sur 2 lignes (largeur x2),
  layout re-espacement (menu y=196), indicateur touch a l'ecran "T:OK/KO"
  (diag sans serial — s_touch_ok file-static).
- App : tache power_button_task (stack 4096, prio 5) : power long >= 1.5 s
  -> sleep screen + deep sleep ; power long + Right maintenu -> esp_restart
  pendant le combo -> hook bootloader -> factory. Contrat ADR-009 complet.
- power_mgr_shutdown : re-arm EXT1 juste avant esp_deep_sleep_start
  (idempotent, garantit la source de reveil).
- Splash : footer commandes mis a jour (sleep ajoute).

A tester : flash x4pro_factory puis x4pro_app. Menu factory lisible +
T:OK/KO. App : power long = ecran "EN VEILLE" puis veille ; power court
au reveil = app (log EXT1, splash saute par contrat) ; power long + right
= factory. Touch : rapporter T:OK ou T:KO + log "touch: OK/ABSENT".

### Session 22:05 — 🔧 Refresh rapide DU (RE du stock FW freeink-sdk)

Symptome : chaque navigation = "2 refresh" (flash inversion puis image).
Diagnostic : UN SEUL draw_menu + UN SEUL DRF par appui (boutons
edge-detect). La "double image" = la waveform GC16 MTP pleine : phase de
clear (inversion ecran entier) puis dessin de la nouvelle image. Inherent
au full refresh.

Reference extraite : freeink-sdk Uc8279X4Driver.cpp (RE du stock FW,
clone dans tmp_ref/freeink/). Decouverte cle : le fast DU du stock N'UTILISE
PAS de LUT hote (REG=1 — c'est ce qui figeait le UC8279 dans le probe) :
c'est la waveform OTP selectionnee par TSSET 0x5A + CDI 0xD7 + fenetre
PTL PLEINE (PTIN sans PTL = DU qui scanne sans developper, mesure terrain
443 ms sans image). OLD plane = frame precedente (diff differentiel),
resync DTM1 apres chaque refresh.

Implementation (factory/main/eink.c) :
- eink_display_fb_fast() : sequence byte-exact du stock (CDI 0xD7,
  CCSET 0x02, TSSET 0x5A, PFS 0x20, gate scan 0x02, PON idempotent,
  PTIN+PTL pleine avec offset +120, PSR 0x17 entre PON et DRF, DRF,
  wait, PTOUT, resync DTM1=fb). Buffer prev 48 Ko en PSRAM.
- Politique : full GC si pas de prev (1er affichage/boot) ou toutes les
  EINK_FAST_BUDGET (10) fasts ; compteur rearme par chaque full.
- PON idempotent (s_screen_on), eink_power_off met le flag a false.
- gfx_flush_fast() ; navigation menu (Left/Right) = fast, boot/status =
  full GC. Build factory vert 22:05.
A tester : navigation menu = ~0,5-1 s sans flash ; au bout de 10 navs
un full GC (flash) purge les ghosts.

### Session 22:15 — 🔧 Phase 8b : bench calibration Argon2id (X4 Pro)

Contexte : S3R8 = 320 Ko DRAM interne -> le work area Argon2id m=8 MiB
(ADR-004) n'existe QUE sur la PSRAM octale. Penalite de vitesse inconnue
-> calibration hardware avant d'implementer secret_store (ROADMAP 8.2).

Choix lib : Monocypher 4.0.2 (crypto_argon2, CRYPTO_ARGON2_ID) — fichier
unique, audite, Argon2id v1.3. Vendore dans test_apps/x4pro-argon2-bench/.
Bench (build vert 22:20, build_bench.py reutilise l'env common.py) :
 0. heap/PSRAM libres + bande passante memcpy PSRAM (contexte).
 1. VECTEUR CROISE pin="123456" salt=00..0f m=2 MiB t=1 p=1, cle 32 o.
    Attendu cote PC (argon2-cffi v19, Type.ID, memory_cost=2048) :
    898e76d4bcda52614ecfd2961847493e80246c0ea7c6c33b227e819d03a40a65
    Le bench imprime la cle cote ESP32 ; comparaison stricte des 64 hex.
 2. Matrice m {1,2,4,8} MiB x t {1,2,4,8}, p=1, mediane de 3 runs.
 3. Reference DRAM interne 256 KiB t=4 (quantifie la penalite PSRAM).

A faire par l'utilisateur : flash (idf.py -p COMx flash depuis un terminal
IDF 5.5.5), monitor, rapporter : vecteur (match ?) + matrice + bande
passante. Cible : 0,5-1,0 s par essai de deverrouillage PIN.

### Session 23:00 — 🔧 Retour test 22:39 : ghosting DU, spam otadata, wake invisible, diag touch

Retours utilisateur (builds factory 22:17 / app 22:18, testés 22:17-22:39) :
1. Nav DU mieux mais ghosting persistant : la barre de selection deselectionnee
   passe noir -> gris -> blanc sans jamais revenir a l'etat initial ; chaque
   ligne finit avec une couleur differente.
2. Touch home pad toujours sans effet cote factory.
3. Power long dans l'app = succession de refresh puis page avec traces du
   splash precedent (ghost du full GC), font jugee illisible.
4. Power court en deep sleep "ne fait rien" : le log montre pourtant le
   reveil (EXT1) — il etait INVISIBLE par contrat (splash saute au wake).
5. Power+Right en deep sleep -> bascule factory : CONFIRME FONCTIONNEL
   (log bootloader hook complet).

Analyse du log factory (capture complete cette fois) :
- Boot 22:17:15 : probe I2C 0x14 NACK a t=1196 (attendu), lecture 0x5D
  REUSSIE (pas de 2e NACK) -> le GT911 repond. La suite (0x8047, upload
  config) ne loggait rien -> statut inconnu d'ou l'indicateur T:KO/T:OK.
- otadata valide au boot ("Only otadata[0] is valid", t=1906) puis devenu
  "ota data invalid" ~31 s plus tard, sans cause identifiable dans le code
  (hooks.c verifie : erase otadata UNIQUEMENT si Right maintenu au boot ;
  l'app ne touche pas a otadata). Non resolu — diagnostics ajoutes.
- action_sd_flash VERIFIE : SD montee + fichier ouvert + taille validee
  AVANT tout erase -> pas de bug brickant (crainte initiale infondee).

Fix appliques (build factory + app verts 23:00) :
- Ghosting : EINK_FAST_BUDGET 10 -> 4 (factory/main/eink.c). La barre de
  selection = grosse zone noire mobile = cas pire DU.
- Spam otadata : label "Active:" cale UNE FOIS au boot
  (cache_active_ota_label) — supprime l'appel esp_ota_get_boot_partition
  a chaque redraw (2 lignes de log esp_ota_ops par navigation) + la relente
  flash a chaque redraw.
- boot_partition : relecture otadata apres esp_ota_set_boot_partition +
  ESP_LOGI (diagnostic de la corruption mid-session).
- Touch : ESP_LOGI/ESP_LOGE always-on a chaque etape (probe addr, cfg
  version, upload resultat) — la prochaine session donnera l'etape exacte
  de l'echec au lieu de deviner.
- App : splash desormais affiche AUSSI au wake (interim jusqu'a l'ecran
  UNLOCK 8.4) — le reveil power est desormais visible.

Non traites (reportes) : traces/ghost du splash -> sleep screen (probablement
ameliore par l'unification BSP 8c) ; police sleep screen a revalider apres
fix font ; bench Argon2id toujours en attente de flash (vecteur croise +
matrice).

A tester : flash x4pro_factory + x4pro_app. Factory : nav = ghosting reduit
(full GC tous les 4 fasts) ; log boot = "touch probe OK, addr 0x5D" +
"config hote uploadee" ou l'etape en erreur ; "active partition: factory".
Boot app0 -> log "boot -> 'app0' (otadata relu: app0)". App : power court
au reveil = splash visible. Power+Right = factory (deja OK).

### Session 23:05 — 🔧 Spec Power = Cancel dans la factory

Demande utilisateur : Power (GPIO3) n'est PAS un OK — c'est un Cancel qui
repart sur la partition par defaut. Seul le pad Home (touch) valide.
- dispatch BTN_3 -> action_cancel() : log + status + eink_power_off +
  esp_restart (otadata restaure au boot -> reboot = partition par defaut).
- Pad Home -> execute_selected_action() direct (ne passe plus par BTN_3).
- Footer : "L/R=nav Home=OK Power=Cancel" (25 car. x2 = 400 px, tient dans
  440 px ; l'ancien "Home/Power=OK" etait trompeur).
- Header + commentaire boucle mis a jour. CONSEQUENCE : touch KO = plus
  aucune validation possible dans la recovery -> le diag touch devient
  critique.
Build factory vert 23:06. A tester : nav L/R, Home valide, Power cancel
(reboot sur app par defaut).

### Note 2026-09-16 — combo Power+Right invalide (ne pas se fier aux entrees ci-dessus)

Les sessions ci-dessus (toutes datees 2026-09-13) rapportent le combo
Power+Right comme "CONFIRME FONCTIONNEL" en deep sleep. Reteste sur
hardware le 2026-09-16 : Power+Right tenus ensemble depuis la veille ne
reveille JAMAIS le device (aucun log, aucune reaction), quelle que soit la
duree de maintien — contrairement a Power seul qui reveille
systematiquement. Root cause non identifiee cote firmware (probleme en
amont du hook bootloader). Combo abandonne, remplace par Power seul avec
mesure de duree (< 10 s = veille/reveil normal, >= 10 s = factory) —
cf. ADR-009 (amendement 2026-09-16) dans docs/ROADMAP.md. Les entrees
historiques ci-dessus sont conservees telles quelles (log d'session), mais
ne decrivent plus le comportement actuel.
