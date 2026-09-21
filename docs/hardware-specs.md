# XTEINK X4 Pro — Hardware & Drivers Sheet (MySafeFob project reference, ex-standalone-TOTP)

> **Status (2026-09-13)**: ✅ **Hardware fully brought up on our unit** —
> SoC/flash, I2C bus, RTC, gauge, GT911 touch (4-corner mapping + Home pad validated),
> UC8279 E-Ink (display + final orientation validated), SD, frontlight, buttons,
> charging. **Hardware bring-up complete.**
>
> Each section indicates its level of proof: ✅ validated on our unit / 🔧 confirmed by FreeInk
> (not re-tested on our side) / ⚠️ to be validated.

---

## 🔧 SoC (✅ validated — esptool + probe)

| Parameter | Value |
|---|---|
| **Model** | ESP32-S3R8 (QFN56), revision v0.2 |
| **Cores** | 2 × Xtensa LX7, 240 MHz |
| **SRAM** | 512 KB |
| **PSRAM** | **8 MB octal, embedded** (tested OK at boot) |
| **Flash** | **16 MB** quad, 3.3 V (detected by esptool; the factory bootloader's binary header claimed 2 MB — ignore this, actual size is 16 MB) |
| **Crystal** | 40 MHz |
| **Wi-Fi MAC** | 7C:0C:5F:41:9E:8C |
| **Connectivity** | Wi-Fi 2.4 GHz + BLE (radio disabled outside of time sync — ADR-001) |
| **USB** | GPIO19 = D−, GPIO20 = D+ (native S3 USB, via pogo dock) — **do not repurpose** |
| **Flash/debug** | Native USB-Serial/JTAG via pogo dock → COMx, auto-reset works (`idf.py -p COMx flash monitor`) |

## 🗺️ Full GPIO map (consolidated view)

| GPIO | Function | Direction | Notes |
|------|----------|-----------|-------|
| 0 | **Left** button | IN pull-up | ⚠️ strapping pin — not held LOW at boot |
| 1 | Peripheral rail | OUT | **HIGH = ON**, held HIGH permanently |
| 2 | **Touch** power-enable | OUT | **LOW = ON** (active-low) — held LOW during operation |
| 3 | **Power** button | IN pull-up | active-LOW; does not cut power (GPIO only) |
| 4 | **Touch RST** (GT911) | OUT | reset dance, see § Touch |
| 5 | **SD** power-enable | OUT | **LOW = ON** — pulse HIGH 80 ms → LOW 120 ms on mount |
| 6 | E-Ink **BUSY** | IN | **active-LOW** (BUSY_N) |
| 7 | **Right** button | IN pull-up | active-LOW |
| 8 | **Cool** frontlight | OUT (LEDC) | PWM 25 kHz 10-bit, active-HIGH |
| 9 | **Warm** frontlight | OUT (LEDC) | PWM 25 kHz 10-bit, active-HIGH |
| 10 | **Touch INT** (GT911) | IN/OUT | address at reset + config update mode (see § Touch) |
| 11 | E-Ink MOSI | OUT | SPI |
| 12 | E-Ink SCLK | OUT | SPI 10 MHz |
| 13 | E-Ink CS | OUT | |
| 14 | E-Ink RST | OUT | |
| 18 | E-Ink DC | OUT | |
| 19 / 20 | USB D− / D+ | — | do not repurpose (console) |
| 21 | **Charge** detection | IN | active-HIGH (raw level = charging) |
| 26-32 | Flash bus | — | **forbidden** |
| 33-37 | PSRAM bus | — | **forbidden** |
| 38 | I2C **SCL** | OD | shared bus, 400 kHz |
| 39 | I2C **SDA** | OD | shared bus, 400 kHz |
| 40 | SD **DAT0** | SDMMC | slot 1, 1-bit |
| 41 | SD **CLK** | SDMMC | 40 MHz |
| 42 | SD **CMD** | SDMMC | |
| 43 / 44 | UART TX / RX | — | reserved for debug |

## 🔌 I2C bus #0 — SDA=39 / SCL=38 (✅ validated)

| Address | Device | Status |
|---------|--------------|--------|
| 0x51 | **BM8563** RTC | ✅ validated |
| 0x63 | **CW2017** fuel gauge | ✅ validated |
| 0x5D / 0x14 | **GT911** touch | ✅ validated (scan) |

> ⚠️ **I2C IDF 5.4 driver anomaly observed**: **1-byte NACK** reads occur systematically
> (zero-byte probe and 1-byte read fail; ≥ 2 bytes succeed). Any presence detection
> must read ≥ 2 bytes. GT911 is invisible to a standard scan (16-bit addressing).

## 📟 E-Ink display (✅ validated — UC8279 identified, patterns displayed)

| Parameter | Value |
|---|---|
| **Panel** | 4.3" 800×480 B/W, native landscape |
| **Controller** | **UC8279** — identified by `einkprobe`: response `00 0F 68` (CHIP_VER 0x0F, LUT_VER 0x68), UltraChip batch |
| **Internal addressing** | 800×600 addressed, visible window = **gates 120..599** (gateOffset 120) |
| **BUSY** | **active-LOW** (0 = busy, 1 = idle) |
| **Power supply** | Internal booster — **no GPIO enable** |
| **SPI** | 2 (FSPI), 10 MHz, write-only, manual CS; DMA limited to 32,768 B/transfer → chunk at 16 KB |

### Validated UC8279 sequence (ref. `_ext/Uc8279X4Driver.cpp`)

**Init** (after RST):
```
PSR  (0x00) = 37 4D          ; REG=1 (external LUT) at init
TRES (0x61) = 03 20 02 58    ; 800 x 600
GSST (0x65) = 00 00 00 00
PFS  (0x03) = 20
PLL  (0x30) = 0E             ; X4 Pro ONLY (stock X4C: no-op)
GATE_SCAN (0xE1) = 02
; NO BTST or PWS — PWR/VDCS/BTST remain panel-programmed (OTP/MTP)
```

**Streaming a plane** (DTM1 0x10 = OLD / DTM2 0x13 = NEW), 100 bytes/line:
```
120 white lines (0xFF)            ; gates 0..119 not visible
480 framebuffer lines, direct order y=0→479, bytes as-is
white padding up to 600 gates
```

**FULL refresh** (exact order — RE'd from stock firmware):
```
CDI    (0x50) = 97            ; 1 byte ONLY (cdiBwFull) — no 2nd byte!
CCSET  (0xE0) = 02
TSSET  (0xE5) = 1E            ; full GC
PON    (0x04) + wait for BUSY HIGH
PSR    (0x00) = 17 4D         ; 0x37 & 0xDF: REG clr → OTP waveform.
                              ; MANDATORY between PON and DRF (PON reloads the MTP,
                              ; only PSR writes after PON are latched)
DRF    (0x12) + wait for start (BUSY drop, 50 ms) then end (BUSY HIGH, 8 s timeout)
; no CDI restore afterward (absent from stock)
```

**Power off**: POF (0x02) + wait idle.

### Batch identification (to be done BEFORE any init)
`einkprobe` (bit-bang SPI, cmd 0x70/0x71 half-duplex on MOSI=11) → read the
identification register. Response `00 0F 68` = UC8279. Possible variants by batch:
SSD1677 (original units) / UC8179 (recent batches) / UC8279 (our unit) — same
glass, same pinout, different sequences. Reference drivers in freeink-sdk.

### ✅ Orientation (FINAL — measurements 2026-09-13)

**Validated config: MTP scan (PSR `0x17` post-PON, REG=0) + raw stream (mode 0)
= 90° CW hardware rotation, identical to stock firmware.** The physical panel
is natively **800×480 landscape**, mounted in portrait 480×800 in the reader — the
framebuffer must therefore be drawn in native landscape. No software transform needed.

⚠️ **NEVER write PSR `0x37` (REG=1) between PON and DRF**: on this UC8279
(rev v0.2) the full GC never completes (BUSY stuck LOW > 20 s, screen grey,
POF has no effect — reproduced from cold boot, measurements 11:21→11:58). The DRF must
scan with the MTP registers, as stock does
(`psr0 & 0xDF = 0x17`).

Measurement history (context, do not reopen): with REG=1 the scan was
unstable between the 1st display (X mirror) and subsequent ones (X+Y mirror) because PON
reloads the MTP; the 11:21 failure was initially wrongly attributed to a
PSR write before the stream (real culprit: `pdMS_TO_TICKS(5)` tick bug = 0 tick
→ task_wdt IDLE0, fixed). The software transform modes 1-3 of
`uc_stream_plane_mode` are kept for reference but are obsolete.

Tool: `einkuc2 <0-3> <0-6>`, pattern 6 = native asymmetric markers
(A 40×40 TL, B 120×60 TR, C 64×120 BL, D 104×100 BR, H bar 400×20).
**Validation 12:18**: mode 0 → markers exactly in their expected positions in portrait
+ expected vertical bar; 3 displays strictly identical, DRF ~2-4 s
without timeout. `einkuc 2` → full black screen (✅).

## 👆 GT911 Touch (✅ validated — real touch points)

| Signal | GPIO | Notes |
|--------|------|-------|
| SDA/SCL | 39/38 | shared bus, 400 kHz |
| INT | **10** | LOW at reset → address 0x5D; **held LOW at POR → CONFIG UPDATE mode** |
| RST | **4** | |
| Power | **GPIO2 active-LOW** | held LOW permanently during operation (RAM config = volatile) |

### Critical findings (unit, 2026-09)

1. **No self-load**: 0x8047 (config version) reads **0x00** after any compliant
   dance → factory-empty OTP config. The chip responds on I2C (Product ID "911" @0x8140) but does not
   scan without a config. → **host upload mandatory on every boot**.
2. **CONFIG UPDATE mode required to write**: in normal mode, writes to @0x8047
   are acknowledged but then **ignored**. Working sequence:
   ```
   RST low + INT low BEFORE powering on the rail (GPIO2)  ; POR under reset, update mode
   rail on 50 ms → release RST → 60 ms                    ; INT ALWAYS low
   write 185 bytes @0x8047 (config + checksum @0x80FF)
   write 0x01 @0x8100 (config_fresh)                       ; INT low during the ENTIRE write
   release INT (input+pullup)
   ```
3. **Checksum**: sum of the 185 bytes (0x8047..0x80FF) ≡ 0 (mod 256).
   (Ref. `_ext/GoodixFW.h`; register map `_ext/gt911_structs.h`. ⚠️ the checksum of
   `g911xOrig` is corrupted in the Staars source — always recompute it.)
4. **No persistence**: the config lives in RAM only → must be re-uploaded after every
   RST reset or GPIO2 rail cutoff. Driver implementation: `dance → read 0x8047 →
   if 0x00: upload → scan`.
5. **Validated coordinates**: status 0x814E (bit7 ready, bit4 home, count nibble),
   points 0x8150 (8 B/pt, X-lo in byte 0). **Raw portrait: X 0-480, Y 0-800** →
   **swapXY = true** for the 800×480 display (validated with real points at 00:02).
   **4-corner test at 00:55 (order TL, TR, BR, BL)**: raw (475,80) (476,660) (52,661)
   (48,58) → final mapping: **swapXY = true, invert_y = true** (post-swap,
   i.e. raw X inverted), invert_x = false. Verified: all 4 corners land exactly right.
   **Reconfirmed 2026-09-15** (factory build 12:25, host config uploaded):
   TL (475,17) TR (473,612) BR (49,640) BL (37,31) — same proportions,
   same mapping (`raw_x` drives the inverted screen Y axis, `raw_y` drives the
   direct screen X axis). ⚠️ `touch.c` currently does NOT apply this to `pt.x`/`pt.y`
   (a comment there wrongly claims the raw data is "already portrait"); no
   consequence for now (only the Home zone, in raw coordinates, is
   used) but must be fixed before any positional tap is used in the UI
   (Phase 8.4).
6. **Pitfalls**: `pdMS_TO_TICKS(2)/(8)` = 0 tick (FreeRTOS tick 10 ms) → RST glitch;
   use `esp_rom_delay_us` instead. POR must happen **with RST asserted**, otherwise the state/address is
   inconsistent. Detection: read ≥ 2 bytes (see I2C anomaly).
7. **Config in use**: version 0x81, X=480 (E0 01), Y=800 (20 03), `g911xOrig`
   base adapted; reference implementation: `cmd_touchcfg` (x4pro-probe).
8. **Home pad = ordinary touch point** with our uploaded config (bit 0x10 of
   0x814E never set with it). The freeink SDK (Ghidra reverse engineering
   of the OEM firmware, `xteink-x4pro-support.md`) states that on the stock
   firmware — which uses **self-load**, never a host upload — Home IS a
   real GT911 capacitive key (`0x814E & 0x10`). The factory driver
   (`touch.c`) tests both: the software zone (fallback) AND the key bit
   (active if self-load ever succeeds).
   → **Software zone recalibrated 2026-09-15** (real log, factory build
   12:25, host config uploaded): repeated presses on the physical Home pad →
   raw **(x=2..8, y=693..696)**, very stable → raw rx ∈ [0,70], ry ∈ [660,720].
   ⚠️ Replaces the 01:34 measurement (36,479) which no longer matches anything
   under the config currently in use — likely a different config or chip state
   between the two measurement sessions. If the host upload or
   the dance changes again, revalidate this zone before treating it as
   fixed.
9. **User reference driver**: `_ext/touch_gt911/` (ESP3D) — good base
   for the final driver (probing 0x5D/0x14, INT as IRQ hint + polling, swap/invert),
   to be combined with the config upload described above.

## 🕐 BM8563 RTC (✅ validated)

| Parameter | Value |
|---|---|
| I2C address | 0x51 |
| Backup | Main battery — **keeps time across flashes** (validated) |
| Reading | regs 0x02-0x08 in BCD (see `cmd_rtc`) |

## 🔋 CW2017 gauge (✅ validated)

| Parameter | Value |
|---|---|
| I2C address | 0x63 |
| VCELL | regs 0x02/0x03, 14-bit, **mV = (raw·5 + 8) >> 4** — 4365 mV measured, formula validated |
| SoC | reg 0x04 — 100% measured (factory BATINFO profile loaded); returns 0% without a profile |
| Charge | GPIO21, active-HIGH |

## 💡 Dual warm/cool frontlight (✅ validated)

| Channel | GPIO | LEDC | Polarity |
|-------|------|------|----------|
| Cool/white | 8 | ch 4 | active-HIGH |
| Warm | 9 | ch 5 | active-HIGH |

PWM 25 kHz, 10-bit. Cool/warm gamma validated (progressive ramp-up).

## 💾 SD card — native 1-bit SDMMC (✅ validated)

| Signal | GPIO |
|--------|------|
| CLK | 41 |
| CMD | 42 |
| DAT0 | 40 |
| Power | 5 (active-LOW, pulse on mount) |

Slot 1, 40 MHz, internal pull-ups. **16 GB card mounted, FAT32 readable, stock
directories visible (XTCACHE/XTDATA), MBR signature 55AA read** — see `cmd_sd`.

## 🔘 Buttons (✅ validated — digital, active-LOW, INPUT_PULLUP)

| Button | GPIO | Notes |
|--------|------|-------|
| Left | 0 | ⚠️ strapping pin: OK at runtime, not held at reset |
| Right | 7 | |
| Power | 3 | does not cut power (plain GPIO) |
| Home | — | via GT911 (§ Touch — to be validated) |

## 📋 Drivers to produce (Phase 8)

| Driver | Device | Interface | Priority | Code base |
|--------|-------------|-----------|----------|--------------|
| `eink_driver` | UC8279 (+ batch auto-detect) | SPI 10 MHz | P0 | § E-Ink sequence (validated) |
| `touch_driver` | GT911 + config upload | I2C 400k | P0 | `touch_gt911/` + `cmd_touchcfg` |
| `pmic/rails` | GPIO1/2/5 | GPIO | P0 (prerequisite) | `set_rails` probe |
| `rtc_driver` | BM8563 | I2C | P0 (TOTP) | `cmd_rtc` probe |
| `button_driver` | 3 buttons + Home | GPIO/GT911 | P1 | `cmd_btn` probe |
| `sd_driver` | 1-bit SDMMC | SDMMC | P1 (update/backup) | `cmd_sd` probe |
| `battery_driver` | CW2017 + charge | I2C + GPIO21 | P2 | `cmd_gauge`/`cmd_charge` |
| `backlight_driver` | warm/cool LED | LEDC 25k | P2 | ✅ done — `boards/x4pro/app/frontlight.c` (ADR-016, 2026-09-19) |

## ✅ Validation completed (state as of 2026-09-13)

- [x] esptool: S3R8 rev v0.2, PSRAM 8 MB, flash 16 MB, MAC
- [x] Flash path: native USB via pogo dock, auto reset
- [x] Rails GPIO1=H / GPIO2=L / GPIO5=L (universal prerequisite)
- [x] I2C bus 39/38: BM8563 @0x51, CW2017 @0x63, GT911 @0x5D/0x14
- [x] BM8563 RTC: reading + backup across flashes
- [x] CW2017: VCELL 4365 mV, SoC 100%, formula validated; charge GPIO21
- [x] UC8279 E-Ink: identification + init + patterns 0-5 displayed (FULL GC)
- [x] GT911 touch: config upload + scan validated (raw 480×800 points)
- [x] Left/Right/Power buttons (GPIO); cool/warm frontlight; 16 GB SD FAT32
- [x] Touch: 4-corner test → **swapXY=true, invert_y(post-swap)=true** (00:55)
- [x] Touch: ~~Home pad key zone~~ → **Home = raw touch point (36,479)**,
      software zone in driver (rx<70, ry 380-580) — validated 01:34
- [x] E-Ink: **stable** scan + **final** orientation (validated 12:18):
      PSR `0x17` (REG=0, MTP scan) between PON and DRF + raw stream = 90° CW
      rotation = stock orientation. **Forbidden: PSR `0x37` (REG=1) at DRF →
      full GC hangs (BUSY LOW > 20 s, grey screen)**. No software transform.

## 📦 Stock 16 MB partitions (reference — FreeInk dump)

| Label | Offset | Size |
|---|---|---|
| nvs | 0x009000 | 0x5000 |
| otadata | 0x00E000 | 0x2000 |
| app0 | 0x010000 | 0x7E0000 |
| app1 | 0x7F0000 | 0x7E0000 |
| spiffs | 0xFD0000 | 0x14000 |
| coredump | 0xFE4000 | 0x1C000 |

Stock = dual OTA, boots from **app1**: app0 @ 0x10000 = incorrect Arduino variant
(`ESP32S3_X4_TL`), app1 @ 0x7F0000 = the real X4 Pro firmware (`ESP32S3_X4_TL_SSD1677`,
`XTEink::SSD1677_800x480` / `GT911Driver`). Our probe (factory @ 0x10000) overwrote the
start of app0 — which was NOT the real firmware. **app1 is likely still intact.**
(phy_init @ 0xF000 overlaps the old otadata — stock recovery out of scope, no-backup
decision made 2026-09-12.)

---

*Sources: `freeink-sdk/docs/xteink-x4pro-support.md` (MIT, confirmed on hardware),
`_ext/Uc8279X4Driver.cpp`, `_ext/GoodixFW.h`, `_ext/touch_gt911/`, esptool, x4pro probe
(`test_apps/x4pro-probe`). Detailed log: `docs/test-log.md`.*
