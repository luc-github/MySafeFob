# FEATURES.md — MySafeFob (MSF): X4 Pro firmware — TOTP + Password Manager

> **Status**: v1.0 — ✅ **VALIDATED 2026-09-13 (Phase 6)** — amended 2026-09-26
> (F-02 alert threshold and Alerts screen, ADR-018; §7 UI library, ADR-017)
> **Rule**: once this document is validated, no new features until v1.0 (Phase 6 gate).
> Reference: `docs/ROADMAP.md` (ADR-001: time sync via Wi-Fi).

---

## 1. Vision

A pocket **air-gapped** device (XTEINK X4 Pro Developer Edition) that replaces Authy for
TOTP and stores passwords, **never connected in normal use**.
The trust surface is minimal: no persisted network credentials, no OTA,
updates only via SD.

## 2. Main use cases

1. **UC-1**: Open the device → unlock via PIN → choose an account from the
   TOTP list → read the 6-digit code (+ validity countdown) → type it on the target machine.
2. **UC-2**: Search / display a stored password to type it manually.
3. **UC-2b**: In case of a TOTP sync issue, display a recovery code
   for the relevant service and mark it used.
4. **UC-3**: Provision a new TOTP account (Base32 secret entry via UI).
5. **UC-4**: Sync the time (one-off Wi-Fi, or manual entry).
6. **UC-5**: Update the firmware via SD card.

## 3. Must Have (v1.0)

### F-01 — Multi-account TOTP
- Account list (label + Base32 secret + digits [6/8] + period [30/60 s]).
- Add / edit / delete an entry via UI.
- "One entry at a time" display: menu → selection → code screen.
- Visual countdown of code validity; the code stays displayed after expiry
  (grayed out/marked) until the next display — real usage = read then type.
- Engine already validated (Phase 3/4): RFC 6238, HMAC-SHA1, cross-checked with Authy + RFC vectors.

### F-02 — Time management
- **On-demand Wi-Fi sync only** (ADR-001): credentials entered each session,
  never stored; radio off outside of sync; SNTP → `settimeofday`.
- **Manual UI entry** (permanent fallback): human-readable date + HH:MM, a
  minute-boundary trick (confirm at :00 → ~1 s precision), UTC offset in settings.
- **Time health indicator**: age of the last sync + measured drift per day
  shown in Settings > About; **alert when the last sync is older than a
  threshold, default 90 days** (amended 2026-09-26, was 45 days — ADR-018):
  a warning button appears on HOME and opens the Alerts screen, which
  explains the problem and links to Settings > Time. The threshold is the
  setting `TimeSyncMaxAgeS`, changeable from the serial console (`setting`
  command, F-20) for tests without reflashing. "Never synced" raises an
  alert too.

### F-03 — PIN lock
- **6-digit PIN, fixed length (v1.0)** — trade-off: back-off protects against
  "online" brute-force, length protects against "offline" brute-force (flash
  extraction + KDF on PC: 10⁴ = trivial, 10⁶ = days with a slow KDF).
- PIN on power-up; automatic lock after inactivity (adjustable delay).
- N attempts with exponential back-off (anti online brute-force).
- Encrypted content is never readable without unlocking.
- **Anti-trace PIN pad**: non-sequential layout (or screen clearing
  after entry) to counter fingerprint-mark analysis on the screen.

### F-04 — Encrypted secret storage
- **Encrypted application blob** in a raw partition (no filesystem) — see ADR-002.
- AES-256-GCM (mbedtls); key derived from the PIN via **Argon2id** (ADR-004:
  memory-hard, m=8 MiB PSRAM, ~0.5-1 s/attempt — PBKDF2-SHA256 ruled out because
  it is GPU/ASIC-parallelizable; **confirmed Phase 5: no secure element**).
- Random salt, unique per device, stored in the clear (the salt is not secret).
- Blob format: `magic | version | salt | iterations | nonce | ciphertext | tag`,
  **versioned from the start** (project rule 3); detailed spec in Phase 7.
- TOTP secrets + passwords + recovery codes in the same encrypted container.

### F-05 — Password management
- Entries: label, username, password, optional notes.
- One entry displayed at a time (sober, monochrome screen, no animation).
- Add / edit / delete via UI.
- No copy-paste possible (air-gapped): the display is designed to be typed
  from (readable font, ability to reveal character by character).
- **Auto-clear**: automatic return to a neutral screen after a delay without
  interaction when a secret is displayed (the e-paper image persists while
  powered off — a displayed password would otherwise stay visible).
  **Delay decided 2026-09-26: default 5 min**, setting `SecretAutoClearS`
  (seconds, changeable with the console `setting` command, ADR-018).
  Applies to every screen showing a secret (TOTP code, password, recovery
  code). A TOTP code is only valid 30 s anyway, so the delay is about
  hiding what stays on the e-paper, not about the code's validity. If the
  idle-sleep timeout is shorter, sleep comes first and the sleep screen
  already hides the secret.

### F-05b — Recovery code management
- Entries: label (service) + list of one-time codes provided at 2FA activation.
- Stored in the same encrypted container; dedicated UI section.
- **Usage**: display one code at a time, mark it "used" after actual use
  (the code is then struck through but remains visible in history — traceability).
- Target use case: TOTP sync issue, temporary device loss, or a
  service requiring a recovery code to reconfigure 2FA.
- The Phase 2 TEST-PLAN already established the GitHub recovery procedure using
  these codes — the device becomes the reference store for them.

### F-06 — Firmware update via SD
- No OTA (project decision). Binary on SD → verification (checksum via
  `esp_ota_end`, cf. `INTERFACES.md` §3) → flash.
- **Decided (ADR-007, amended ADR-011)**: a single app slot (`app0`) + the
  `factory` partition as a safety net — no esp_ota A/B rollback
  (no network OTA, no scenario justifying it). Updates always go
  through the factory, never through the running app.

### F-06b — Data import/export via SD
- **Dedicated export passphrase** (ADR-005, default transfer mode): the exported
  SD card is unusable without it. "Same key as the device" backup option available.
- **Versioned format** `x4pro-export-v1.bin`: magic | version | timestamp |
  entry_count | salt | nonce | AES-256-GCM(internal cleartext) | tag — atomic
  write (temp + rename), never overwritten, versioned by date.
- **Import**: parse + verify tag BEFORE touching the current keystore,
  explicit confirmation and overwrite warning.
- The blob stays encrypted throughout the transfer: the SD card never sees the
  data in the clear.
- The SD card is also used for firmware updates (F-06); it is never mounted
  permanently.

### F-07 — Secure, sober boot
- Boots directly into the unlock screen; zero active network task at startup.
- Sober monochrome UI, one screen at a time: the screen is created for its
  context then destroyed to free memory (no persistent screen stack).

### F-19 — Sleep screen (deep sleep)
- **Principle**: e-ink retains the last image at zero power consumption → the
  screen shown before entering deep sleep remains visible indefinitely (like
  the original X4 Pro). This is a free UI surface, not a luxury.
- **Content**:
  - status indicator (device asleep / battery),
  - map of the physical buttons and their function (left / right / power / home),
  - **optional** owner contact info (phone or email) in case of loss —
    configurable in Settings, **disabled by default**.
- **Security**: nothing secret on screen (obviously), and the factory-entry
  gesture (Power held ≥ 10 s, ADR-009 amended 2026-09-16) **does not appear**
  on this screen — whatever is drawn on it ends up in the hands of whoever
  finds the device. Only wake-up (short power press) is documented visually;
  the factory gesture stays in the documentation.
- The sleep screen is also the home screen after a boot into main mode.

### F-20 — Serial command interpreter (permanent, not a throwaway tool)
- **Decision (2026-09-16)**: the REPL console (`esp_console`, USB-Serial/JTAG)
  started in `main.c` is **not** a temporary debug tool meant to
  disappear once the touch UI (F-06/task 8.4) ships — it stays in
  place permanently, to run commands and check statuses without
  going through the screen (diagnostics, support, testing). A command
  interpreter remains useful even once the product is finished.
- **Threat model**: accessible only via a physical USB connection — the same
  trust level as a firmware update via SD (F-06) or factory
  recovery (ADR-007/009): both already require physical access to the
  device. Exposes no secrets by default (no credential-dump command
  as long as F-03/F-04 are not in place).
- **Current scope** (Phase 8 skeleton): `help`, `about`,
  `totpselftest`, `sleep`. To be expanded over the phases (e.g. battery/RTC
  status, storage diagnostics) rather than replaced. Planned next
  (ROADMAP 8.0): `setting list|get|set|reset` over the whole NVS settings
  table (ADR-018) and `settime` (serial time sync, ADR-006).
- **⚠️ Pitfall encountered (2026-09-16), checkpoint for the future**:
  `esp_console_new_repl_usb_serial_jtag()`/`_uart()` do create the REPL
  task, but it stays parked in the `CONSOLE_REPL_STATE_INIT` state
  (no command processed, banner/logs still visible since they are
  independent of that task) until **`esp_console_start_repl(repl)`**
  is explicitly called to move it to
  `CONSOLE_REPL_STATE_START` (`esp_console_common.c`, IDF 5.5.5). An easy
  pitfall to miss precisely because the symptom is misleading: everything
  seems to work (prompt displayed, logs scrolling), only input never does
  anything. Spotted by comparison with
  `references/test_apps/x4pro-probe/main/main.c`, which does call it. **On
  every new use of `esp_console_new_repl_*()` in this project,
  verify that `esp_console_start_repl()` follows the call.**

## 4. Should Have (if time/memory budget allows)

| # | Feature | Notes |
|---|---------|-------|
| F-08 | Password generator | Nice-to-have (confirmed by user): length, character sets |
| F-10 | HOTP (counter) | RFC 4226 support for rare accounts |
| F-11 | Search/filter in entries | Useful beyond ~15 accounts |
| F-12 | Categories or tags | TOTP / passwords / personal / work |
| F-16 | Battery indicator (CW2017) + frontlight in the status bar | Drivers already validated Phase 5. Frontlight **off by default**, manual turn-on only (exact trigger TBD Phase 8), adjustable auto-off (default 30 s) |

## 5. Nice to Have (post-v1.0)

| # | Feature | Notes |
|---|---------|-------|
| F-13 | Duress PIN (decoy content) | Advanced threat model |
| F-14 | User passphrase (vs numeric PIN) | Stronger key, slow entry on e-ink |
| F-15 | SHA-256/SHA-512 TOTP support | Rarely required by services |
| F-17 | **Read-only USB-MSC bridge**: the SD card exposed as a USB drive (TinyUSB, write-protected) → official Cryptomator vault read/decrypted by the host PC/phone (Android OTG; limited on iOS) | Read-only; zero crypto to reimplement; the device becomes a carrier vault |
| F-18 | Custom e-ink viewer for encrypted documents | Optional — only if the "read without a PC" need arises; custom vault not Cryptomator-compatible, images only |

## 6. Explicit non-goals (out of scope for v1.0)

- **OTA / network updates** — SD only.
- **Connectivity in normal use** — Wi-Fi radio off outside time sync.
- **Storing Wi-Fi credentials** — ADR-001.
- **Copy-paste / USB bridge to the target machine** — usage is "read and type".
- **QR code reading / camera import** — no camera on the X4 Pro (confirmed by press coverage);
  manual Base32 entry only for provisioning.
- **Authy-style cloud backup** — backup = encrypted SD (F-06b).
- **Multi-user** — single-user personal device.
- **UI animations/transparency** — sober monochrome, no animation (user decision).

## 7. Technical constraints (confirmed Phase 5 — 2026-09-13)

| Constraint | Impact |
|------------|--------|
| Flash = only persistent storage, SD mounted only on demand | The SD card serves as: (1) firmware update area, (2) backup export/import, (3) Cryptomator document vault (F-17, read-only wired USB-MSC bridge). Never mounted permanently |
| ESP-IDF 5.5.x (not 6.x: stability + footprint) | Fixed build target; watch for component renames (`console` vs future `esp_console`) |
| **LVGL 9.x** for the app (ADR-017, 2026-09-21 — supersedes ADR-010's FreeInkUI, which only the factory still uses) | Sober mono theme; our own ports: `lv_port_disp.c` (flush into `eink.c`, DU/GC choice, zone refresh) and `lv_port_indev.c` (touch + Left/Right focus groups, confirm) |
| **3.7" e-ink typography** (confirmed by user 2026-09-14) | 8×16 = unreadable; **minimum 16×32 px** (factory + splash validated at this size). The UI must build on fonts ≥ 20-24 px equivalent, adapted per panel density |
| **No secure element** (confirmed Phase 5) | The offline barrier = memory-hard Argon2id KDF (F-03/F-04, ADR-004); never store a key in the clear |
| **BM8563 RTC with validated backup** (survives reboots) | F-02 is still required for initial sync/resync, but time persists between usages |
| **UC8279 E-Ink: native 800×480 landscape fb, hardware 90° CW rotation** | fb to be drawn in native landscape, raw stream; never PSR REG=1 at DRF (freeze) — cf. `hardware-specs.md` |
| **Wi-Fi/NVS**: esp_wifi stores credentials in NVS by default | `CONFIG_ESP_WIFI_NVS_ENABLED=n` + `WIFI_STORAGE_RAM` + wipe buffers (acceptance criterion 2) |
| **Argon2id KDF** (ADR-004) | libsodium or monocypher port in `src/components/`; parameters calibrated to ~0.5-1 s on S3 |
| X4 Pro hardware: **bring-up complete** (E-Ink, GT911, RTC, CW2017, SD, frontlight, buttons) | Definitive reference: `hardware-specs.md` — drivers to be ported as-is |

## 8. Global acceptance criteria (v1.0)

1. TOTP codes from the device are valid on 3 distinct real services (GitHub + 2 others).
2. No trace of Wi-Fi credentials in flash after a sync (binary audit,
   including NVS — `CONFIG_ESP_WIFI_NVS_ENABLED=n`).
3. The encrypted DB withstands raw flash extraction (no key in the clear).
4. SD update validated with factory recovery on failure (ADR-011, Phase 9).
5. Battery life ≥ 2 weeks of real usage (Phase 9 test).
6. The sleep screen (F-19) exposes no sensitive data and does not
   document the factory gesture.

---

*Document to be validated: any modification after validation requires a formal review.*
