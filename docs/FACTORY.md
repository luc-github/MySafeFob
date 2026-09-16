# MySafeFob — Factory & Recovery Bootloader (technical doc)

> **Origin**: based on a previously proven recovery/bootloader-hook
> mechanism (same author, an earlier ESP32 project). This document adapts
> that design to the X4 Pro and **highlights the differences**.
> Porting decision: ADR-007 / ADR-008 / ADR-009 (`docs/ROADMAP.md`).

---

## 1. Differences from the original reference design (summary)

| Domain | Reference design (ESP32 classic) | MySafeFob (ESP32-S3 X4 Pro) |
|---|---|---|
| Recovery bootloader trigger | BTN3 = GPIO17 | **Power = GPIO3** held ≥ 10 s (GPIO0 strapping, GPIO3 strapping JTAG — Power+Right combo abandoned, cf. ADR-009 amendment 2026-09-16) |
| Recovery feedback | Buzzer (bit-bang PWM) | **ROM logs only** (no buzzer on the X4 Pro) |
| Display | ILI9341 240×320 RGB565, direct LCD writes | **UC8279 800×480 1 bpp, framebuffer + full refresh** (hardware 90° CW rotation) |
| gfx | zone-based flush to the LCD | **48 KB DRAM framebuffer**, `gfx_flush()` = full e-ink refresh (~2-4 s) |
| Touch | FT6336U @0x38, simple polling | **GT911 @0x5D/0x14, 185-byte config upload required on every boot**, software Home zone |
| Navigation | physical BTN1/2/3 + touch zones + encoder | **physical Left/Right + Home pad = select**, Power = fallback select. **No encoder** |
| SD | SPI (SDSPI) | **native SDMMC 1-bit slot 1** (CLK=41, CMD=42, DAT0=40) + GPIO5 power pulse |
| Flash targets on SD | app0/app1 + `ui_resources` | app0 (OTA, only app slot — ADR-011) + **factory (direct write — the recovery updates itself)**. No resources partition |
| Firmware file | `/sdcard/esp3dfw.bin` | `/sdcard/msf-fw.bin` |
| Screen snapshot (→ SD) | Yes (option) | **Not carried over** (not applicable to 1 bpp e-ink) |
| Software path to factory | `[ESP444]FACTORY` / touch screen | **Power held ≥ 10 s** (app or wake, ADR-009 amended 2026-09-16) — `power_mgr_switch_to_factory()` (§4) |
| IDF | 5.4.x | **5.5.5** (hooks porting validated at the 2026-09-13 build, §3.3) |
| Flash / partitions | 8 MB, PT offset 0xC000, factory 320 KB | **16 MB, PT offset 0xC000, factory 1 MB** (same table offset) |
| Sleep | no (LCD always powered) | **Deep sleep + GPIO3 wake** (ADR-009); bistable e-ink = image retained while off |

What remains **identical** to the reference design: the otadata
backup/erase/restore mechanism, the contractual constants, the OTA flash
logic, the factory sub-project structure, and the bootloader component's
CMakeLists.

---

## 2. The otadata mechanism (reminder — identical to the reference design)

The apps' "active flag" does not live in the app partitions but in
**otadata** (2 entries of 32 bytes: `seq` + `ota_state` + crc; the entry with the
highest valid seq = active slot). The `factory` partition has **no**
otadata entry: it is the **fallback** when otadata is empty/invalidated.

```
Boot USB / wake
  │
  ├── Hook: GPIO3 (Power) NOT held ≥ 10 s → normal boot (otadata → app0)
  │
  └── Hook: GPIO3 (Power) held ≥ 10 s
        │
        ├── 1. Backup otadata (2 entries) @0xB000 + magic 0xAA55AA55
        ├── 2. Erase otadata (2 sectors)
        ├── 3. Software reset
        │         └── Bootloader: otadata empty → boot factory
        │               └── Factory: restore otadata from the backup
        │                     └── Power-off → back to the correct app
```

Why the backup: without it, erasing otadata would destroy the "active
slot + seq counter" info → the OTA chain (alternation by seq parity) would be
broken. The restore copies the entries byte for byte (CRC included); the
erased magic prevents re-restoring in a loop; an empty backup (0xFF entries,
device never OTA-flashed) is detected and ignored.

**Contractual constants** (identical in `hooks.c` and
`boards/x4pro/factory/main/main.c`, verified by grep):

```
OTADATA_BACKUP_OFFSET   0xB000     free sector: after the S3 bootloader (~0x7000),
                                   before the partition table (0xC000),
                                   outside partitions (NVS @0xD000), 4 KB aligned
BACKUP_MAGIC            0xAA55AA55 @0x40 within the backup sector
BACKUP_MAGIC_OFFSET     0x40
OTADATA_OFFSET          0x10000
```

Rules for choosing the backup sector and a detailed diagram: same logic
as the original design's "Otadata Backup Layout", recalculated for the
16 MB layout (more headroom than in 8 MB).

---

## 3. Bootloader hook

Source: `src/boards/x4pro/factory/bootloader_components/custom_bootloader/hooks.c`
(direct port from the reference design). The factory lives UNDER the board
because it depends on its hardware — each board carries its own factory
(structural rule, cf. `src/README.md` §Structure).

### 3.1 What changes vs the reference design

- **Buzzer removed**: `buzzer_tone()/beep_*()` removed; acknowledgment =
  `esp_rom_printf` (gated by `FACTORY_LOG_LEVEL`, same pattern as before).
- **Button = GPIO3 / Power** (active-LOW, pull-up, same 3/5 debounce,
  10 s threshold). Power+Right combo (GPIO7) abandoned on 2026-09-16: the
  hardware never wakes the device when both buttons are held together
  (cf. ADR-009 amendment). `RECOVERY_BUTTON_PIN` migrated from GPIO7 to GPIO3.
- **S3 strapping**: GPIO0 (boot mode), GPIO3 (JTAG source), GPIO45/46.
  Never use GPIO0 in the hook. GPIO3 is used knowingly (already the EXT1
  wake pin for Power) — to be validated: the USB console remains usable
  after a GPIO3 hold (validation point 8c).
- **`CONFIG_IDF_TARGET="esp32s3"`**, octal PSRAM in the factory sdkconfig.

### 3.2 Bootloader budget

Same logic as before: `CONFIG_PARTITION_TABLE_OFFSET=0xC000` (44 KB of
bootloader headroom). The S3 bootloader can be larger (security features) —
**check the size on the first build** and, if it overflows, `rm -rf
build/bootloader && idf.py build`.

### 3.3 S3 porting points — validated at the 2026-09-13 build

All the points below were verified by the 1st successful factory build in
IDF 5.5.5 (binary `mysafefob-factory.bin`, 0x5A1F0, 65% free within the
1 MB partition):

- [x] `esp_rom_spiflash_read/write/erase_sector` signatures on S3 / 5.5.5
- [x] `gpio_ll_*` and `esp_rom_gpio_pad_select_gpio` — unchanged, compile
- [x] `esp_rom_software_reset_system()` — same signature, compiles
- [x] Bootloader size: **0x5520 < 0xB000** OK (comfortable margin)
- [x] Bootloader hooks: **no Kconfig option in 5.5.5** — unconditional
  mechanism (weak hooks `bootloader_hooks.h` called if defined).
  Symbol `bootloader_after_init` confirmed in `bootloader.elf`.
  `CONFIG_BOOTLOADER_HOOKS=y` removed from `sdkconfig.defaults` (dead option).

**Component renames observed in 5.5.5** (fixed at the build):
- `esp_flash` → `spi_flash` component (factory's REQUIRES)
- `esp_vfs_fat` → absorbed into `fatfs` (header `esp_vfs_fat.h` unchanged,
  no more managed dependency required)
- `esp_console` → `console` component (app's REQUIRES)
- New `gen_esp32part` constraint: `nvs` partition read/write ≥ 0x3000
  → `partitions.csv` bumps nvs from 0x2000 to **0x3000** (otadata
  offsets @0x10000 and backup @0xB000 unchanged — contracts preserved)

**Broken APIs observed on the app side (fixed at the build)**:
- REPL console: `esp_console_repl_start()` **removed** —
  `esp_console_new_repl_uart()` spawns the REPL thread itself (5.5)
- Deep sleep GPIO wake-up: S3 does **not** have `SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP`
  (no digital GPIO wake-up) → **EXT1** required. GPIO3 is an
  RTC IO: `esp_sleep_enable_ext1_wakeup(BIT3, ESP_EXT1_WAKEUP_ANY_LOW)`,
  wake cause = `ESP_SLEEP_WAKEUP_EXT1` (not `ESP_SLEEP_WAKEUP_GPIO`)
- `add_compile_definitions()` after `project()` does not propagate to
  IDF components → `idf_build_set_property(COMPILE_DEFINITIONS ... APPEND)`

**Binaries produced (2026-09-13 build)**: factory 0x5A1F0 (65% free
out of 1 MB), app 0x49EA0 (71% free), bootloader 0x5160 (58% free).
LVGL 9.2.2 fetched via the component manager (`managed_components/lvgl__lvgl`).

### 3.4 Rebuilding the bootloader

The bootloader is a separate sub-build: after modifying `hooks.c`,
force `rm -rf build/bootloader` or `idf.py bootloader-flash`.

**⚠️ PITFALL (discovered 2026-09-16, cost several debug cycles for
nothing): `hooks.c` is compiled ONLY by the `factory` project's build, never
by the app's.** ESP-IDF only detects `bootloader_components/` if it
is a direct child of the `PROJECT_SOURCE_DIR` of the project currently being
built (`components/bootloader/subproject/CMakeLists.txt`:
`set(PROJECT_EXTRA_COMPONENTS "${PROJECT_SOURCE_DIR}/bootloader_components")`).
This folder lives under `boards/x4pro/factory/bootloader_components/` — so
**only** `cd boards/x4pro/factory && ./msf_build.bat build` (or `idf.py
build` from that folder) sees it and compiles it. Running `./msf_build.bat
build` from the repo ROOT does build a `build/bootloader/
bootloader.bin`, but it is the STOCK ESP-IDF bootloader, whose
`bootloader_after_init` resolves to the weak stub in
`components/bootloader/subproject/main/bootloader_start.c` — **no trace
of hooks.c in it at all**, even though the build "succeeds" without error
(nothing flags it). Verifiable afterward with
`nm build/bootloader/bootloader.elf | grep bootloader_after_init`: if it
points to `libmain.a(bootloader_start.c.obj)`, it is the stub, not the hook.

The root build (`postbuild.cmake`) then **copies** (does NOT rebuild)
`boards/x4pro/factory/build/bootloader/bootloader.bin` (with the hook) to
`installer/x4pro_app/bootloader_16MB.bin` **if and only if** that factory
build already exists on disk — without ever checking that it is up to date
with the current sources. Modifying `hooks.c` and then rerunning only the
root build (even a full clean, `rm -rf build`) therefore **refreshes
nothing**: `installer/x4pro_app/bootloader_16MB.bin` silently stays the old
binary.

**Correct procedure after any `hooks.c` change**:
1. `cd boards/x4pro/factory && ./msf_build.bat build` (rebuilds the REAL bootloader with the hook)
2. `cd ../../.. && ./msf_build.bat build` (rebuilds the app + refreshes `installer/x4pro_app/` by copy)
3. Flash `installer/x4pro_app/bootloader_16MB.bin` (or directly
   `boards/x4pro/factory/build/bootloader/bootloader.bin`, identical) at
   offset `0x0` — this is the ONLY bootloader that matters, whether testing
   the app or the factory (shared bootloader, flashed only once).

A change to a CMake default (e.g. `option(... OFF)` → `option(... ON)`
in `bootloader_components/custom_bootloader/CMakeLists.txt`) also does not
apply as long as the factory project's existing CMake cache is not
invalidated: `rm -rf boards/x4pro/factory/build` before step 1 if an
`option()` has changed.

**⚠️ Another pitfall (observed 2026-09-16): the very first boot right after a
flash may look broken (no wake-up) even though the firmware is fine.**
The reset sent by `esptool` at the end of flashing (RTS/DTR toggle) is not
always electrically equivalent to a clean power-on — the RTC/pad state
can remain slightly unstable for this very first boot, especially since
we now directly manipulate GPIO3's RTC_IO routing in the hook
(§3, `rtcio_ll_function_select`). A 2nd reset (e.g. the one triggered by
simply connecting a serial monitor) is enough to settle into a stable
state. **Always perform an extra reset right after each flash,
before starting tests.**

---

## 4. Software path to the factory (to implement on the app side, task 8c)

MSF equivalent of the reference design's `[ESP444]FACTORY`: **Power held
≥ 10 s**, from the awake app or from wake-up (ADR-009, amended
2026-09-16 — Power+Right combo abandoned, hardware never wakes with both
buttons held together). The app-side handler
(`power_mgr_switch_to_factory()`) must reproduce the hook's sequence:

```
1. esp_flash_read(): backup of the 2 otadata entries @0xB000 + magic
   (address outside partitions → dangerous-write protection to handle:
   esp_flash_set_dangerous_write_protection() / sdkconfig)
2. esp_ota_set_boot_partition(factory)   → erases otadata
3. esp_restart()
     └── bootloader: otadata empty → factory → restore (like the hook)
```

The `src/components/power_mgr/` stub carries the contract; the
implementation arrives with the board drivers (8c).

---

## 5. Factory app

Source: `src/boards/x4pro/factory/main/` (13 files). Flow identical to
the reference design's architecture: restore otadata **first**, then init
screen/inputs, menu, actions.

### 5.1 Boot sequence

1. `restore_otadata_from_backup()` (same logic as before, constants §2)
2. `eink_init()` — validated UC8279 sequence (PSR 0x37 at init, PSR 0x17
   between PON and DRF — **never 0x37 at DRF**, full GC blocked, cf.
   `docs/hardware-specs.md`)
2b. `splash_show()` (**new on 2026-09-15**, ADR-010 amended) — displays
   `resources/splash.png` (converted to a 1bpp bitmap by `tools/gen_splash.py`
   → `main/splash_bitmap.h`) via `FreeInkUI::DisplayTarget`, a frozen copy
   specific to the factory (`components/freeinkui/`, never shared with the
   app). Covers the input/SD init time below with a fixed image instead of
   letting the user watch the menu itself get flashed by its own full
   refresh — see `splash.h`/`splash.cpp` (the only file in the
   factory that touches FreeInkUI, isolated from the rest written in C).
3. `buttons_init()`, `touch_init()` (best-effort, non-blocking)
4. SD probe (`msf-fw.bin`) — no more app1 probing (removed, ADR-011)
5. Loop: `button_wait_press(50)` + Home pad polling

### 5.2 Navigation (difference from the reference design)

| Input | Role |
|---|---|
| Left (GPIO0) | up |
| Right (GPIO7) | down |
| **Home pad** (GT911 touch zone `raw_x<70, raw_y 660-720`, recalibrated 2026-09-15 — the old 380-580 reading from 01:34 no longer matched the uploaded host config) | **select** |
| Power (GPIO3) | fallback select (recovery usable without touch) |

Touch is not re-danced between polls (known pitfall from earlier
touch-driver work: re-resetting on every read prevented scanning).

### 5.3 SD flash actions

| Item | Mechanism |
|---|---|
| `SD -> app0` | `esp_ota_begin/write/end` + `esp_ota_set_boot_partition` + reboot (same mechanism as before; only app slot since ADR-011) |

**`SD -> factory` removed (2026-09-15)**: writing the `factory` partition
while it is running from itself (XIP) exposes it to a brick with no
recovery safety net if interrupted mid-flight (power loss, bug). The
factory is treated like the bootloader: updated **only via
USB/serial** (`flash_mgr.py --variant x4pro_factory`), never self-service
in the field. See ROADMAP.md, ADR-007/ADR-011 amendment.
| `SD -> factory` | **MSF novelty**: direct `esp_partition_erase_range` + `esp_partition_write`, then reboot into factory. The recovery updates itself (consistent with ADR-003: SD-only updates) |

File conventions: `msf-fw.bin` → renamed to `msf-fw.ok` (success) or
`msf-fw.bad` (failure).

Progress: full redraw at every 10% step (a full e-ink refresh already
takes ~2-4 s; ~10 redraws for a 2 MB flash — acceptable for a
recovery). **Difference from the reference design**: no partial redraw
possible (e-ink); the earlier LCD-based version redrew by zones.

### 5.4 E-ink constraints (vs the reference design's LCD)

- A single `gfx_flush()` per screen (full framebuffer).
- No animation, no fast-press visual feedback (the full refresh
  is slower than a press) — the feedback is the next redraw.
- Menu selection = **video inversion** (black fill + white text).
- `eink_power_off()` before any outgoing `esp_restart()` (image retained).

### 5.5 Factory sdkconfig (same baseline as before + specific points)

```ini
CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED=y   # erasing the backup @0xB000
CONFIG_PARTITION_TABLE_OFFSET=0xC000         # MUST match the main app
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="../partitions.csv"  # relative to boards/<board>/factory/ -> boards/<board>/partitions.csv
```

Stack/heap budget: the e-ink framebuffer (48 KB) lives in **internal
DRAM** (zero PSRAM required, choice validated by the probe); the factory
fits comfortably within its 1 MB partition.

---

## 6. 16 MB flash layout (ADR-007, amended ADR-011: app1 removed)

```
0x001000  Bootloader (custom, S3 hooks)
0x00B000  ← OTADATA BACKUP (hooks.c / main.c contract)
0x00C000  Partition table (CONFIG_PARTITION_TABLE_OFFSET)
0x00D000  NVS          0x3000
0x010000  otadata      0x2000
0x020000  factory      0x100000  (1 MB)
0x120000  app0 (ota_0) 0x200000  (2 MB)
0x320000  secrets      0x240000  (2.25 MB — encrypted keystore blob, ADR-002;
                                  enlarged by 0x40000 with the space freed
                                  by app1, ADR-011)
0x560000  coredump     0x10000
```

Flashing is done via `flash_mgr.py` (interactive with memory) or the
`.bat` files in `boards/x4pro/flash_scripts/`:
- `x4pro_app` — full install (firmware + factory + bootloader hook +
  partitions + otadata with seq=1) → boots directly into the app;
- `x4pro_factory` — rescue (factory + bootloader + partitions + otadata
  zeroed) → GUARANTEED boot into the recovery menu.

The `installer/<variant>/` folders are populated by each project's
postbuild cmake step; each one carries a JSON flash map (`<variant>.json`,
format `{meta, files}`) — the single source of truth, consumed by
flash_mgr.py AND reusable by a web installer.

---

## 7. Troubleshooting

| Symptom | Lead |
|---|---|
| Bootloader too large | `CONFIG_BOOTLOADER_LOG_LEVEL` → WARN/NONE; check the 0xB000 margin |
| Bootloader not rebuilt after modifying hooks.c | `rm -rf build/bootloader` / `idf.py bootloader-flash` |
| Factory boot-loops | otadata restore not performed: check the backup magic, log "backup sector erased" |
| No restore after power cycle | check `CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED=y` (silent abort otherwise) |
| Gray screen + BUSY stuck | PSR 0x37 written at DRF: 30 s USB power cycle, fix the sequence (§5.1 / hardware-specs) |
| Touch dead in the factory | GT911 config upload (0x8047==0x00 → POR dance + 185 bytes) — cf. `touch.c`, do not re-dance between polls |
| SD crash (`LoadProhibited` tlsf_malloc) | heap too low for SD+OTA; here 512 KB S3 SRAM, monitor `esp_get_free_heap_size` in debug |

---

## 8. Deliberately not carried over from the reference design

- **Snapshot system**: LCD screen capture → SD for documentation. Not
  applicable: 1 bpp e-ink, and documentation is done differently (photos).
- **Rotary encoder**: no hardware on the X4 Pro.
- **Buzzer**: no hardware.
- **SD/telnet/web log backends** (esp3d_log): air-gap, the serial
  backend is sufficient for development.

---

*Sources: X4 Pro bring-up (`docs/hardware-specs.md`, `test_apps/x4pro-probe`),
ADR-007/008/009 (`docs/ROADMAP.md`).*
