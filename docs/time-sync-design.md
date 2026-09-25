# Time management: design notes and lessons learned

Development notes for the Time feature (Settings > Time), written 2026-09-25 as a
memory aid: final architecture, the decisions behind it, the problems we hit and
how they were solved, and what is still open. The user-facing BLE setup guide is
`ble-time-sync-nrf-connect.md`; ADR-001 and ADR-006 in `ROADMAP.md` hold the
original decisions.

## 1. What it does

Three ways to set the device clock, from one screen (Settings > Time, three tabs):

| Tab | Source | Notes |
|-----|--------|-------|
| Wi-Fi | SNTP (`pool.ntp.org`) | Scan, pick a network, on-screen password, connect, sync. |
| BLE | Current Time Service of a phone | Scan, pick a device, read 0x2A2B (+ 0x2A0F), sync. No pairing. |
| Manual | Typed date/time | 12-digit keypad plus a fixed UTC offset stepper. |

Every sync is recorded (date, source, offset, see section 4) and the last one is
shown in Settings > About.

## 2. Architecture

```
ui_screen_time.cpp   the screen: tabs, Manual keypad, Wi-Fi and BLE lists
  |-- ui_keyboard.cpp        reusable alphanumeric keyboard (Wi-Fi password)
  |-- wifi_time.c            worker task: scan / join / SNTP
  |-- ble_time.c             worker task: NimBLE scan / connect / read CTS
  `-- time_service.c         THE clock: system time + BM8563 RTC + sync record
        `-- settings_store.c   NVS: UTC offset, last 3 syncs
ui_screen_settings_about.cpp  shows last sync + drift per day
```

Key rules:

- **UTC everywhere.** The system clock and the BM8563 RTC (I2C 0x51, battery backed)
  hold UTC, because TOTP (RFC 6238) is UTC-only. The time zone is a *fixed UTC
  offset in minutes* (NVS `tz_off_min`, no DST) used only to display and to type
  local time. Never feed local time to TOTP.
- **Boot:** `time_service_init()` loads the RTC into the system clock. From then on
  the system clock (ESP32 crystal, accurate) runs on its own; the RTC is only read
  again at the next boot.
- **Radios are short-lived.** Wi-Fi and BLE are initialised for one operation
  (scan or sync) and fully torn down afterwards (ADR-001: radio off in normal use).
  Wi-Fi credentials are never stored (`CONFIG_ESP_WIFI_NVS_ENABLED` off, RAM only,
  the copy is wiped after use).
- **Worker task + UI polling.** `wifi_time` / `ble_time` run in their own FreeRTOS
  task and publish a state and a message (release/acquire on an atomic). The UI
  creates a 300 ms `lv_timer` only when the user starts an operation and deletes it
  at the final state. Nothing on screen refreshes by itself (project rule: no
  refresh without a direct interaction).
- **BLE = GATT client** (NimBLE central only, one connection): active scan (names
  come in scan responses), connect, discover service 0x1805, read 0x2A2B (10 bytes:
  peer *local* time) and 0x2A0F (time zone, DST) when present, else use the
  configured offset. `UTC = local - tz - dst`. The system clock is set at the instant
  of the read (microsecond precision, 1/256 s fraction used); only the slow RTC
  write happens after tear-down.
- **RTC writes are aligned to the second** (section 4, problem 5).
- Persistent data (NVS, table driven in `settings_defs.inc`): `tz_off_min`, and a
  3-entry history `sync_e0..2` (epoch), `sync_d0..2` (offset, signed seconds,
  `INT32_MIN` = unknown), `sync_s0..2` (source: 0 manual, 1 Wi-Fi, 2 BLE).

Files: `time_service.[ch]`, `wifi_time.[ch]`, `ble_time.[ch]`, `ui_keyboard.[ch]`,
`ui_screen_time.cpp`, `ui_screen_security.cpp` (PIN keypad, UI test only),
`ui_screen_settings_about.cpp`.

## 3. UI decisions worth remembering

- **Keypad handlers run synchronously** in the click callback. `lv_async_call()`
  does not preserve call order: typing 4, 5, 6 quickly displayed 6, 5, 4. (`ui_defer`
  is still used for navigation, where order does not matter.)
- **Keyboard layout:** 26 characters over 4 rows (7/7/6/6) plus a control row (5
  rows total), instead of the classic 10-key QWERTY rows. Wider keys and larger gaps
  matter on this touch panel (about 8 px of calibration error). Layers: lowercase,
  uppercase (one-shot shift), digits and symbols, more symbols.
- **Focus outline clipping:** LVGL clips a child's outline to its direct parent, so
  every container that wraps buttons reserves `kFocusOutlineSlack` (10 px), and the
  keyboard is built screen-wide for the same reason.
- **Status text lives above the keyboard,** never under it.
- **Signal strength** is shown as a percentage (`2 * (rssi + 100)`, clamped 0-100),
  not raw dBm.

## 4. Problems met and how they were solved

1. **Boot loop after adding the Time screen (task watchdog).** The backtrace ended
   in `get_local_style` right after `LV_ASSERT_MALLOC`: LVGL's fixed 64 KB internal
   pool was exhausted by the ~50 keyboard keys, and an LVGL allocation failure spins
   forever. *Fix:* `CONFIG_LV_USE_CLIB_MALLOC=y` (LVGL uses the C allocator).
2. **Internal RAM exhausted (BLE init failed: "hci inits failed", ESP_FAIL).** After
   fix 1, only ~50 KB of internal heap remained (largest block 21 KB), not enough
   for the BLE controller and host. `SPIRAM_MALLOC_ALWAYSINTERNAL` (16 KB) sent every
   small LVGL/app allocation to internal RAM. *Fix:* `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0`
   (PSRAM first for `malloc`; DMA/ISR code asks for internal RAM explicitly, and
   `SPIRAM_MALLOC_RESERVE_INTERNAL` keeps a reserve) and
   `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y` (NimBLE host in PSRAM). Keep an eye on
   flush times if UI performance ever seems to change.
3. **No IDF error message for the BLE failure.** `esp3d_log` sets every IDF tag to
   `NONE` at boot, hiding the NimBLE/controller errors. *Fix:* `ble_time.c` raises the
   level to WARN while the stack is up, then restores it. Our own logs use the
   printf-based macros in `app_log_workaround.h`, which bypass that filter.
4. **Drift measurement was wrong (showed +4 s, +2 s).** The offset was computed as
   "system clock before SNTP" versus "system clock after SNTP", so it included the
   connect + DNS + round trip time. *Fix:* the clock is read together with a
   monotonic timestamp (`esp_timer`) just before SNTP; in the SNTP sync callback the
   expected clock is `old + elapsed monotonic`, and the offset is the SNTP time minus
   that. The log prints `SNTP set the clock, error was N ms`. The BLE path measures at
   the instant of the read (`time_service_set_system_precise`).
5. **RTC quantization (up to about 2 s).** The BM8563 only stores whole seconds. Writing
   it without regard to the fractional second, and reading it at a random phase at boot,
   both lose up to 1 s (always in the same direction: the device runs late).
   *Fix:* write with the STOP bit set and release it exactly when the system clock
   reaches the written second; at boot, poll the seconds register until it ticks
   (adds up to about 1 s to boot and 1-2 s to a sync).
6. **Reflash does not reset the RTC** (own battery), but any reboot rebuilds the system
   clock from the RTC, so RTC error (quantization plus real drift) shows up at boot.
   Between two syncs without a reboot, the ESP32 clock is accurate (offset 0 s).
7. **BLE phone not visible.** Causes found, in the order they bit us: (a) extended
   advertising on the phone (the device scans legacy advertising only), (b) the GATT
   server configuration in nRF Connect not saved/active (`No time service`), (c) the
   advertiser not connectable / app in the background.
8. **Partition warning at build:** the app (now about 1.5 MB) no longer fits the 1 MB
   `factory` partition; it fits `app0` (2 MB), where it is flashed. Harmless as long as
   the app is never installed in `factory`.

## 5. Security notes

- Any BLE device can advertise CTS with any name. The device sets its clock from
  whatever the chosen device sends, and TOTP depends on it. The current mitigation is
  only that the user has to pick the device.
- The Wi-Fi password is typed on the device and used once; not stored, not logged.

## 6. Open items

- Confirmation step before applying a BLE time (show device name, proposed time and
  offset; mandatory above a threshold such as 60 s), and/or refuse absurd jumps.
- Reminder to resync after about 3 months, and an optional ppm compensation from the
  measured drift per day (the About screen shows it once 3 syncs exist; the first three
  records taken before fix 4 are unreliable).
- BLE pairing/bonding (PIN) for peers that require encryption (iPhone/iPad, some
  Android phones). Untested: no iOS hardware.
- Extended-advertising scan support (`CONFIG_BT_NIMBLE_EXT_ADV`, `ble_gap_ext_disc`)
  or a hint in the BLE tab when no CTS device is found.
- BLE debug traces are compiled out; set `BLE_TIME_VERBOSE` to 1 in `ble_time.c` to get
  raw advertising packets, every device seen and the GATT steps.
