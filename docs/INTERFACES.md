# INTERFACES.md — MySafeFob (MSF): internal interfaces specification (Phase 7)

> **Status**: v1.0 — proposed 2026-09-13, under review
> **Scope**: everything specified here is frozen before development begins
> (Phase 8). Any change = formal review (same rule as
> FEATURES.md). References: `FEATURES.md` (features), `hardware-specs.md`
> (hardware/drivers), `ROADMAP.md` (ADR-001 Wi-Fi sync, ADR-002 encrypted blob).

---

## 1. Encrypted secrets container (F-04 / ADR-002)

Stored in the `secrets` partition (raw, no filesystem). Everything that is
sensitive (TOTP, passwords, recovery codes, settings) lives in THIS single
container. **The flash never contains a secret in plaintext** (flash encryption
not enabled in v1 — the barrier is application-level encryption).

### 1.1 Encrypted envelope (binary layout, little-endian)

| Offset | Size | Field | Notes |
|--------|--------|-------|-------|
| 0      | 4      | magic `MSF1` | MySafeFob Blob, format v1 |
| 4      | 1      | format_version | = 1 |
| 5      | 1      | kdf_algo | 2 = Argon2id (ADR-004; 1 = PBKDF2 reserved, never written) |
| 6      | 1      | kdf_t | Argon2id passes (target 4) |
| 7      | 1      | kdf_p | parallelism (1) |
| 8      | 2      | kdf_m_mib | memory in MiB (target 8 — PSRAM), uint16 LE |
| 10     | 16     | salt | random (`esp_random()`), generated once at first commissioning, **stored in plaintext** |
| 26     | 12     | nonce | random on every blob rewrite |
| 38     | N      | ciphertext | AES-256-GCM of the internal plaintext (§1.2) |
| 38+N   | 16     | tag | GCM authentication tag |

- **Key**: `Argon2id(PIN_utf8, salt, m=8 MiB, t=4, p=1)` — port of
  libsodium/monocypher, parameters recalibrated at implementation time for
  **~0.5-1 s/attempt** on the S3 (offline memory-hard barrier, ADR-004:
  PBKDF2-SHA256, not memory-hard, falls in ~1-2 days on GPU despite a
  nominal "250 ms" cost).
- **Uninitialized state**: erased partition (0xFF) → the device offers
  PIN creation on first boot.
- **Forgetting the PIN = permanent data loss** (no backdoor in v1;
  the encrypted SD backup F-06b remains restorable with the correct PIN). Warning
  message displayed at PIN creation.

### 1.2 Internal plaintext (custom TLV serialization, versioned)

TLV format: `tag (1 byte) | length (2 bytes LE) | payload`. Strings = UTF-8
without NUL (explicit length). All records carry a unique
uint8 `id`, never reused after deletion (avoids UI collisions).

```
[0x01] HEADER    : magic "MSFS" (4 bytes) + version (1 byte)
[0x10] TOTP rec  : id | label ≤48 | binary secret ≤64 (base32 DECODED)
                   | digits (6/8) | period_s (30/60) | created_epoch u32
[0x11] PWD rec   : id | label ≤48 | username ≤64 | password ≤128 | notes ≤256
[0x12] RCV rec   : id | label ≤48 | nb_codes | codes (nb × ≤16)
                   | used_mask u16 (bit i = code i consumed)
[0x20] SETTINGS  : lock_timeout_s u16 | frontlight_auto_off_s u16
                   | last_sync_epoch u32 | last_sync_drift_ppm i32
                   | utc_offset_min i16 | sync_warn_days u8 (default 45)
```

**Budget**: ciphertext ≤ 64 KB (~400-600 entries depending on note size).
The partition is 256 KB — margin for growth without changing the schema.

### 1.3 PIN anti-brute-force

- **Online** (on the device): N failures → exponential back-off (10 s, 30 s,
  2 min, 10 min, 1 h). Counter persisted in plaintext in the dedicated sector of
  the `secrets` partition (outside the blob). Accepted limitation: a flash
  erase resets the counter to zero BUT also erases the data — the attacker
  gains nothing. (Possible eFuse v2, Nice-to-Have.)
- **Offline** (flash extraction + KDF attack on PC): barrier =
  memory-hard Argon2id (m=8 MiB) × 10⁶ 6-digit PIN combinations —
  resistant to GPU/ASIC, unlike PBKDF2-SHA256 (ADR-004).

## 2. Flash partitioning (16 MB)

```
# partitions.csv (target ESP-IDF 5.5.x) — ADR-007: factory + backup otadata @0xB000
# CONFIG_PARTITION_TABLE_OFFSET=0xC000 mandatory (S3 bootloader hooks)
# app1 removed (ADR-011, 2026-09-15): freed space given to secrets.
nvs,       data, nvs,      0xD000,  0x3000,
otadata,   data, ota,     0x10000,  0x2000,
factory,    app, factory,0x20000, 0x100000,
app0,       app, ota_0, 0x120000, 0x200000,
secrets,   data, spiffs,0x320000, 0x240000,
coredump,  data, coredump,0x560000, 0x10000,
```

| Partition | Role |
|-----------|------|
| `nvs` | minimal — **no Wi-Fi credentials** (`CONFIG_ESP_WIFI_NVS_ENABLED=n`). Nothing secret. |
| `otadata` | boot slot selection (standard esp_ota mechanism) |
| `factory` | **PC-less recovery** (ADR-007): minimal app (plain e-ink UI) that flashes app0 from SD or serial and restores otadata from the backup @0xB000 |
| `app0` | **only app slot** (ADR-011: no network OTA, so no need for esp_ota A/B rollback) — written via SD/serial only (ADR-003), always from the factory (never a self-update while running) |
| `secrets` | encrypted container (section 1) + plaintext anti-brute-force counter at the head of the partition (sector 0) — 2.25 MB, enlarged with the space freed by app1 (ADR-011) |
| `coredump` | Phase 9 debug, can be removed for release |

Note: otadata backup @ **0xB000** (gap between the bootloader and the partition table),
magic `0xAA55AA55` — constants shared between hooks.c (bootloader) and the factory app.

## 3. Firmware update protocol via SD (F-06)

No network OTA (ADR-003: **SD or serial UART only**, never over the
network). **Since ADR-011 (app1 removed, single app slot): the update is
no longer an in-app feature, it always goes through the
factory** (`SD -> app0`, already implemented and validated — `docs/FACTORY.md`
§5.3). No esp_ota A/B rollback: resilience comes from the factory
itself (independent partition, always accessible by holding Power
≥ 10 s, cf. ADR-009 amended 2026-09-16), not from a second app slot.

**Files expected at the SD root** (factory convention):
```
msf-fw.bin              — app image (raw partition binary, unencrypted)
```
Renamed `msf-fw.ok` (success) or `msf-fw.bad` (failure) after the attempt —
cf. `docs/FACTORY.md` §5.3.

**Sequence** (in the factory, not in the app):
1. User boots into factory (Power held ≥ 10 s, ADR-009) → SD mounted →
   checks for the presence of `msf-fw.bin`.
2. `esp_ota_begin(app0)` → `esp_ota_write` (streamed from the SD, size
   checked ≤ partition size) → `esp_ota_end` — **`esp_ota_end` already validates
   the image header/checksum**: a truncated or corrupted file is
   rejected before it can become bootable.
3. `esp_ota_set_boot_partition(app0)` + reboot into the freshly
   written firmware.
4. **No application self-check nor automatic rollback**: if the
   new firmware doesn't boot or is logically broken, the user
   goes back into factory (Power held ≥ 10 s) and reflashes — manual
   recovery, not automatic, the accepted trade-off for dropping the 2nd
   slot (ADR-011).

**Residual threat accepted (ADR-003)**: without secure boot before the
final prerelease, anyone with the device + an SD card can flash arbitrary
firmware. Accepted: the v1 threat model assumes an attacker *without*
prolonged physical access (the PIN protects the data, not the firmware).

**Data import/export (F-06b / ADR-005)**: file
`/x4pro-export-YYYY-MM-DD.enc` — the SD never sees the plaintext and never
knows the device's PIN:

```
magic "MSFEX1" (8B, zero-padded) | version u16 | timestamp u64
| entry_count u32 | salt 16B | nonce 12B
| AES-256-GCM( Argon2id(export_passphrase, salt, m=8MiB, t=4, p=1), internal plaintext )
| tag GCM 16B
```

- **Dedicated export passphrase** (transfer mode, default) — a stolen SD
  alone is worthless. The "same key as the device" backup (restorable on a
  new device without an additional passphrase) remains available as an option.
- Atomic write (temp file + rename), **never overwritten**
  (date-based versioning in the filename).
- Import: parse + verify magic/version/**GCM tag** BEFORE touching the
  current keystore; bounded sizes, no unterminated strings; explicit
  confirmation + overwrite warning.

## 4. Internal APIs (C contracts)

Each module = an ESP-IDF component under `src/components/`. The headers below
are the contracts; the implementation is Phase 8.

### 4.1 `totp_engine` (already validated in Phase 4 — ported as-is)

```c
esp_err_t totp_engine_init(void);
esp_err_t totp_set_secret(const uint8_t *b32, size_t len);   /* or decoded binary */
esp_err_t totp_generate(uint64_t epoch_s, char *out_code,    /* 6/8 digits */
                        uint8_t digits, uint32_t period_s);
```

### 4.2 `secret_store`

```c
esp_err_t store_unlock(const char *pin);        /* derives the key, opens the blob */
void      store_lock(void);                     /* wipes the key from RAM */
bool      store_is_unlocked(void);
esp_err_t store_totp_add/update/delete/list(...);    /* filter type=TOTP */
esp_err_t store_pwd_add/update/delete/get(...);
esp_err_t store_rcv_mark_used(id, code_index);
esp_err_t store_export_to_sd(void);             /* F-06b */
esp_err_t store_import_from_sd(void);           /* F-06b, explicit confirmation */
/* Persistence: every mutation rewrites the entire blob (N is small, writes are rare,
   flash endurance saving: no incremental log in v1). */
```

### 4.3 `time_svc` (ADR-001 + trusted terminal ADR-006)

```c
esp_err_t time_sync_ble_cts(void);   /* NimBLE GATT client, CTS 0x1805/0x2A2B,
                                        scan→connect→read→disconnect, no pairing */
esp_err_t time_sync_serial(void);    /* time pushed by the PC over UART
                                        (mechanism validated at probe, cmd 'st') */
esp_err_t time_sync_wifi(const char *ssid, const char *pwd);  /* never persisted */
esp_err_t time_set_manual(int64_t epoch_s);                   /* HH:MM UI entry */
int64_t   time_now(void);                                     /* RTC + drift */
void      time_health(int32_t *drift_ppm, uint32_t *age_sync_s);
/* Invariants:
   - the 3 radio/serial channels carry ONLY the time, never keystore
     data (ADR-006);
   - Wi-Fi: esp_wifi_set_config(..., WIFI_STORAGE_RAM) (never FLASH),
     memset of the credential buffers after esp_wifi_stop(), then
     esp_wifi_deinit() to power down the PHY;
   - no credential ever passes through NVS (CONFIG_ESP_WIFI_NVS_ENABLED=n). */
```

### 4.4 `ui_mgr` + `input`

```c
void ui_show(screen_t s);     /* screen created for its context, the old one destroyed */
/* Screens: UNLOCK, HOME(menu), TOTP_LIST, TOTP_CODE, PWD_LIST, PWD_VIEW,
   RCV_LIST, RCV_CODE, ADD_EDIT_ENTRY, TIME_SYNC, SETTINGS, UPDATE_SD, STATUS */
/* input provides: mapped touch coordinates (swapXY+invert_y, cf. hw-spec),
   3 buttons, Home zone (raw point 36,479), long-press. */
```

### 4.5 `pwr`

```c
uint8_t pwr_battery_percent(void);    /* CW2017, validated formula */
void    pwr_frontlight_set(bool on, uint8_t level);  /* warm/cool, auto-off */
```

### 4.6 `update_svc` — see §3 (exact sequence).

## 5. Global invariants (non-negotiable)

1. No active network task at boot; the radio only turns on via `time_sync_wifi`.
2. No plaintext secret on the flash, the SD, the NVS, or the UART logs
   (logs disabled in release; `menuconfig`: no printf of secrets).
3. Every persistent format is versioned (magic + version) from the very first byte.
4. The UI never keeps two live screens simultaneously (memory).
5. The blob is only decrypted in RAM, and `store_lock()` wipes the key and the
   plaintext cache (memset + `heap_caps_check_integrity` in debug).
6. **All randomness (salt, nonce, IDs) comes from `esp_random()`** (hardware RNG,
   RF noise/jitter) — never `rand()`/`random()`.

## 6. Open points before Phase 8

- [ ] **Final Argon2id parameters**: measure actual m/t/p on the S3
      (target 0.5-1 s/attempt) and fix the values in the code.
- [ ] **PIN length**: fixed at 6 (FEATURES F-03). UI entry to be specified at
      the time of the UNLOCK screen (full-screen numeric keypad, anti-smudge layout).
- [ ] **Frontlight trigger** (F-16): manual activation confirmed; the exact
      UI widget is decided during implementation of the status bar.
