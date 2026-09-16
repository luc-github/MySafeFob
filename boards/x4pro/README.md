# Board `x4pro` — XTEINK X4 Pro Developer Edition

Main board of the MySafeFob project. **Complete and validated hardware
bring-up** (2026-09-13) — the single reference for pins/drivers is
[`docs/hardware-specs.md`](../../../docs/hardware-specs.md) (at the repo
root). This README only summarizes and points there.

## Hardware summary

| Element | Value |
|---|---|
| SoC | ESP32-S3R8 (2×LX7 @240 MHz, 512 KB SRAM, **8 MB octal PSRAM**) |
| Flash | 16 MB quad, 3.3 V |
| E-paper | 4.3" 800×480 B/W, **UC8279** controller (SPI 10 MHz, BUSY active-LOW) |
| Touch | **GT911** I2C @0x5D (config upload mandatory on every boot) |
| RTC | **BM8563** @0x51 (battery backup, keeps time across flashes) |
| Gauge | **CW2017** @0x63 + charging on GPIO21 |
| Frontlight | dual warm/cool, LEDC 25 kHz 10-bit (GPIO8/9, active-HIGH) |
| SD | native 1-bit SDMMC (CLK=41, CMD=42, DAT0=40, power GPIO5 active-LOW) |
| Buttons | Left=GPIO0 (⚠ strapping), Right=GPIO7, Power=GPIO3 — active-LOW |
| Home | GT911 software touch zone (raw point ~(36,479)) |
| Rails | GPIO1 permanently HIGH (peripherals), GPIO2 LOW (touch), GPIO5 LOW (SD) |

## Critical constraints (validated, do not reopen)

- **E-ink**: between PON and DRF, PSR must be `0x17` (REG=0, MTP scan).
  **NEVER** `0x37` (REG=1) at DRF → full GC stalls (BUSY LOW > 20 s,
  gray screen). Orientation = 90° CW hardware rotation, native
  800×480 landscape framebuffer, raw stream, no software transform.
- **GT911**: no self-load (empty OTP config) → host upload of 185 bytes
  @0x8047 (checksum) + 0x01 @0x8100 on every boot, in CONFIG UPDATE
  mode (POR under reset RST=GPIO4 with INT=GPIO10 LOW). Volatile config:
  re-upload after every reset. Mapping: swapXY=true, invert_y(post-swap)=true.
- **I2C** (IDF 5.4 anomaly): any presence read must read ≥ 2 bytes.
- **pdMS_TO_TICKS < 10 ms = 0 tick** → use `esp_rom_delay_us` in
  timing-critical sequences (notably touch reset).

## Reference driver

The `test_apps/x4pro-probe/` probe contains the validated bring-up code:
commands `einkucinit`/`einkuc`/`einkuc2` (UC8279), `touchcfg`/`touchinfo`
(GT911 + config upload), `cmd_rtc`, `cmd_gauge`, `cmd_sd`, `cmd_btn`,
`set_rails`. The final drivers for task 8c are extracted from these.

## Toolchain

Target IDF **5.5.5** (installation in progress on the user's side). The
probe runs on 5.4.3 — S3 bootloader hook porting to be verified at the
1st compilation (ADR-007).
