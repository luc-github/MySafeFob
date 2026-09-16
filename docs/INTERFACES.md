# INTERFACES.md — MySafeFob (MSF) : spécification des interfaces internes (Phase 7)

> **Statut** : v1.0 — proposé le 2026-09-13, en validation
> **Portée** : tout ce qui est spécifié ici est figé avant le début du
> développement (Phase 8). Toute modification = revue formelle (même règle que
> FEATURES.md). Références : `FEATURES.md` (features), `hardware-specs.md`
> (hardware/drivers), `ROADMAP.md` (ADR-001 sync Wi-Fi, ADR-002 blob chiffré).

---

## 1. Conteneur chiffré des secrets (F-04 / ADR-002)

Stocké dans la partition `secrets` (raw, pas de filesystem). Tout ce qui est
sensible (TOTP, mots de passe, recovery codes, settings) vit dans CE conteneur
unique. **La flash ne contient jamais de secret en clair** (flash encryption
non activée v1 — la barrière est le chiffrement applicatif).

### 1.1 Enveloppe chiffrée (layout binaire, little-endian)

| Offset | Taille | Champ | Notes |
|--------|--------|-------|-------|
| 0      | 4      | magic `MSF1` | MySafeFob Blob, format v1 |
| 4      | 1      | format_version | = 1 |
| 5      | 1      | kdf_algo | 2 = Argon2id (ADR-004 ; 1 = PBKDF2 réservé, jamais écrit) |
| 6      | 1      | kdf_t | passes Argon2id (cible 4) |
| 7      | 1      | kdf_p | parallelisme (1) |
| 8      | 2      | kdf_m_mib | mémoire en MiB (cible 8 — PSRAM), uint16 LE |
| 10     | 16     | salt | aléatoire (`esp_random()`), généré une fois à la première mise en service, **stocké en clair** |
| 26     | 12     | nonce | aléatoire à chaque ré-écriture du blob |
| 38     | N      | ciphertext | AES-256-GCM du clair interne (§1.2) |
| 38+N   | 16     | tag | tag d'authentification GCM |

- **Clé** : `Argon2id(PIN_utf8, salt, m=8 MiB, t=4, p=1)` — portage
  libsodium/monocypher, paramètres recalibrés à l'implémentation pour
  **~0,5-1 s/essai** sur le S3 (barrière offline memory-hard, ADR-004 :
  PBKDF2-SHA256, non memory-hard, tombe en ~1-2 jours sur GPU malgré un
  coût « 250 ms » nominal).
- **État non initialisé** : partition effacée (0xFF) → le device propose la
  création du PIN au premier boot.
- **Oubli du PIN = perte définitive des données** (pas de porte dérobée v1 ;
  le backup SD chiffré F-06b reste restaurable avec le bon PIN). Message
  d'avertissement affiché à la création du PIN.

### 1.2 Clair interne (sérialisation TLV maison, versionnée)

Format TLV : `tag (1 octet) | length (2 octets LE) | payload`. Strings = UTF-8
sans NUL (longueur explicite). Tous les enregistrements portent un `id`
uint8 unique, jamais réutilisé après suppression (évite les collisions d'UI).

```
[0x01] HEADER    : magic "MSFS" (4 octets) + version (1 octet)
[0x10] TOTP rec  : id | label ≤48 | secret binaire ≤64 (base32 DÉCODÉ)
                   | digits (6/8) | period_s (30/60) | created_epoch u32
[0x11] PWD rec   : id | label ≤48 | username ≤64 | password ≤128 | notes ≤256
[0x12] RCV rec   : id | label ≤48 | nb_codes | codes (nb × ≤16)
                   | used_mask u16 (bit i = code i consommé)
[0x20] SETTINGS  : lock_timeout_s u16 | frontlight_auto_off_s u16
                   | last_sync_epoch u32 | last_sync_drift_ppm i32
                   | utc_offset_min i16 | sync_warn_days u8 (défaut 45)
```

**Budget** : ciphertext ≤ 64 Ko (~400–600 entrées selon taille des notes).
La partition fait 256 Ko — marge pour grossir sans changer le schéma.

### 1.3 Anti-brute-force PIN

- **Online** (sur le device) : N échecs → back-off exponentiel (10 s, 30 s,
  2 min, 10 min, 1 h). Compteur persistant en clair dans le secteur dédié de
  la partition `secrets` (en dehors du blob). Limitation acceptée : un effacement
  flash remet le compteur à zéro MAIS efface aussi les données — l'attaquant
  ne gagne rien. (eFuse v2 éventuel, Nice-to-Have.)
- **Offline** (extraction flash + attaque KDF sur PC) : barrière =
  Argon2id memory-hard (m=8 MiB) × 10⁶ combinaisons PIN 6 chiffres —
  résistant GPU/ASIC, contrairement à PBKDF2-SHA256 (ADR-004).

## 2. Partitionnement flash (16 Mo)

```
# partitions.csv (cible ESP-IDF 5.5.x) — ADR-007 : factory + backup otadata @0xB000
# CONFIG_PARTITION_TABLE_OFFSET=0xC000 obligatoire (bootloader hooks S3)
# app1 retire (ADR-011, 2026-09-15) : espace libere donne a secrets.
nvs,       data, nvs,      0xD000,  0x3000,
otadata,   data, ota,     0x10000,  0x2000,
factory,    app, factory,0x20000, 0x100000,
app0,       app, ota_0, 0x120000, 0x200000,
secrets,   data, spiffs,0x320000, 0x240000,
coredump,  data, coredump,0x560000, 0x10000,
```

| Partition | Role |
|-----------|------|
| `nvs` | minimal — **pas de credentials Wi-Fi** (`CONFIG_ESP_WIFI_NVS_ENABLED=n`). Rien de secret. |
| `otadata` | selection du slot de boot (mecanisme standard esp_ota) |
| `factory` | **recovery sans PC** (ADR-007) : app minimale (UI e-ink sobre) qui flashe depuis SD ou serial vers app0 et restaure l'otadata depuis la sauvegarde @0xB000 |
| `app0` | **seul slot app** (ADR-011 : pas d'OTA reseau donc pas besoin du rollback A/B esp_ota) — ecriture via SD/serial uniquement (ADR-003), toujours depuis la factory (jamais d'auto-update en cours d'execution) |
| `secrets` | conteneur chiffre (section 1) + compteur anti-brute-force en clair en tete de partition (secteur 0) — 2,25 Mo, agrandi de l'espace libere par app1 (ADR-011) |
| `coredump` | debug Phase 9, peut etre retire en release |

Attention : backup otadata @ **0xB000** (gap entre bootloader et table de partitions),
magic `0xAA55AA55` — constantes partagees hooks.c (bootloader) / factory app.

## 3. Protocole de mise à jour firmware par SD (F-06)

Pas de OTA réseau (ADR-003 : **SD ou serial UART uniquement**, jamais par le
réseau). **Depuis ADR-011 (app1 retiré, un seul slot app) : la mise à jour
n'est plus une fonctionnalité in-app, elle passe systématiquement par la
factory** (`SD -> app0`, déjà implémentée et validée — `docs/FACTORY.md`
§5.3). Pas de rollback A/B esp_ota : la résilience vient de la factory
elle-même (partition indépendante, toujours accessible en tenant Power
≥ 10 s, cf. ADR-009 amendé 2026-09-16), pas d'un second slot app.

**Fichiers attendus à la racine de la SD** (convention factory) :
```
msf-fw.bin              — image app (binaire brut de partition, non chiffré)
```
Renommé `msf-fw.ok` (succès) ou `msf-fw.bad` (échec) après tentative —
cf. `docs/FACTORY.md` §5.3.

**Séquence** (dans la factory, pas dans l'app) :
1. Utilisateur boote en factory (Power tenu ≥ 10 s, ADR-009) → SD montée →
   vérifie présence de `msf-fw.bin`.
2. `esp_ota_begin(app0)` → `esp_ota_write` (streaming depuis la SD, taille
   vérifiée ≤ taille partition) → `esp_ota_end` — **`esp_ota_end` valide déjà
   le header/checksum de l'image** : un fichier tronqué ou corrompu est
   rejeté avant de devenir bootable.
3. `esp_ota_set_boot_partition(app0)` + reboot sur le firmware fraîchement
   écrit.
4. **Pas de self-check applicatif ni de rollback automatique** : si le
   nouveau firmware ne boote pas ou est logiquement cassé, l'utilisateur
   revient en factory (Power tenu ≥ 10 s) et reflashe — récupération manuelle,
   pas automatique, contre-partie assumée de l'abandon du 2ᵉ slot (ADR-011).

**Menace résiduelle assumée (ADR-003)** : sans secure boot avant la
prerelease finale, quiconque a le device + une SD peut flasher un firmware
arbitraire. Accepté : le modèle de menace v1 suppose un attaquant *sans*
accès physique prolongé (le PIN protège les données, pas le firmware).

**Import/export des données (F-06b / ADR-005)** : fichier
`/x4pro-export-YYYY-MM-DD.enc` — la SD ne voit jamais le clair et ne connaît
pas le PIN du device :

```
magic "MSFEX1" (8B, zero-padded) | version u16 | timestamp u64
| entry_count u32 | salt 16B | nonce 12B
| AES-256-GCM( Argon2id(passphrase_export, salt, m=8MiB, t=4, p=1), clair interne )
| tag GCM 16B
```

- **Passphrase d'export dédiée** (mode transfer, défaut) — la SD volée seule
  ne vaut rien. Le backup « même clé que le device » (restaurable sur un
  device neuf sans passphrase supplémentaire) reste possible en option.
- Écriture atomique (fichier temporaire + rename), **jamais d'écrasement**
  (versioning par date dans le nom).
- Import : parse + vérification magic/version/**tag GCM** AVANT de toucher au
  keystore courant ; tailles bornées, pas de chaîne non terminée ; confirmation
  explicite + avertissement d'écrasement.

## 4. APIs internes (contrats C)

Chaque module = composant ESP-IDF sous `src/components/`. Les headers ci-dessous
sont les contrats ; l'implémentation est Phase 8.

### 4.1 `totp_engine` (déjà validé Phase 4 — portage tel quel)

```c
esp_err_t totp_engine_init(void);
esp_err_t totp_set_secret(const uint8_t *b32, size_t len);   /* ou binaire décodé */
esp_err_t totp_generate(uint64_t epoch_s, char *out_code,    /* 6/8 chiffres */
                        uint8_t digits, uint32_t period_s);
```

### 4.2 `secret_store`

```c
esp_err_t store_unlock(const char *pin);        /* dérive la clé, ouvre le blob */
void      store_lock(void);                     /* efface la clé de la RAM */
bool      store_is_unlocked(void);
esp_err_t store_totp_add/update/delete/list(...);    /* filtre type=TOTP */
esp_err_t store_pwd_add/update/delete/get(...);
esp_err_t store_rcv_mark_used(id, code_index);
esp_err_t store_export_to_sd(void);             /* F-06b */
esp_err_t store_import_from_sd(void);           /* F-06b, confirmation explicite */
/* Persistance : toute mutation ré-écrit le blob entier (N petit, écritures rares,
   économie d'endurance flash : pas de log incrémental en v1). */
```

### 4.3 `time_svc` (ADR-001 + terminal de confiance ADR-006)

```c
esp_err_t time_sync_ble_cts(void);   /* GATT client NimBLE, CTS 0x1805/0x2A2B,
                                        scan→connect→read→disconnect, sans pairing */
esp_err_t time_sync_serial(void);    /* heure poussée par le PC sur l'UART
                                        (mécanisme validé au probe, cmd 'st') */
esp_err_t time_sync_wifi(const char *ssid, const char *pwd);  /* jamais persisté */
esp_err_t time_set_manual(int64_t epoch_s);                   /* saisie UI HH:MM */
int64_t   time_now(void);                                     /* RTC + drift */
void      time_health(int32_t *drift_ppm, uint32_t *age_sync_s);
/* Invariants :
   - les 3 canaux radio/serial ne transportent QUE l'heure, jamais de donnée
     du keystore (ADR-006) ;
   - Wi-Fi : esp_wifi_set_config(..., WIFI_STORAGE_RAM) (jamais FLASH),
     memset des buffers credentials après esp_wifi_stop(), puis
     esp_wifi_deinit() pour éteindre le PHY ;
   - aucun credential ne transite par la NVS (CONFIG_ESP_WIFI_NVS_ENABLED=n). */
```

### 4.4 `ui_mgr` + `input`

```c
void ui_show(screen_t s);     /* écran créé pour son contexte, l'ancien détruit */
/* Écrans : UNLOCK, HOME(menu), TOTP_LIST, TOTP_CODE, PWD_LIST, PWD_VIEW,
   RCV_LIST, RCV_CODE, ADD_EDIT_ENTRY, TIME_SYNC, SETTINGS, UPDATE_SD, STATUS */
/* input fournit : coordonnées tactiles mappées (swapXY+invert_y, cf. hw-spec),
   3 boutons, zone Home (point brut 36,479), long-press. */
```

### 4.5 `pwr`

```c
uint8_t pwr_battery_percent(void);    /* CW2017, formule validée */
void    pwr_frontlight_set(bool on, uint8_t level);  /* warm/cool, auto-off */
```

### 4.6 `update_svc` — voir §3 (séquence exacte).

## 5. Invariants globaux (non négociables)

1. Aucune tâche réseau active au boot ; la radio ne s'allume que via `time_sync_wifi`.
2. Aucun secret en clair sur la flash, la SD, la NVS ou les logs UART
   (logs désactivés en release ; `menuconfig` : pas de printf de secrets).
3. Tout format persistant est versionné (magic + version) dès le premier octet.
4. L'UI ne garde jamais deux écrans vivants simultanément (mémoire).
5. Le blob n'est déchiffré qu'en RAM, et `store_lock()` efface la clé et le
   cache clair (memset + `heap_caps_check_integrity` en debug).
6. **Toute aléa (sel, nonce, IDs) vient de `esp_random()`** (RNG matériel
   bruit RF/jitter) — jamais `rand()`/`random()`.

## 6. Points ouverts avant Phase 8

- [ ] **Paramètres Argon2id finaux** : mesurer m/t/p réels sur le S3
      (cible 0,5-1 s/essai) et fixer les valeurs dans le code.
- [ ] **Longueur PIN** : 6 fixé (FEATURES F-03). Saisie UI à spécifier au
      moment de l'écran UNLOCK (pavé numérique plein écran, layout anti-traces).
- [ ] **Frontlight trigger** (F-16) : allumage manuel confirmé ; le widget UI
      exact est décidé pendant l'implémentation de la barre d'état.
