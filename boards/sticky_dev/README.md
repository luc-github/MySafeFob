# Board `sticky_dev` — Seeed reTerminal Sticky (development target)

**Not a product target.** This board exists to work around the X4 Pro's
unreliable pogo/magnetic USB dock, which disconnects on the slightest
handling and makes live logging impossible while exercising touch/button
flows. The Sticky has a real USB-C port and a non-openable unibody case,
so it becomes the dev/debug platform for app-layer logic
(`settings_store`, `battery`, `ui_nav` state machine) while the X4 Pro
remains the reference product target.

**Status: not bring-up validated yet.** Pin mapping below comes from a
third-party reference project (MIT), not from physical probing on this
repo's hardware — treat it as a starting point, not ground truth, until
verified the same way `docs/hardware-specs.md` was for the X4 Pro.

## Hardware summary

| Element | Value |
|---|---|
| SoC | ESP32-S3, 8 MB PSRAM, 32 MB QSPI flash |
| E-paper | 3.97" 800×480 B/W, **SSD1677** controller (different from the X4 Pro's UC8279 — needs its own driver) |
| Touch | **GT911** I2C — same controller as the X4 Pro, driver should port with only a pin remap |
| Gauge | **BQ27220** (different from the X4 Pro's CW2017 — needs its own driver, low priority for a dev board) |
| Buttons | single AI/power button only — no Left/Right, nav UX will need to be simplified for this target |
| Frontlight | none (e-ink only, no equivalent to the X4 Pro's warm/cool LEDC frontlight) |
| USB | native USB-C, charge + data, stable during handling |

## Reference pin mapping (unvalidated, from third-party source)

Source: [reterminal-sticky-2048-eink-game](https://github.com/Lukilyy/reterminal-sticky-2048-eink-game)
(MIT), built against ESP-IDF v5.4, tested on physical reTerminal Sticky
production hardware by its author.

| Function | GPIO |
|---|---|
| AI / power button | 4 |
| External-power detect | 9 |
| E-paper SPI | 13–18 |
| GT911 I2C | 2, 3 |
| GT911 enable / interrupt / reset | 42, 21, 41 |
| BQ27220 I2C | 0, 1 |
| Charger enable | 39 |
| Buzzer PWM | 48 |
| Power hold / lock | 45, 46 |

## Bring-up plan

1. Flash the reference 2048 firmware as-is to confirm the physical unit
   is healthy and the pin mapping above is accurate.
2. Port the GT911 touch driver from `boards/x4pro/app/touch.c`, remapping
   pins only (I2C bus + enable/int/reset).
3. Write a minimal SSD1677 driver, using the reference project's
   `seeed_epaper` component as a study reference (not a direct copy —
   re-derive against this repo's e-ink abstraction).
4. Wire up `settings_store`/`battery`/`ui_nav` against this board's
   `hw_config.h` to validate app-layer logic with stable USB-C logging.
5. Defer: BQ27220 gauge driver, buzzer, frontlight-equivalent (none —
   out of scope), full Left/Right nav parity (single-button UX instead).
