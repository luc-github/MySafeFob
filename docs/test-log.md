# Test log — MySafeFob (formerly standalone-TOTP)

Format defined in `docs/TEST-PLAN.md` §7. One block per session.

---

## Session 2026-09-07 — Phase 3 (Python)

### Setup
- Service: GitHub (`totp-user-test`)
- Tool: `test_apps/totp_reference.py`
- Reference: Authy (same secret)

### Results
| Test | Result | Notes |
|------|----------|-------|
| T1 | ✅ PASS | 5+ codes generated, all identical to Authy |
| T2 | ✅ PASS | Successful GitHub login with the script's code |
| T3 | ✅ PASS | Drift = 0 s, 30 s transitions in sync with Authy (continuous monitoring) |
| T3-bis | ✅ PASS | 30 s countdown + auto-generation: switches at the exact same instant as Authy, same values |
| T4 | ✅ PASS | Old code rejected after expiration |

### Issues encountered
- None

### Actions
- Phase 3 validated → moving on to Phase 4 (ESP-IDF)

---

## Session 2026-09-07 — Phase 4 (ESP32, ESP-IDF test app)

### Setup
- Board: generic ESP32 dev board (target `esp32`), 4 MB flash detected
- ESP-IDF: v5.4.3, GCC 14.2.0 xtensa
- Project: `test_apps/esp32-totp-test` (REPL `console`, prompt `totp>`)
- Secret: `JBSWY3DPEHPK3PXP` (dummy, same as the Python script)
- Time: entered manually via `st <timestamp>` (no battery-backed RTC)

### Results
| Test | Result | Notes |
|------|----------|-------|
| E1 | ✅ PASS | Boot self-tests: Base32 2/2, RFC 6238 vectors 6/6 (incl. T=20000000000 → 65353130) |
| E4 | ✅ PASS | Correct codes for the ESP32's time; measured sync offset: **26 s constant** (PowerShell copy-paste-to-monitor delay), no drift within the session |
| E2/E3 | ✅ PASS | Real GitHub secret entered via `s`, codes continuously identical to Authy → code valid by construction; GitHub login deemed redundant (it would test the server window, not our engine) |

### Issues encountered
1. **Build**: `base32_dec` array oversized (272 elements) → replaced with a C99 designated initializer.
2. **Build**: `-Werror=format-truncation` on `snprintf("%0*u")` → manual digit-by-digit formatting.
3. **Build**: `bool` used without `#include <stdbool.h>` → added.
4. **Runtime**: nested `fgets` on stdin UART return NULL (impossible to enter secret/time) → migrated the test app to the `esp_console` REPL (component named `console` in IDF 5.4.x), single-line arguments (`s <secret>`, `st <ts>`).
5. **Display**: `T=20000000000` truncated to `-1474836480` by a 32-bit `(long)` cast in the log → fixed to `(long long)` (cosmetic, time is properly 64-bit).

### Measurements
- Manual sync offset: 26 s, constant across several trials → confirms ESP32 clock stability; error entirely attributable to the copy-paste delay.
- Measured workaround: `st <timestamp + 26>` → residual offset ~0 s.

### Actions
- **Phase 4 fully validated** (E1–E4 + E2/E3 via Authy comparison). The TOTP engine is officially validated on ESP32.
- Next step: Phase 5 (X4 Pro bring-up) — pending receipt of the device.

---

---

## Session 2026-09-12 — Phase 5: X4 Pro received

### Setup
- Device: XTEINK X4 Pro Developer Edition received (order placed on xteink.com on 2026-08-29)
- SoC: ESP32-S3 confirmed (badge/tool) — matches press specs
- Flash size: not verified (esptool) at time of receipt

### Results
| Step | Result | Notes |
|-------|----------|-------|
| Receipt | ✅ | S3 SoC confirmed; accessories/box to be inventoried (pogo dock?) |
| esptool identification | ✅ | ESP32-S3 (QFN56) rev v0.2, 8 MB PSRAM on board, 16 MB quad flash, 40 MHz crystal, MAC 7c:0c:5f:41:9e:8c |
| Flashing path | ✅ | **native S3 USB (USB-Serial/JTAG) via pogo dock → COM5**, auto reset working |
| Probe flashed + boot | ✅ | 8 MB PSRAM detected and tested OK, USB-Serial/JTAG REPL operational (`probe>` prompt) |
| i2cguess (23 pairs) | ⚠️ Nothing found | Cause identified: I2C bus with no power rails — GPIO1 (peripheral rail) never enabled |
| Official pinout | ✅ | **FreeInk SDK** (`docs/xteink-x4pro-support.md`, confirmed on hardware): I2C bus SDA=39/SCL=38, GT911 0x5D (power GPIO2 active-LOW, INT=10, RST=4), BM8563 0x51, CW2017 **0x63** (corrected vs. 0x62), E-Ink SPI 12/11/13/18/14/6, SDMMC 41/42/40 + GPIO5, frontlight 8/9, buttons 0/7/3 |

### Decisions
- **No stock firmware backup** (user decision, 2026-09-12): risk accepted, justified — restoring to stock is out of scope, all the OEM dump info has already been published by FreeInk (pinout, sequences, CW2017 BATINFO, partitions). app1 is probably still intact if a recovery is ever needed.

### Actions
- Enhanced probe: `rails` command (GPIO1=H, GPIO2=L, GPIO5=L) and `rtc` command (BM8563 read)
- hardware-specs.md rewritten with the FreeInk-confirmed pinout
- Next validation: `rails` → `i2cscan 39 38` (expecting 0x51 + 0x63 + 0x5D) → `rtc`

---

## Session 2026-09-12 (evening) — Phase 5: I2C bus validated

### Setup
- `x4pro-probe` probe enhanced: transactional `i2cscan` (2-byte read of reg 0x00, 100 ms), `gauge` (CW2017), `touchid` (GT911 reset dance)
- IDF v5.4.3, flashed via COM5 (native S3 USB + pogo dock)

### Results
| Step | Result | Notes |
|-------|----------|-------|
| rails (GPIO1=H, GPIO2=L, GPIO5=L) | ✅ | required before any scan |
| i2cscan 39 38 | ✅ | **0x14 (GT911), 0x51 (BM8563), 0x63 (CW2017)** — 3 devices |
| gauge (CW2017 @0x63) | ✅ | VERSION reg = 0x0F, **VCELL = 4365 mV**, SoC = 100% (battery full; factory BATINFO loaded) |
| touchid (GT911) | ✅ | Product ID "911" rev 0x00 **@0x14** |

### Hardware findings (to fold into specs)
1. **GT911 at 0x14 on our unit**, not 0x5D — the INT-low reset dance did not switch the address; 0x14 is the default Goodix address. The driver must try 0x14 first, then 0x5D.
2. **1-byte reads NACK on the IDF 5.4 I2C master driver** (both zero-byte `i2c_master_probe` AND 1-byte reads fail, reads ≥ 2 bytes succeed — confirmed: BM8563 read fine over 7 bytes while a 1-byte probe showed an empty bus). Any presence-detection must read ≥ 2 bytes.
3. False positive scans on pairs {0,2} and {4,0}: GPIO0 (Left button), GPIO2 (power touch), GPIO4 (touch RST) are control pins — scanning them as an I2C bus disturbs the real bus. Ignore them.
4. CW2017 SoC = 100% with VCELL 4365 mV → factory-loaded BATINFO profile, the formula mV = (raw×5+8)>>4 is correct.

### Issues encountered
- `gauge`/`touchid` not recognized after the first reflash: functions present but **forgot to register them** in the `app_main` block → fixed.
- Transactional scan finds 0 devices: 1-byte / 25 ms probe systematically NACKs → switched to 2 bytes / 100 ms.

### Actions
- I2C bus **fully validated**: RTC ✅, gauge ✅, touch ✅
- Remaining for Phase 5: E-Ink (first display), SD, buttons, frontlight, charging
- Community source found: CrossPoint firmware (crosspoint-reader) supports the X4Pro (SSD1677 + GT911 confirmed), built on the same freeink-sdk → reference for init sequences

---

---

## Session 2026-09-12 (night) — Phase 5: E-Ink UC8279 identified + GT911 touch working

### Setup
- `x4pro-probe` probe (IDF v5.4.3, flash via COM5)
- References: clone of the FreeInk driver `Uc8279X4Driver.cpp` (`_ext/`), FreeInk doc `xteink-x4pro-support.md` (`_ext/`), Staars/GT911_ESP32 `GoodixFW.h` config table (`_ext/`), user's own `_ext/touch_gt911/` driver (ESP3D, Sunton 8048S050C — no register table, relies on self-load)

### Results
| Step | Result | Notes |
|-------|----------|-------|
| einkprobe (bit-bang identification) | ✅ | `VER = 00 0F 68 00 00` → **UC8279** (CHIP_VER 0x0F, LUT_VER 0x68), UltraChip batch — not an SSD1677 nor UC8179 |
| einkucinit + einkuc 0-5 (UC8179 sequence) | ⚠️ partial | display working but wrong sequence (PSR 3F/0A, BTST, inverted rows, no gate offset) |
| einkucinit + einkuc 0-5 (corrected UC8279 sequence) | ✅ | patterns displayed FULL GC with no BUSY timeout |
| touch silent despite the documented dance (10/10/100) | ❌ | `touchinfo`: **0x8047 = 0x00**, resolution 0×0, everything zero → self-load never happened |
| corrected dance (POR held under reset + exact µs values) | ❌ | 0x8047 still 0x00 → **this unit will never self-load** (empty factory OTP config) |
| writing config in normal mode | ❌ | I2C ACK but ignored (0x8047 reads back 0x00 after reset) |
| writing config in CONFIG UPDATE mode (INT low at POR) | ✅ | `[A]` readback: `81 E0 01 20 03`; `[B]/[B2]`: stable in RAM for ≥ 1 s |
| config persistence (power-cycle) | ❌ | `[C]`: 0x00 after rail power-off → **no writable config flash** on this variant |
| **touch scan with RAM config, no reset** | ✅ | `[C2]`: real points coming through, X ∈ 0-480, Y ∈ 0-800 (raw portrait), e.g. (404,657), (2,696), (50,11) |

### Key findings (to fold into specs)

**E-Ink UC8279** (exact ref. `Uc8279X4Driver.cpp`, `_ext/`):
1. Init: PSR `0x37 0x4D`, TRES 800×600, GSST 0, PFS 0x20, **PLL (0x30) = 0x0E — X4 Pro only**, gate scan 0x02. **No BTST or PWS** (PWR/VDCS stay in OTP/MTP).
2. Stream plan: **120 blank rows first** (gateOffset, gates 0-119 not visible), then 480 rows **in direct order** y=0→479, bytes as-is (ROWREV/XMIRROR disabled — "hardware-confirmed upright"), then a blank pad up to 600.
3. FULL refresh: CDI (0x50) = **1 byte 0x97** (cdiBwFull); CCSET 0x02; TSSET 0x1E; **PON + wait idle; PSR (0x00) = `0x17 0x4D` (0x37 & 0xDF, REG clr → OTP) BETWEEN PON and DRF** (PON reloads MTP: only the PSR writes issued after PON get latched); DRF 0x12; wait for BUSY. No idle CDI restore.
4. Old UC8179 port: the 2nd CDI byte (0x07) that was being sent was interpreted as DSLP (deep sleep) — spurious side effect eliminated.

**GT911**:
5. **Self-load impossible on this unit**: 0x8047 reads 0x00 after any compliant dance → empty OTP config. Either the stock firmware worked around it... or the stock touch panel was dead — never tested before flashing.
6. **CONFIG UPDATE mode mandatory to write the config**: INT low at POR (RST low + INT low before powering the rail on), INT held low during the write, released afterward. In normal mode, writes @0x8047 are acknowledged then ignored.
7. **Checksum**: sum of the 185 bytes (0x8047..0x80FF) ≡ 0 (mod 256). Formula validated against the Lenovo table from `GoodixFW.h`; the `g911xOrig` one is **corrupted in the Staars file** (total 0x2C) — always recompute it.
8. **No persistence**: the config lives in RAM only as long as the GPIO2 rail holds. Chosen architecture: **upload on every boot** (dance → if 0x8047==0x00 → upload 480×800 config + fresh → scan). Re-upload after any RST reset/rail power-off.
9. **Tricky bugs**: `pdMS_TO_TICKS(2)`/`(8)` = **0 tick** (FreeRTOS tick = 10 ms) → glitched RST pulse; use `esp_rom_delay_us` instead. POR must happen with **RST asserted**, otherwise the address/state ends up inconsistent (chip seen at 0x14 instead of 0x5D).
10. **Home pad not working yet**: bit 0x10 never comes through — the key zone registers (0x8093+) are zero in the adapted config. Needs configuring (key map + key area), or the pad may come through as a regular point instead — pad geometry to be confirmed.

### Measurements
- Adapted config: version 0x81, X=480 (0x01E0), Y=800 (0x0320), based on `g911xOrig`, recomputed checksum 0x04.
- Raw points observed: (404,657), (348,417), (394,662), (2,696), (316,313), (81,613), (50,11) — consistent with a 480×800 range.

### Actions
- **GT911 touch working** (scan + points) → remaining: 4-corner protocol test (fix swapXY/flipX/flipY), Home key zone, integrate config upload into the driver's init
- **E-Ink UC8279** sequence corrected in `eink_test.c` → remaining: validate the reference frame orientation (expected black 200×100 block at top-left)
- Update `hardware-specs.md` (UC8279 + GT911 update-mode sections)
- The `touchcfg` command remains the reference tool for replaying the upload; to be integrated into `gt911_begin` next

---

## Session 2026-09-13 (00:55) — E-Ink orientation + 4 corners + Home pad

### Context
Continuation of bring-up. Three manual validations requested (reference
frame orientation `einkuc 0`, 4-corner touch protocol, Home pad) — probe
firmware unchanged (`einkucinit`/`einkuc 0`/`touchcfg` commands from the
previous day).

### Results

**1. E-Ink orientation — ⚠️ transposed scan (90° CW rotation), not a mirror.**
Reference frame `einkuc 0` (fb: 200×100 block top-left + horizontal band
in the middle):
- the block appears **100 wide × 200 tall at the top RIGHT**
- the horizontal fb band appears **VERTICAL**
→ two independent measurements (inverted aspect ratio + verticality)
force `fb(x,y) → user(799-y, x)`. The FreeInk batch streams direct rows
"upright," so our unit differs (software rotation on the stock side, or
a panel sub-variant). Still open: this mapping does not make user px
0..319 addressable (gates 600..919 don't exist) — either the visible
window is limited (to verify: is all-black a full screen or a 480×480
square?), or the register init is incomplete for this batch.

**2. 4-corner touch — DEFINITIVE mapping.** Touch order: TL, TR, BR, BL.
Raw GT911: (475,80) (476,660) (52,661) (48,58).
→ **swapXY = true, invert_y (post-swap) = true, invert_x = false.**
Verified: all 4 corners land exactly on their physical positions.

**3. Home pad — no event at all** (neither a point nor bit 0x10).
Confirmed: it's a GT911 **touch key** (Crosspoint RE: stock reads
0x814E & 0x10), key zones 0x8093+ are zero in our config → pad silent
until the key areas are configured.

### Code added (x4pro-probe probe)

| Addition | File | Purpose |
|---|---|---|
| `eink_uc_show_pattern_mode(p, mode)` + `uc_stream_plane_mode()` | `eink_test.c` | stream modes 0=raw / 1=mirrorX / 2=rot90CW |
| pattern 6 (asymmetric 4-corner markers + H bar) | `eink_test.c` | visually discriminate orientation |
| `einkuc2 <mode> <pattern>` command | `main.c` | test the 3 modes without reflashing |
| `touchkeys <k0..k3> <area> [sec]` command | `main.c` | key enable (0x804D bit4) + 4 candidate X values + Y key area, live listening on 0x814E-0x815D |

GT911 key hypotheses (validated via struct + GoodixFW.h tables):
key[4] @idx76-79 = X of the touches (1 byte, probable unit res/256),
keyArea @idx80 = shared Y, levels 0x40/0x30, keySens 0x55/0x50,
keyRestrain 0x27; candidate defaults for a bottom landscape bezel
(raw X 0..48 → ~X/2, center Y 400 → 128).

### Tests pending (next flash)
1. `einkucinit` then `einkuc2 0 6`, `einkuc2 1 6`, `einkuc2 2 6` →
   describe marker positions for each mode; `einkuc2 2 2` → full-screen
   black or a square?
2. `touchkeys` (defaults) → press the Home pad → does key bit 0x10 go
   high? If not, iterate: `touchkeys 21 21 21 21 128`, then other X/area
   values.

### Addendum 01:34 — Home pad resolved (a touch point, not a key)

Re-test with `touchcfg` + pressing the Home pad: the pad reports a
**normal touch point** at raw **(36,479)** (user ≈ (550,443) bottom
bezel), n=1 then n=2 (wide contact area). Key bit 0x10 NEVER goes high.

**Conclusion**: on this unit the Home pad does NOT use the GT911 key
mechanism (unlike the Crosspoint RE on another variant). Chosen
architecture: **software zone** in the driver (raw rx ∈ [0,70],
ry ∈ [380,580] → Home, debounce, accept n≥1). The `touchkeys` command
stays in the code but isn't needed. (The no-detection at 00:55 was a
pressure/timing artifact.)

Still open: E-Ink orientation (`einkuc2`, pending a flash).

### Addendum 02:01 — orientation is NOT static: the PSR latch changes the scan

Test `einkuc2 1 6` on a fresh boot (rails → einkucinit): markers show a
**180° rotation** (small square BR, horizontal rect BL, vertical rect
TR, big square TL) — identical to yesterday's session. But software
mode 1 = mirror X, and yesterday's raw mode had proven a hardware
mirror X → expected composition = upright image. **Contradiction
impossible with static hardware.**

Reconstruction from the 4 sessions:
- **1st display after init**: scan = mirror X + 120-gate offset
  (markers mirrored horizontally, aspects preserved, full-screen frame).
- **Subsequent displays** (same boot or next boot): scan = mirror X
  **+ mirror Y** (180° rot) + 120 offset kept; the "all black" 60/40 =
  theoretical 75/25 (blank pad gates pushed to the bottom by the row
  reversal).

Root cause: after PON, the PSR was latched in **OTP mode** (`0x17 0x4D`,
REG clr → MTP settings) — the scan then switches to the panel's MTP
registers, different from the host config. The 1st display was still
scanning with the host config (mirror X); every one after that used
MTP (rot180). Hence the total inconsistency of last night's rapid-fire
tests.

**Coded fix** (`eink_test.c`, `eink_uc_show_pattern_mode`): FULL
rewrite of the init registers (PSR `0x37 0x4D` REG=1 + TRES + GSST +
PFS + PLL + gate-scan) between PON and DRF, on every display. Each DRF
therefore scans with the same explicit host config, independent of MTP.

**Validation test** (after reflash): `einkucinit` then `einkuc2 1 6`
**twice in a row** → both screens must be identical and upright
(A small square top-left, B horizontal rectangle top-right, C vertical
rectangle bottom-left, D big square bottom-right, horizontal bar
centered). Then `einkuc 2` → full-screen black.

### Session 11:08 — stability achieved, final transform = software mirror Y

After reflashing with the "rewrite host registers between PON and DRF"
fix:
- `einkuc 2` → **full-screen black** ✓ (full-bleed, complete refresh)
- `einkuc2 1 6` **twice in a row** → **identical** screens: big square
  TL, vertical TR, horizontal BL, small square BR, centered vertical
  line (= 180° rotation). Colors invert during the refresh then settle
  back — normal GC16 behavior.

**Complete model validated** (write and scan each have their own flip
depending on host/OTP state):

| write-state | scan-state | net hardware |
|---|---|---|
| host | OTP (yesterday's 1st display) | mirror X |
| OTP | OTP (yesterday's subsequent ones) | mirror Y |
| host | host (current fix) | mirror Y |

→ With a stable host config: **hardware = mirror Y** ⇒ the software
transform that yields an upright image = **mirror Y** (row order
reversed, bytes unchanged). Added to the code: mode 3 (mirror Y) in
`uc_stream_plane_mode` + PSR `0x37` write **before the stream**
(locks the W_host write path regardless of prior history).

**Validation pending**: `einkuc2 3 6` twice in a row → upright image
identical on both screens (A small square TL, B horizontal TR, C
vertical BL, D big square BR, horizontal bar centered).


### Session 12:18 — ✅ DEFINITIVE ORIENTATION: MTP scan (PSR 0x17), mode 0 = upright image

Chain of events from the morning (fully triaged, not to be reopened):

1. **11:21** — crash with `einkuc2 3 6`: gray screen, task_wdt on IDLE0.
   Actual cause #1 (fixed): `uc_wait_idle`/`wait_busy` used
   `vTaskDelay(pdMS_TO_TICKS(2))` and `(5)` = **0 tick** (FreeRTOS tick
   = 10 ms) → IDLE0 starved during the long BUSY waits. Fixed → delays
   ≥ 10 ms. False lead: the PSR write before the stream (removed, then
   found to be irrelevant — it's the DRF that matters, not the
   stream).
2. **11:39** — watchdog fixed but **systematic BUSY DRF timeout** (even
   mode 1, validated at 11:08). Timeout raised 8→20 s + POF recovery +
   power-cycle messages.
3. **11:58** — identical after a **full power cycle** (unplugged 30 s,
   cold reboot) → rules out residual state. Re-reading the stock driver
   (`_ext/Uc8279X4Driver.cpp`): **the only difference** = stock writes
   PSR `0x37 & 0xDF` = **`0x17` (REG=0 → scan with MTP registers)**
   between PON and DRF; we were writing `0x37` (REG=1 → host registers).
   Switched to 0x17.
4. **12:11** — `einkuc2 0 6`: refresh **completes cleanly (~2-4 s)**.
   Image = mirror X of the expected description → new transform
   hypothesis to work out.
5. **12:18** — recalibration using the actual pattern 6 coordinates
   (fb drawn in **native 800×480 landscape**; the reader is held in
   480×800 portrait): what the user sees in mode 0 = **pure 90° CW
   rotation, no mirror** = exactly the stock firmware's behavior.
   Mode 1 verified (software mirror X, now obsolete). **3 strictly
   identical displays** → stable scan.

**Final verdict**:
- DRF = always an MTP scan (PSR `0x17` post-PON). ⚠️ PSR `0x37`
  (REG=1) at DRF = full GC that NEVER completes on this UC8279 v0.2
  (BUSY LOW > 20 s, gray screen, POF with no effect, reproduced from
  cold boot). Lock coded + documented in `hardware-specs.md`.
- fb in **native landscape 800×480**, raw stream (blank gates 0-119,
  480 fb rows, blank pad); no software transform.
- The portrait fb reported by pattern 6 yesterday (mirror X / mirror Y)
  was an artifact of the unstable REG=1 config — the entire mirror
  decision tree is now obsolete.
- **Hardware bring-up for Phase 5 complete.** Remaining: frontlight
  colors not tested in detail (P2, out of TOTP scope).


### Session 12:38 — ✅ Orientation confirmed a second time (arrow test)

`einkuc2 0 7` (new pattern 7: native +x arrow + native TL square):
**black arrow pointing DOWN + square at top-RIGHT** = exactly the
expected 90° CW rotation. Reproduces the 12:18 verdict with a
non-symmetric marker. **Hardware bring-up + orientation: closed.**


### Session 15:39 — 🔴 Factory boot loop (TG0WDT, Saved PC ROM) — DIAGNOSIS IN PROGRESS

Flashing `x4pro_factory` via flash_mgr → boot loop:
`rst:0x7 (TG0WDT_SYS_RST)`, `Saved PC:0x400454d5` (ROM), crashes during
the ROM loader's `load:` phase → the second-stage bootloader never
starts. Zero bootloader logs (console on UART0, not connected on the
native USB dock).

Hypotheses and facts:
- Bootloader identical between both variants (cmp OK) → the difference
  lies in what gets loaded afterward, or in the flash mode.
- **Only DIO is proven on this unit** (x4pro-probe, which booted fine).
  QIO was forced in sdkconfig.defaults — never validated until now.
- Bootloader hook ruled out as a direct cause (max wait ~5.4 s <
  WDT 9 s).

Fixes applied (root + factory sdkconfig.defaults):
DIO, `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`,
`CONFIG_BOOTLOADER_LOG_LEVEL_DEBUG=y`, DEBUG LOG (max+default).
⚠️ sdkconfig frozen → clean_board mandatory before rebuild.

Next: clean → rebuild → reflash factory → collect logs.


#### Resolution 16:45 — ✅ 3 stacked causes identified and fixed

1. **esptool patches the flash mode header on flashing**: `flash_mgr`
   was passing `--flash-mode qio` (stale
   `boards/x4pro/flash_params.json`) → the freshly built DIO
   bootloader was reflashed as QIO on the flash chip.
   → `flash_params.json` switched to `dio`. The build had been DIO from
   the start (header byte 0x02, checked against the reference probe
   bootloader — esptool mapping: qio=0, dio=2).
2. **sdkconfig frozen** (factory 11:36, app 11:41): the
   USB-Serial/JTAG console + DEBUG LOG defaults were ignored by
   olddefconfig. The interactive clean was NOT deleting the sdkconfig →
   clean_variant fixed in `common.py` (+ blueprint template). Side
   effect: the 16:20 build finally actually applied the USB console →
   broke the `main.c` build (unconditional UART REPL) → REPL made
   conditional (`esp_console_new_repl_usb_serial_jtag` vs `_uart`).
3. **Flash mode mapping**: qio=0x00 / dio=0x02 (esptool) — the "QIO"
   read from the header on the first dump was actually DIO (table was
   inverted).

**State:** factory+app builds green, DIO binaries, flash map meta=dio,
sdkconfigs regenerated (USB-Serial/JTAG console + DEBUG). Remaining:
reflash + on-device boot validation.


### Session 17:00 — 🟠 First factory validation on device (4 findings)

Factory boot OK (DIO + USB logs). Full flow discovered:
- **The "crash" on power is not a crash**: `RTC_SW_CPU_RST` =
  esp_restart() after selecting "Boot app0" → otadata restored →
  **the app starts** (8 MB PSRAM OK). The ADR-007 rescue works
  end-to-end.
- **Landscape instead of portrait**: the probe had concluded that mode
  0 (raw stream) = correct orientation — the memory fb is native
  landscape, mounted portrait. The factory was drawing in fb
  coordinates → rotated content. Gfx fix: UI in portrait 480x800
  coordinates, put_pixel transposes fb_x = uy, fb_y = 479 - ux
  (mirror X INCLUDED — fb(0,0) displays top-RIGHT, per the 12:38 arrow
  measurement; without the mirror the text would be reversed).
- **Touch dead**: code matches the validated sequence, BUT the factory
  logs were compiled out (ENABLE_FACTORY_DEBUG_LOG=OFF) → no
  visibility. Turned ON (diag). Next flash will tell "touch: OK/ABSENT."
- **Full GC refresh + inversion** on every navigation: the MTP LUT
  (REG=0) forces the GC16 waveform. Partial = host LUT (REG=1) — this
  is what was freezing in the probe. To experiment with in the probe
  BEFORE porting (methodology).

Fixes applied: portrait gfx transpose, portrait touch mapping
(pt.x=raw_x, pt.y=raw_y), factory logs ON. Factory build green, flash
map meta=dio. Next test: flash the x4pro_factory variant.

### Session 18:20 — 🔧 Static app splash (Phase 8c slice 1)

Request: a static page to know where we are when leaving the factory.
- `boards/x4pro/app/` component: eink.c + font8x16.c copied from the
  factory (proven), splash.c with minimal gfx and the validated
  portrait transpose. Rails: periph ON, touch/SD OFF (full BSP in 8.4).
  After refresh: POF (persistent image at zero power draw, principle
  F-19).
- `board_config.cmake`: EXTRA_COMPONENT_DIRS (before project()).
- **Weak symbol trap**: the weak hook in main.c was never pulled from
  the static archive (the weak symbol satisfies the reference, nm
  showed W). Fixed: extern declaration only, strong implementation
  mandatory (explicit link error otherwise). nm shows T. App build
  green.

### Session 21:00 — ✅ Splash validated on device + font x2

- App splash seen on hardware (portrait OK, POF OK, REPL OK
  afterward).
- Long power press "screen change" = factory → "Boot app0" selection
  → otadata restored → reboot into the app + splash. **Nominal
  behavior** (RTC_SW_CPU_RST = voluntary esp_restart, not a crash).
- Font x2 (16x32) for factory + splash (user request: 8x16 too small).
  Menu layout adjusted (30 char max/line, 60 px item, shortened
  footer). Factory+app builds green.

### Session 21:45 — 🔧 Deep sleep: normal wake + wake into factory

Request: deep sleep support to validate both wake flows.
- `power_mgr`: `power_mgr_init()` logs the wake cause (EXT1 = Power
  button / cold boot) and configures EXT1 wake on GPIO3 (RTC IO,
  ANY_LOW).
- `main.c`: power_mgr init happens first (wake log always present);
  wake via Power = splash skipped (ADR-009 contract, straight-to-app);
  REPL command `sleep` = sleep screen then `power_mgr_shutdown()`.
- `splash.c`: `board_sleep_screen_show()` — "SLEEPING / Power = wake"
  screen, POF after refresh. Factory combo intentionally absent
  (DECISIONS §16). An e-ink failure does NOT block entering sleep.
- Wake → factory flow: handled by the bootloader hook (GPIO7 held) —
  the app never sees this case, test = Power+Right at wake.
- Pitfalls: `esp_log` component renamed `log` in IDF 5.5; transient
  xtensa GCC ICE on esp_lcd_panel_rgb.c (segfault, harmless — vanishes
  on ninja retry); `board_sleep_screen_show` declared after cmd_sleep =
  implicit declaration (-Werror).
- App build green (fresh installer/x4pro_app). To test: flash
  x4pro_app, `sleep` in REPL, Power wake (EXT1 log, no splash),
  Power+Right wake (factory boot).

### Session 21:45 — 🔧 Follow-up on 21:21 test: diagnosis + button/layout fixes

Analysis of the user's log (fragment):
- The "unreadable deep sleep screen / weird fonts" (point 3) = the APP
  SPLASH seen DURING the full GC16 refresh (3-4 s inversion phase)
  after "Boot app0" from the factory. The app had NO button handler at
  all: power did nothing (points 4-5 = app running, not an actual deep
  sleep).
- Root cause for points 4-5: deep sleep was only reachable via the
  REPL `sleep` command — the power button was not handled in the app.

Fixes applied (factory + app builds green at 21:36):
- Factory: "Active: factory" + "Default: app0" on 2 lines (x2 width),
  layout re-spaced (menu y=196), on-screen touch indicator "T:OK/KO"
  (diag without serial — file-static s_touch_ok).
- App: power_button_task (stack 4096, prio 5): power long press ≥ 1.5 s
  -> sleep screen + deep sleep; power long press + Right held ->
  esp_restart during the combo -> bootloader hook -> factory. Full
  ADR-009 contract.
- power_mgr_shutdown: re-arm EXT1 right before esp_deep_sleep_start
  (idempotent, guarantees the wake source).
- Splash: footer commands updated (sleep added).

To test: flash x4pro_factory then x4pro_app. Factory menu readable +
T:OK/KO. App: long power press = "SLEEPING" screen then sleep; short
power press at wake = app (EXT1 log, splash skipped per contract);
long power press + right = factory. Touch: report T:OK or T:KO + log
"touch: OK/ABSENT".

### Session 22:05 — 🔧 Fast DU refresh (RE of the stock freeink-sdk FW)

Symptom: every navigation = "2 refreshes" (flash inversion then image).
Diagnosis: A SINGLE draw_menu + A SINGLE DRF per press (edge-detected
buttons). The "double image" = the full GC16 MTP waveform: a clear
phase (whole-screen inversion) then drawing the new image. Inherent
to a full refresh.

Reference extracted: freeink-sdk Uc8279X4Driver.cpp (RE of the stock
FW, cloned in tmp_ref/freeink/). Key discovery: the stock fast DU does
NOT use a host LUT (REG=1 — that's what was freezing the UC8279 in the
probe): it's the OTP waveform selected via TSSET 0x5A + CDI 0xD7 +
FULL PTL window (PTIN with no PTL = a DU that scans without
developing, measured on hardware at 443 ms with no image). OLD plane =
previous frame (differential diff), DTM1 resynced after each refresh.

Implementation (factory/main/eink.c):
- eink_display_fb_fast(): byte-exact stock sequence (CDI 0xD7, CCSET
  0x02, TSSET 0x5A, PFS 0x20, gate scan 0x02, idempotent PON, PTIN+full
  PTL with +120 offset, PSR 0x17 between PON and DRF, DRF, wait,
  PTOUT, DTM1 resync=fb). 48 KB prev buffer in PSRAM.
- Policy: full GC if there's no prev (1st display/boot) or every
  EINK_FAST_BUDGET (10) fasts; counter reset by every full refresh.
- Idempotent PON (s_screen_on), eink_power_off sets the flag to false.
- gfx_flush_fast(); menu navigation (Left/Right) = fast, boot/status =
  full GC. Factory build green at 22:05.
To test: menu navigation = ~0.5-1 s with no flash; after 10 navs a
full GC (flash) clears the ghosts.

### Session 22:15 — 🔧 Phase 8b: Argon2id benchmark calibration (X4 Pro)

Context: S3R8 = 320 KB internal DRAM -> the Argon2id m=8 MiB work area
(ADR-004) can ONLY exist in octal PSRAM. Speed penalty unknown ->
hardware calibration before implementing secret_store (ROADMAP 8.2).

Library choice: Monocypher 4.0.2 (crypto_argon2, CRYPTO_ARGON2_ID) —
single audited file, Argon2id v1.3. Vendored in
test_apps/x4pro-argon2-bench/.
Benchmark (build green 22:20, build_bench.py reuses the common.py env):
 0. free heap/PSRAM + PSRAM memcpy bandwidth (context).
 1. CROSS-CHECK VECTOR pin="123456" salt=00..0f m=2 MiB t=1 p=1, 32-byte key.
    Expected on the PC side (argon2-cffi v19, Type.ID, memory_cost=2048):
    898e76d4bcda52614ecfd2961847493e80246c0ea7c6c33b227e819d03a40a65
    The benchmark prints the key on the ESP32 side; strict comparison
    of the 64 hex characters.
 2. Matrix m {1,2,4,8} MiB x t {1,2,4,8}, p=1, median of 3 runs.
 3. Internal DRAM reference 256 KiB t=4 (quantifies the PSRAM penalty).

For the user to do: flash (idf.py -p COMx flash from an IDF 5.5.5
terminal), monitor, report: vector (match?) + matrix + bandwidth.
Target: 0.5-1.0 s per PIN unlock attempt.

### Session 23:00 — 🔧 Follow-up on 22:39 test: DU ghosting, otadata spam, invisible wake, touch diag

User feedback (factory 22:17 / app 22:18 builds, tested 22:17-22:39):
1. Nav DU better but ghosting persists: the deselected selection bar
   goes black -> gray -> white without ever returning to its initial
   state; each row ends up a different color.
2. Home touch pad still has no effect on the factory side.
3. Long power press in the app = a sequence of refreshes then a page
   with traces of the previous splash (full GC ghosting), font judged
   unreadable.
4. Short power press during deep sleep "does nothing": the log does
   show the wake (EXT1) though — it was INVISIBLE by contract (splash
   skipped on wake).
5. Power+Right during deep sleep -> switches to factory: CONFIRMED
   WORKING (full bootloader hook log).

Factory log analysis (full capture this time):
- Boot 22:17:15: I2C probe 0x14 NACK at t=1196 (expected), read of
  0x5D SUCCESSFUL (no second NACK) -> the GT911 responds. Nothing
  after that (0x8047, config upload) was logged -> unknown status,
  hence the T:KO/T:OK indicator.
- otadata valid at boot ("Only otadata[0] is valid", t=1906) then
  became "ota data invalid" ~31 s later, with no identifiable cause in
  the code (hooks.c checks: erases otadata ONLY if Right is held at
  boot; the app never touches otadata). Unresolved — diagnostics
  added.
- action_sd_flash VERIFIED: SD mounted + file opened + size validated
  BEFORE any erase -> no bricking bug (initial fear unfounded).

Fixes applied (factory + app builds green at 23:00):
- Ghosting: EINK_FAST_BUDGET 10 -> 4 (factory/main/eink.c). The
  selection bar = a large moving black area = worst case for DU.
- otadata spam: "Active:" label cached ONCE at boot
  (cache_active_ota_label) — removes the esp_ota_get_boot_partition
  call on every redraw (2 lines of esp_ota_ops log per navigation) +
  the flash slowdown on every redraw.
- boot_partition: otadata re-read after esp_ota_set_boot_partition +
  ESP_LOGI (diagnostic for the mid-session corruption).
- Touch: always-on ESP_LOGI/ESP_LOGE at every step (probe addr, cfg
  version, upload result) — next session will give the exact failing
  step instead of guessing.
- App: splash is now also shown at wake (interim until the UNLOCK
  screen in 8.4) — the power wake is now visible.

Not addressed (deferred): splash-to-sleep-screen traces/ghost (likely
improved by the 8c BSP unification); sleep screen font to re-validate
after the font fix; Argon2id benchmark still pending a flash (cross
vector + matrix).

To test: flash x4pro_factory + x4pro_app. Factory: nav = reduced
ghosting (full GC every 4 fasts); boot log = "touch probe OK, addr
0x5D" + "host config uploaded" or the failing step; "active partition:
factory". Boot app0 -> log "boot -> 'app0' (otadata reread: app0)".
App: short power press at wake = splash visible. Power+Right = factory
(already OK).

### Session 23:05 — 🔧 Spec: Power = Cancel in the factory

User request: Power (GPIO3) is NOT an OK — it's a Cancel that reboots
into the default partition. Only the Home pad (touch) confirms.
- BTN_3 dispatch -> action_cancel(): log + status + eink_power_off +
  esp_restart (otadata restored at boot -> reboot = default
  partition).
- Home pad -> execute_selected_action() directly (no longer goes
  through BTN_3).
- Footer: "L/R=nav Home=OK Power=Cancel" (25 char x2 = 400 px, fits
  within 440 px; the old "Home/Power=OK" was misleading).
- Header + loop comment updated. CONSEQUENCE: touch KO = no
  confirmation possible at all in recovery -> the touch diagnostic
  becomes critical.
Factory build green at 23:06. To test: L/R nav, Home confirms, Power
cancels (reboot into default app).

### Note 2026-09-16 — Power+Right combo invalid (do not trust the entries above)

The sessions above (all dated 2026-09-13) report the Power+Right combo
as "CONFIRMED WORKING" during deep sleep. Retested on hardware on
2026-09-16: holding Power+Right together from sleep NEVER wakes the
device (no log, no reaction), regardless of how long it is held —
unlike Power alone, which wakes it every time. Root cause not
identified on the firmware side (an issue upstream of the bootloader
hook). Combo abandoned, replaced with Power alone plus a duration
measurement (< 10 s = normal sleep/wake, >= 10 s = factory) — see
ADR-009 (2026-09-16 amendment) in docs/ROADMAP.md. The historical
entries above are kept as-is (session log), but no longer describe the
current behavior.
