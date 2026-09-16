# Board `m5paper_mono` — M5Stack M5PaperMono (STUB, ADR-008)

> **Status**: documentation only. **No code before the hardware is
> in hand** (out of stock at the time of the decision, 2026-09-13).
> The UI converges with the X4 Pro toward **480×800 portrait** — this is
> the project's natural porting target board.

## Specs (gathered from the M5Stack shop, 2026-09-13)

| Element | Value |
|---|---|
| Reference | M5PaperMono, SKU C153, $65 (out of stock) |
| SoC | ESP32-S3R8, 16 MB flash, 8 MB octal PSRAM, Wi-Fi 2.4 GHz |
| E-paper | **SSD1677 480×800 native portrait**, 4 grayscale levels |
| Touch | **FT6336G** (standard driver, unlike the X4 Pro's GT911) |
| RTC | RX8130CE |
| Battery | 1150 mAh (via M5PM1), M5IOE1 IO expander |
| Buttons | 2 user + power; microSD; USB-C |
| Extras (never used — air-gap) | NFC ST25R3916, LoRa SX1262, PDM mic, buzzer, IMU BMI270, RGB LED |

## Anticipated porting notes

- The `test_apps/x4pro-probe/` probe already contains an SSD1677 driver
  path (`eink_ssd` commands, busy=**HIGH** — inverse polarity from the
  UC8279): this is the basis for this board's future e-ink driver.
- Native **portrait** e-paper: no hardware rotation to handle (simplifies
  the driver compared to the X4 Pro).
- The radio extras (NFC/LoRa) stay disabled under the air-gapped model.

## To enable this board later

1. `boards/m5paper_mono/board_config.cmake` + `sdkconfig.defaults`
   (modeled on `boards/x4pro/`).
2. `boards/m5paper_mono/flash_params.json` + README updated with the
   actual measurements.
3. Drivers: SSD1677 (based on the probe's `eink_ssd`), FT6336G (existing
   driver in the IDF ecosystem), RX8130CE.
