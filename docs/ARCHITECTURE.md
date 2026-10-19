# Software architecture — MySafeFob v1 (as built)

**Date**: 2026-10-12 (ROADMAP 1.5), end of Phase 8. Hardware: Xteink X4 Pro
(ESP32-S3, 16 MB flash, 8 MB octal PSRAM, 480x800 UC8279 e-paper, GT911
touch, three buttons, CW2017 gauge, BM8563 RTC, SD slot) — `hardware-specs.md`.
Formats and contracts: `INTERFACES.md`; screens: `UI-SPECS.md`; decisions:
ADRs in `ROADMAP.md`; security: `SECURITY-THREAT-MODEL.md`.

---

## 1. Two firmwares, one flash

```
0x0000   bootloader (factory build, with the hook: Power held 10 s -> factory)
0xB000   otadata backup sector (hook <-> factory contract, ADR-007)
0xC000   partition table
0xD000   nvs        settings (no secrets)
0x10000  otadata
0x20000  factory    1 MB   recovery firmware (frozen, plain gfx, no LVGL)
0x120000 app0       2 MB   the MySafeFob app (LVGL)
0x320000 secrets  2.25 MB  encrypted account store (control sector + 2 slots)
0x560000 coredump
```

- **App** (`main/` + `boards/x4pro/app/` + `components/`): everything the
  user sees day to day. Built by `msf_build.bat`, ~1.7 MB (85 % of `app0`).
- **Factory** (`boards/x4pro/factory/`): recovery, reached by holding Power
  10 s (awake or asleep): boot app0, flash `msf-fw.bin` from the SD card
  into `app0`, rescue export of the store (device PIN). Never updates
  itself; shares only `secret_store` + `monocypher` with the app
  (`FACTORY.md`).
- Updates: SD card through the factory, or USB (`tools/flash_scripts`).
  No network OTA (ADR-003/011).

## 2. App layers

```
┌──────────────────────────────────────────────────────────────────────┐
│ Screens   ui_screen_*.cpp (Home, Names, Account, AccountEdit/Field,   │
│           Recovery, Pin, Backup*, Settings*, Time, Owner, Alerts,     │
│           TouchDiag), ui_sd_check.cpp                                 │
├──────────────────────────────────────────────────────────────────────┤
│ UI kit    ui_widgets.cpp (buttons, rows, steppers, check boxes,       │
│           confirmation modal, auto-clear, ui_defer, touch band),      │
│           ui_keyboard.cpp (full / Base32 / numeric), fonts + icons    │
├──────────────────────────────────────────────────────────────────────┤
│ Nav/power ui_nav.cpp: LVGL loop, screen switching, input routing,     │
│           idle sleep, auto-lock, lock transitions                     │
├──────────────────────────────────────────────────────────────────────┤
│ Services  accounts_store.c (accounts.h over secret_store),           │
│           backup.c + sdcard.c, time_service.c (+ wifi_time.c,         │
│           ble_time.c), settings_store.c (NVS table), alerts.c         │
├──────────────────────────────────────────────────────────────────────┤
│ Drivers   lv_port_disp.c -> eink.c (UC8279), lv_port_indev.c ->       │
│           touch.c / buttons.c, battery.c, frontlight.c, i2c_bus.c,    │
│           splash.cpp (sleep screen, drawn without LVGL)               │
├──────────────────────────────────────────────────────────────────────┤
│ Shared    components/: secret_store (Argon2id + AES-256-GCM store and │
│ comps     .msfx format), monocypher (Argon2id), totp_engine (RFC      │
│           6238 + Base32), power_mgr (deep sleep, switch to factory),  │
│           esp3d_log                                                   │
└──────────────────────────────────────────────────────────────────────┘
main/: app_main (init order, console REPL) + console commands
       (store, backup, setting, settime, battery, kdfbench, about, ...)
```

Rules that hold everywhere:

- **One LVGL task** (`ui_nav`, 8 KiB): every event callback defers its work
  with `ui_defer()`; nothing slow runs there.
- **Slow work in worker tasks** (12 KiB stacks, PSRAM buffers): Argon2id
  (PIN unlock / create / change / confirm, backup export / load), SD card
  access, store writes during an import, Wi-Fi / BLE time sync. The
  screen polls their result with an `lv_timer`.
- **No scrolling** on e-paper: pages of fixed rows, at most 5 rows per
  menu level; fixed y positions that avoid the touch dead band (y 490-554,
  `touch_safe_y()`).

## 3. Display and input path

```
input_sampler_task (4 KiB)          ui_nav task                    panel
touch.c / buttons.c  --queue-->  lv_port_indev  -->  LVGL  -->  lv_port_disp  -->  eink.c
 (calibrated Y table,             (edges kept,       (DIRECT      (transposes the    (fast DU on the
  touch band)                      coalescing)        render)      changed areas)     changed gates;
                                                                                      full GC on a
                                                                                      screen switch or
                                                                                      every 30 fasts)
```

- LVGL renders 1 bpp (I1) into one full-screen PSRAM buffer in **DIRECT**
  mode: only invalidated areas are redrawn; their union gives the gate
  window of the fast refresh (2026-10-11; FULL mode had made every change a
  full-panel refresh). Console: `DispZone`, `DispTiming`.
- Fonts `msf_font_24/32` = Montserrat + LVGL symbols + extra Font Awesome
  icons (`tools/gen_fonts.py`, Regular glyphs from `resources/fonts/`);
  logo image from `tools/gen_logo.py`.

## 4. Data flows

**Unlock**: PIN screen -> worker: `msf_store_unlock()` = Argon2id(PIN, slot
salt) -> AES-GCM decrypt the newest slot -> records into a PSRAM array
(`accounts_store.c`) -> Home. Failure counter written before the check.

**Read / edit an account**: screens read copies through `accounts.h`
(`accounts_get`, `accounts_list`); every change (`add`, `update`,
`remove`, `rcv_mark_used`, `import`) re-encrypts the whole array into the
other slot (`msf_store_save()`), header last; a failed write rolls the RAM
copy back.

**TOTP**: `accounts_totp()` decodes the Base32 key into a stack buffer,
`totp_generate()` (HMAC-SHA1, RFC 6238) with the system time, wipes the
buffer. Time comes from the RTC at boot and from manual syncs (Wi-Fi
NTP, BLE CTS, serial `settime`), `time_service.c`.

**Lock**: manual, auto-lock timer, deep sleep (reboot) or console ->
records wiped, key wiped -> PIN screen.

**Backup export**: Backup menu (PIN confirmed) -> passphrase twice ->
worker: copy records -> `msf_backup_seal()` (Argon2id t=3, AES-GCM) ->
`/sdcard/MSF/MSF-YYYYMMDD-HHMM.msfx` (temp + rename).

**Backup import**: file list (headers only) -> passphrase -> worker:
`backup_load()` = read + `msf_backup_open()` (authenticated before
parsing) -> preview (new / changed / unchanged / removed) -> Merge or
Replace (the backup's profiles only, 2026-10-17) -> `accounts_import()` = one store write.

**PC round trip**: `msf_backup.py decrypt` (.msfx or RESCUE .MSR) -> JSON ->
`msf_editor.html` -> `msf_backup.py encrypt` -> card -> Import.

## 5. Persistent state

| Where | What | Owner |
|---|---|---|
| `secrets` partition | control sector (failure counter), 2 encrypted slots | `secret_store` |
| NVS (`settings_defs.inc`) | UI and power settings, time sync history, owner info, auto-lock, SD check, display tuning, touch calibration | `settings_store.c` (table-driven, `setting` console command) |
| RTC (BM8563) | UTC time, kept in deep sleep | `time_service.c` |
| SD card | `.msfx` backups, rescue copies, `msf-fw.bin` for the factory | `backup.c`, factory |
| eFuses | nothing yet (secure boot / flash encryption planned) | `SECURE-BOOT-PLAN.md` |

## 6. Power

- Idle timeout (Auto-sleep, Controls), Power held ~1.5 s, or the Home
  power icon -> deep sleep (a short Power press is Select, if enabled):
  rails held off through the RTC domain, sleep image (owner info, low
  battery note) drawn by `splash.cpp`, wake on Power. Wake = full reboot,
  so the store is always locked after sleep.
- "Stay awake on USB" suspends the idle sleep only, never the auto-lock.
- Battery: CW2017 gauge (profile re-uploaded after a full drain), low
  alert and automatic sleep at the critical threshold.

## 7. Tools (PC)

| Tool | Role |
|---|---|
| `msf_build.bat` (+ factory's) | ESP-IDF 5.5.5 build wrappers |
| `tools/flash_scripts`, `tools/build_scripts` | flash manager (keeps the data partition), installer images |
| `tools/msf_backup.py` | `.msfx` / rescue copy <-> JSON |
| `tools/msf_editor.html` | offline JSON editor (no network) |
| `tools/settime.py` | serial time sync |
| `tools/gen_fonts.py`, `gen_logo.py`, `gen_splash.py`, `gen_sleep.py` | generated assets (committed) |
