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

> **As implemented 2026-10-10** (`components/secret_store`, ROADMAP 8.2
> step A) — supersedes the table below where they differ:
>
> - **Partition layout** (`secrets`): control sector at 0x00000 (plaintext
>   `"MSFC"` | version | fail_count u32 | CRC32), **slot 0** at 0x10000 and
>   **slot 1** at 0x30000 (128 KiB each). A save writes the slot that does
>   not hold the newest blob, body first and header (with its magic) last:
>   a power cut mid-write leaves the previous blob in use (atomic replace).
>   Unlock tries the newest slot (highest `seq`), then the other.
> - **Slot header** (46 bytes, authenticated as GCM additional data):
>   `"MSF1"` | format_version=1 | kdf_algo=2 (Argon2id) | kdf_t | kdf_p |
>   kdf_m_mib u16 | salt[16] | nonce[12] | **seq u32** | **ct_len u32**,
>   then ciphertext[ct_len] | tag[16]. Nonce = 8 random bytes + seq (unique
>   per write). Salt from the hardware RNG with its entropy source enabled;
>   a new salt (and key) on PIN change, which writes both slots.
> - **Format 2 (2026-10-12)**, up to 250 accounts: two 512 KiB slots at
>   **0x100000** and **0x180000**; header = the same fields with
>   format_version=2, padded with zeros to **64 bytes** (all 64 bytes are
>   the AAD); ciphertext | tag written at +64, rounded up to a 16-byte
>   multiple (flash-encryption ready). Saves always go to a format 2 slot;
>   the format 1 slots above are only read (an older store) and are erased
>   once its first format 2 save succeeded. Unlock tries every valid slot,
>   newest `seq` first.
> - **Account fields added 2026-10-13** (no format change: older firmware
>   skips unknown tags): `0x24` ID (customer / account number, e-mail…,
>   64 B), `0x25` Extra (one free line, 64 B). PC JSON keys `id`, `extra`.
> - **PIN failures** (§1.3): counter incremented *before* each check and
>   reset on success; attempts 1-2 free, then 10 s, 30 s, 2 min, 10 min,
>   then 1 h between attempts, counted from the last failure (or from boot
>   after a restart). No automatic erase (user decision 2026-10-10).

| Offset | Size | Field | Notes |
|--------|--------|-------|-------|
| 0      | 4      | magic `MSF1` | MySafeFob Blob, format v1 |
| 4      | 1      | format_version | = 1 |
| 5      | 1      | kdf_algo | 2 = Argon2id (ADR-004; 1 = PBKDF2 reserved, never written) |
| 6      | 1      | kdf_t | Argon2id passes (1, ADR-004 decision 2026-10-10) |
| 7      | 1      | kdf_p | parallelism (1) |
| 8      | 2      | kdf_m_mib | memory in MiB (2, ADR-004 decision 2026-10-10), uint16 LE |
| 10     | 16     | salt | random (`esp_random()`), generated once at first commissioning, **stored in plaintext** |
| 26     | 12     | nonce | random on every blob rewrite |
| 38     | N      | ciphertext | AES-256-GCM of the internal plaintext (§1.2) |
| 38+N   | 16     | tag | GCM authentication tag |

- **Key**: `Argon2id(PIN_utf8, salt, m, t, p=1)` with Monocypher 4.0.2.
  **m = 2 MiB, t = 1** (ADR-004 decision 2026-10-10, measured ~1.0 s on
  the S3 at 240 MHz; 8 MiB = the whole PSRAM, impossible); the m/t actually
  used are stored in the envelope header (`kdf_*` fields), parameters
  recalibrated at implementation time for
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

> **Superseded by ADR-019 (2026-10-09)** for the entries: one ACCOUNT
> record per account replaces the TOTP / PWD / RCV records below (not
> implemented yet, so no migration). Ids become u16 (the budget allows
> more than 255 records).
>
> ```
> [0x30] ACCOUNT    : id u16 | name ≤48 (unique, case-insensitive) | created_epoch u32
>                     | modified_epoch u32 | then 0..3 sub-records, each at most once:
>   [0x31] OTP      : binary secret ≤64 (base32 DECODED) | digits (6/8) | period_s (30/60)
>   [0x32] LOGIN    : username ≤64 | password ≤128 | pin ≤12 (digits) | note ≤64 (one line)
>                     (ADR-019 amendment 2026-10-09: PIN added, notes reduced to one line)
>   [0x33] RECOVERY : nb_codes (≤32) | codes (nb × ≤16) | used_mask u32 (bit i = code i consumed)
> ```
>
> Sorted by name, case-insensitive; index letter = first character's
> letter A-Z, `#` otherwise.
>
> **As implemented 2026-10-10** (simpler, forward compatible): plaintext =
> `"MSFS"` | version=1 | records; record = `0x30` | len u16 | fields; field
> = tag u8 | len u16 | bytes, empty fields omitted, unknown tags skipped.
> Fields: 0x01 id u16, 0x02 name, 0x10 TOTP key (**Base32 text**, kept as
> typed so editing round-trips; decoded only to compute a code), 0x11
> digits, 0x12 period, 0x20 username, 0x21 password, 0x22 PIN, 0x23 note,
> 0x40 recovery used mask u32, 0x41 one recovery code (repeated, in
> order). No created/modified epochs.

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

**Data import/export (F-06b / ADR-005 + amendment 2026-10-11), as
implemented (ROADMAP 8.0 P11)**: `/MSF/MSF-YYYYMMDD-HHMM.msfx` on the SD
card (local time; `MSF-NODATE` if the clock is not set; `-2`...`-9` added,
never overwritten). The device reads and writes **only** this encrypted
format; the readable JSON lives on the PC (`tools/msf_backup.py`).

```
header (54 B, packed, little-endian, authenticated as GCM AAD):
  magic "MSFX" | format_version u8 (1) | kdf_algo u8 (2 = Argon2id)
  | kdf_t u8 (3) | kdf_p u8 (1) | kdf_m_mib u16 (2) | salt 16B | nonce 12B
  | created i64 (UTC s, 0 = unknown) | count u16 | reserved u16 | ct_len u32
AES-256-GCM( key = Argon2id(passphrase, salt, m, t, p), 32 B;
             plaintext = the store's TLV serialization, §1.2 )
tag 16B
```

- **Dedicated passphrase**, 8 to 64 characters, never the PIN: a stolen
  card alone is worthless, and so is a stolen device with its PIN.
  Argon2id with 3 passes (~3 s on the S3): a backup is made rarely. Fresh
  salt, so a fresh key, per file: the nonce is random.
- Write: temp file `MSF/MSF.TMP` + fsync + rename, so a pulled card never
  leaves a truncated `.msfx`. Listing reads each header (a file whose size
  does not match its header is not listed).
- Import: the whole file is authenticated (GCM tag over header + records)
  and parsed **before** the store is touched; header values are bounded
  before any allocation or KDF. Then a summary: new / changed /
  unchanged accounts (names match case-insensitively, "changed" = any
  field differs), unusable records, and for Replace all the accounts it
  would remove. Then a mode: **Merge** (new names added, differing
  accounts overwritten, the rest untouched, nothing removed) or **Replace
  all** (the store becomes the backup: full sync, removals included). The
  backup / PC JSON is the reference (user decision 2026-10-11, was Add /
  Update / Replace). ONE store write, none if nothing changes: if it
  fails, nothing changed. New accounts get new ids, updated ones keep
  theirs. Unusable records (empty name, bad TOTP key, digits outside 6-8,
  period 0) are never imported.
- The former "same key as the device" option is dropped (amendment
  2026-10-11): one format, one passphrase.

**Factory rescue copy** (`docs/FACTORY.md` §5.3b): `/MSF/RESCUEnn.MSR` =
the newest store slot exactly as on flash (§1.1 slot header, 46 bytes in
format 1, 64 in format 2,
as GCM AAD | ciphertext | tag), written by the factory after the device
PIN; `msf_backup.py decrypt` recognizes the `MSF1` magic and asks the PIN.

**PC JSON** (`tools/msf_backup.py decrypt` / `encrypt`): see the tool's
docstring. `totp` is `{key, digits, period}` or an `otpauth://totp/` URI;
recovery codes are strings (available) or `{"code", "used": true}`. The
tool checks the device's limits and refuses (never truncates) a field
that does not fit.

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
/* ADR-019 (2026-10-09): one API per account record, replacing
   store_totp_* / store_pwd_* / store_rcv_*: */
int       store_account_count(char letter);         /* 'A'-'Z', '#', 0 = all */
esp_err_t store_account_list(char letter, int first, int max, ...);  /* names + ids, sorted */
esp_err_t store_account_get(uint16_t id, ...);      /* name + present parts */
esp_err_t store_account_add/update/delete(...);     /* name unique, case-insensitive */
esp_err_t store_account_totp(uint16_t id, time_t utc, ...); /* code + seconds left */
esp_err_t store_rcv_mark_used(uint16_t id, int code_index);
/* F-06b, as implemented: msf_backup_seal/peek/open (secret_store.h, file
   image in RAM), backup_export/list/load (app backup.h, SD), then
   accounts_import(records, n, ADD|UPDATE|REPLACE) (app accounts.h). */
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
/* As built (2026-09-26): time_service.c/.h (board app) owns the clock
   (UTC in system clock + BM8563 RTC), wifi_time.c and ble_time.c are
   the Wi-Fi/BLE sources, the last 3 syncs are kept in NVS (settings_store)
   for drift tracking. Serial sync = console command `settime` (ROADMAP
   8.0 P4). The staleness check is the alert registry's job (ADR-018,
   alerts.c), threshold = setting TimeSyncMaxAgeS (default 90 days). */
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
/* Amended 2026-09-26: the authoritative inventory is UI-SPECS.md §1.4
   (UPDATE_SD dropped, STATUS folded into SETTINGS_ABOUT, ALERTS added by
   ADR-018). Implemented on LVGL (ADR-017) as `enum class Screen` +
   switch_screen() in ui_screens.h. Settings' Back uses one "return-to"
   screen id (UI-SPECS §1.3.1), not a stack. */
/* input provides: mapped touch coordinates (swapXY+invert_y, cf. hw-spec),
   3 buttons, Home zone (raw point 36,479), long-press. */
```

### 4.5 `pwr`

```c
uint8_t pwr_battery_percent(void);    /* CW2017, validated formula */
void    pwr_frontlight_set(bool on, uint8_t level);  /* warm/cool, auto-off */
```

### 4.6 `update_svc` — see §3 (exact sequence).

### 4.7 `alerts` (ADR-018, added 2026-09-26)

```c
typedef enum { ALERT_TIME_NEVER_SYNCED, ALERT_TIME_SYNC_STALE, ALERT_COUNT } alert_id_t;
int  alerts_evaluate(void);                 /* re-checks every condition, returns the active count */
bool alerts_is_active(alert_id_t id);       /* from the last alerts_evaluate() */
const char *alerts_title(alert_id_t id);
void alerts_describe(alert_id_t id, char *buf, size_t len);  /* explanation, built now */
/* Invariants: evaluated when HOME is built, never from a timer; alerts
   are condition-based (no acknowledge state in v1); the text never
   contains a secret. */
```

### 4.8 Serial console settings access (ADR-018)

`setting list | get <id> | set <id> <value> | reset <id>` over the
`settings_defs.inc` table (F-20). Values: BOOL `0/1/on/off`; U32 decimal,
negative accepted, optional `s/m/h/d` suffix. Secrets are never in that
table (they live in the `secrets` partition), so the command cannot expose
them.

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
- [x] ~~**Frontlight trigger** (F-16)~~: done — Settings > Display toggle
      (ADR-016).
