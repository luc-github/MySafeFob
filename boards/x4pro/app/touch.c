/* 
 Project: MySafeFob  touch.c
  Copyright (c) 2026 Luc Lebosse. All rights reserved.

  This code is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
/**
 * @file touch.c
 * @brief MySafeFob App — X4 Pro GT911 touch driver.
 *
 * Identical copy of boards/x4pro/factory/main/touch.c (ADR-010: same
 * hardware, no board divergence expected) — kept independent from the
 * factory's per ADR-010 pt.3.
 *
 * Assembles the sequences validated on the x4pro-probe/main/main.c probe:
 *  - gt911_begin()      : POR dance under reset (self-load guaranteed)
 *  - cmd_touchcfg       : uploads 480x800 host config if 0x8047 == 0x00
 *  - cmd_touchdump/info : status + points reading, 4-corner mapping
 *
 * Config table: based on Staars/GT911_ESP32 GoodixFW.h (g911xOrig 1024x600)
 * adapted to 480x800 portrait; checksum recomputed at runtime (the original
 * table was corrupted — total 0x2C instead of 0x00).
 */
#include "touch.h"
#include "hw_config.h"
#include "i2c_bus.h"
#include "settings_store.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_log_workaround.h"

/* Distinctive tag (2026-09-18, was "touch"): makes every GT911 register
 * read stand out in a busy serial log while diagnosing "touch does
 * nothing" reports, instead of blending into other TAG="touch"-adjacent
 * lines. */
static const char *TAG = "GT911";

/* Axis-swap calibration (2026-09-18, 5-point crosshair hardware test,
 * see the touch_read() comment where these are used for the full
 * derivation): the touch grid's raw_x/raw_y are NOT the display's
 * logical x/y at all -- raw_y tracks the display's horizontal position
 * and raw_x tracks its vertical position (matching FreeInkUICore.h's
 * own touchToLogical() LandscapeClockwise case, which this driver had
 * never actually called). Two calibration points per axis, both
 * measured directly against on-screen crosshair targets (not screen
 * edges -- the crosshairs sit inset from the header/footer). */
#define TOUCH_X_RAWY_LO      25    /* raw_y at the LEFT crosshairs (logical x=20) */
#define TOUCH_X_LOGICAL_LO   20
#define TOUCH_X_RAWY_HI      690   /* raw_y at the RIGHT crosshairs (logical x=460) */
#define TOUCH_X_LOGICAL_HI   460

/* Y is NOT a single line end to end (2026-09-18, follow-up retest): a
 * 3rd calibration point at the center crosshair landed ~100px off the
 * straight-line prediction between top and bottom, while X's own center
 * point was within ~25px (consistent with plain tap imprecision, not a
 * real curve -- the two X half-ranges have almost identical slopes:
 * 0.650 vs 0.668). Y's two halves don't: -2.564 (top-to-mid) vs -0.870
 * (mid-to-bottom) -- a real, repeatable nonlinearity, not noise. X stays a
 * single line; Y's own fit was superseded 2026-09-24 -- see
 * kTouchYBreakpoints below, this was the first (2-segment) cut at the same
 * underlying nonlinearity, later found to need more than 2 segments. */
/* Linear interpolation/extrapolation between two calibration points,
 * clamped to [0, logical_max]. Shared by both axes above -- same math,
 * different points and a different logical_max (EINK_H-1 for X, since
 * DisplayTarget's Portrait rotation makes EINK_H==480 the logical
 * *width* here; EINK_W-1 for Y, same swapped-name situation). */
static int16_t touch_lerp(int16_t raw, int16_t raw_lo, int16_t logical_lo,
                          int16_t raw_hi, int16_t logical_hi, int16_t logical_max)
{
    int32_t v = logical_lo + (int32_t)(raw - raw_lo) * (logical_hi - logical_lo) /
                (raw_hi - raw_lo);
    if (v < 0) v = 0;
    if (v > logical_max) v = logical_max;
    return (int16_t)v;
}

/* Y-axis breakpoints, raw_x DESCENDING as logical_y increases (2026-09-24
 * rewrite -- was a 2-segment TOP/MID/BOTTOM fit). A dense single-tap sweep
 * down the vertical center of the screen (Settings > Touch Calibration,
 * temporarily repurposed as a raw-mapping tool) found raw_x staying almost
 * flat from y=440 to y=490 (322 -> 315, a real plateau, not noise -- 2
 * independent sweeps agreed within a few units) then dropping abruptly to
 * ~138 by y=500 -- a ~180-unit jump over just 10px, 15-20x steeper than
 * every other interval measured. No single linear scale+offset can
 * represent a plateau immediately followed by a cliff, which is exactly
 * why every attempt at a per-unit correction (touch_calibrate()) kept
 * getting its fitted scale rejected as unsafe (up to 3x out of bounds) --
 * it was trying to fit a straight line through a real discontinuity.
 * Likely cause (docs/touch-calibration-notes.md §2): this unit's GT911
 * runs a generic substitute config (`s_cfg_480x800` below, adapted from an
 * unrelated 1024x600 reference panel, since this unit's own OTP config
 * was blank) rather than X4 Pro's real factory config -- a config-blob
 * property can easily produce exactly this kind of non-uniform raw
 * response if its virtual sensitivity zones don't match this panel's real
 * electrode layout. Fixing that would need the real OEM config bytes
 * (not available); this piecewise table works around it in software using
 * the actual measured response instead. TOP and the y=640 point are the
 * original 2026-09-18 hardware-validated calibration, unchanged and still
 * consistent with the new sweep; the rest are new. */
typedef struct {
    int16_t raw_x;
    int16_t logical_y;
} touch_y_breakpoint_t;

static const touch_y_breakpoint_t kTouchYBreakpoints[] = {
    /* Header/upper region (2026-09-24, 2 sweeps at x=240, averaged): raw_x
     * falls ~0.5 units/px like the rest of the panel, flattening near the
     * top (~477) as it nears the ~87px dead zone. Before this, every raw_x
     * >= 415 clamped to y=240, so no tap above y~240 could ever land on
     * the Back/Settings buttons (y~88-144). */
    { 477,  90 },
    { 475, 100 },
    { 472, 115 },
    { 468, 130 },
    { 456, 150 },
    { 441, 180 },
    { 428, 210 },
    { 415, 240 },   /* TOP (2026-09-18, unchanged; today's sweep read ~412) */
    { 322, 440 },   /* refined MID (was raw_x=334 in the original 2-segment fit) */
    { 315, 490 },   /* plateau end -- raw_x barely moved since the point above */
    { 138, 500 },   /* post-jump -- raw_x dropped ~180 units over just 10px */
    { 104, 640 },   /* BOTTOM (2026-09-18, unchanged -- still consistent with the new sweep) */
    {  45, 750 },   /* extended range, beyond the original BOTTOM point */
};
#define TOUCH_Y_BREAKPOINT_COUNT \
    (int)(sizeof(kTouchYBreakpoints) / sizeof(kTouchYBreakpoints[0]))

/* Y-axis: picks which segment (see the breakpoint table above) raw_x
 * falls into, then interpolates within that segment only. Clamps to the
 * outermost breakpoints instead of extrapolating past them with whatever
 * slope the nearest segment happens to have. */
static int16_t touch_lerp_y(int16_t raw_x)
{
    if (raw_x >= kTouchYBreakpoints[0].raw_x) {
        return kTouchYBreakpoints[0].logical_y;
    }
    if (raw_x <= kTouchYBreakpoints[TOUCH_Y_BREAKPOINT_COUNT - 1].raw_x) {
        return kTouchYBreakpoints[TOUCH_Y_BREAKPOINT_COUNT - 1].logical_y;
    }
    for (int i = 0; i + 1 < TOUCH_Y_BREAKPOINT_COUNT; i++) {
        if (raw_x <= kTouchYBreakpoints[i].raw_x && raw_x >= kTouchYBreakpoints[i + 1].raw_x) {
            return touch_lerp(raw_x, kTouchYBreakpoints[i].raw_x, kTouchYBreakpoints[i].logical_y,
                              kTouchYBreakpoints[i + 1].raw_x, kTouchYBreakpoints[i + 1].logical_y,
                              EINK_W - 1);
        }
    }
    return kTouchYBreakpoints[TOUCH_Y_BREAKPOINT_COUNT - 1].logical_y;   /* unreachable given the clamps above */
}

/* -----------------------------------------------------------------------
 * Per-unit correction (2026-09-23) -- Settings > Touch Calibration's
 * guided 9-target sequence (ui_screen_touch_diag.cpp) fits a linear
 * (scale + offset) correction per axis on top of the fixed calibration
 * above, to absorb whatever per-unit deviation the 5-crosshair diagnostic
 * this replaced could only ever show, never correct. Cached in RAM
 * (loaded once by touch_init()) so touch_read()'s ~50Hz poll never touches
 * NVS -- same reasoning as this file's own I2C device-handle caching.
 * Defaults to identity (scale=1000, offset=0) until a run completes.
 * ----------------------------------------------------------------------- */
static int32_t s_cal_scale_x1000 = 1000, s_cal_offset_x = 0;
static int32_t s_cal_scale_y1000 = 1000, s_cal_offset_y = 0;

static void load_calibration(void)
{
    s_cal_scale_x1000 = (int32_t)settings_store_get_touch_cal_scale_x();
    s_cal_offset_x = settings_store_get_touch_cal_offset_x();
    s_cal_scale_y1000 = (int32_t)settings_store_get_touch_cal_scale_y();
    s_cal_offset_y = settings_store_get_touch_cal_offset_y();
}

static int16_t apply_cal_x(int16_t x)
{
    int32_t v = ((int32_t)x * s_cal_scale_x1000) / 1000 + s_cal_offset_x;
    if (v < 0) v = 0;
    if (v > EINK_H - 1) v = EINK_H - 1;
    return (int16_t)v;
}

static int16_t apply_cal_y(int16_t y)
{
    int32_t v = ((int32_t)y * s_cal_scale_y1000) / 1000 + s_cal_offset_y;
    if (v < 0) v = 0;
    if (v > EINK_W - 1) v = EINK_W - 1;
    return (int16_t)v;
}

bool touch_set_calibration(int32_t scale_x1000, int32_t offset_x,
                            int32_t scale_y1000, int32_t offset_y)
{
    s_cal_scale_x1000 = scale_x1000;
    s_cal_offset_x = offset_x;
    s_cal_scale_y1000 = scale_y1000;
    s_cal_offset_y = offset_y;
    settings_store_set_touch_cal_scale_x((uint32_t)scale_x1000);
    settings_store_set_touch_cal_offset_x(offset_x);
    settings_store_set_touch_cal_scale_y((uint32_t)scale_y1000);
    settings_store_set_touch_cal_offset_y(offset_y);

    /* settings_store_set_*() is void -- every setter in this codebase is,
     * matching NVS's own fire-and-forget convention -- so a silent write
     * failure would otherwise be completely invisible here, and the
     * calibration screen would cheerfully report "saved" regardless
     * (2026-09-23 user question: "comment sait-on que la calibration est
     * sauvegardee?" -- honest answer at the time was "we don't check").
     * Read every value straight back from NVS and compare: a real
     * verification, not just trusting a void function returned. */
    bool persisted =
        settings_store_get_touch_cal_scale_x() == (uint32_t)scale_x1000 &&
        settings_store_get_touch_cal_offset_x() == offset_x &&
        settings_store_get_touch_cal_scale_y() == (uint32_t)scale_y1000 &&
        settings_store_get_touch_cal_offset_y() == offset_y;
    if (!persisted) {
        ESP_LOGE(TAG, "calibration write did not verify on read-back -- NOT reliably saved "
                 "(still applied for this session)");
    }
    return persisted;
}

void touch_get_calibration(int32_t *scale_x1000, int32_t *offset_x,
                           int32_t *scale_y1000, int32_t *offset_y)
{
    if (scale_x1000) *scale_x1000 = s_cal_scale_x1000;
    if (offset_x) *offset_x = s_cal_offset_x;
    if (scale_y1000) *scale_y1000 = s_cal_scale_y1000;
    if (offset_y) *offset_y = s_cal_offset_y;
}

void touch_reset_calibration(void)
{
    touch_set_calibration(1000, 0, 1000, 0);
}

/* Least-squares fit of expected = measured*scale + offset, over n point
 * pairs -- run once at the end of a 9-target calibration sequence, never
 * in a hot path, so plain double math is fine (this MCU has an FPU). */
static void linear_fit(const int16_t *measured, const int16_t *expected, int n,
                        double *out_scale, double *out_offset)
{
    double sum_m = 0, sum_e = 0, sum_me = 0, sum_mm = 0;
    for (int i = 0; i < n; i++) {
        sum_m += measured[i];
        sum_e += expected[i];
        sum_me += (double)measured[i] * expected[i];
        sum_mm += (double)measured[i] * measured[i];
    }
    double denom = (double)n * sum_mm - sum_m * sum_m;
    if (denom == 0.0) {
        *out_scale = 1.0;
        *out_offset = 0.0;
        return;
    }
    *out_scale = ((double)n * sum_me - sum_m * sum_e) / denom;
    *out_offset = (sum_e - (*out_scale) * sum_m) / (double)n;
}

/* Sanity bounds for a fitted correction, checked BEFORE it's ever applied
 * or persisted (2026-09-23 hardware incident: a desynced set of
 * (measured, expected) pairs -- see ui_screen_touch_diag.cpp's tap-cooldown
 * fix for the actual root cause -- flipped the fitted Y scale's sign;
 * touch_set_calibration() applied it immediately and unconditionally,
 * which clamped every subsequent tap's Y to 0 and made the touchscreen
 * unusable, INCLUDING for the rest of that same calibration run. A fit
 * this far from identity is never a real per-unit deviation on a panel
 * that already went through hardware-validated calibration in touch_lerp()
 * -- it's a measurement/pairing problem, and applying it would do more
 * harm than just keeping the previous (or, mid-run, identity) correction
 * and reporting failure. */
static bool calibration_fit_is_sane(int32_t scale_x1000, int32_t offset_x,
                                    int32_t scale_y1000, int32_t offset_y)
{
    if (scale_x1000 < 500 || scale_x1000 > 2000) return false;
    if (scale_y1000 < 500 || scale_y1000 > 2000) return false;
    if (offset_x < -EINK_H || offset_x > EINK_H) return false;
    if (offset_y < -EINK_W || offset_y > EINK_W) return false;
    return true;
}

int32_t touch_calibrate(const int16_t *measured_x, const int16_t *measured_y,
                         const int16_t *expected_x, const int16_t *expected_y, int n,
                         bool *out_persisted)
{
    double scale_x, offset_x, scale_y, offset_y;
    linear_fit(measured_x, expected_x, n, &scale_x, &offset_x);
    linear_fit(measured_y, expected_y, n, &scale_y, &offset_y);

    int32_t scale_x1000 = (int32_t)lround(scale_x * 1000.0);
    int32_t scale_y1000 = (int32_t)lround(scale_y * 1000.0);
    int32_t offset_x_i = (int32_t)lround(offset_x);
    int32_t offset_y_i = (int32_t)lround(offset_y);

    if (!calibration_fit_is_sane(scale_x1000, offset_x_i, scale_y1000, offset_y_i)) {
        ESP_LOGE(TAG, "fitted calibration (scale %ld/%ld offset %ld/%ld) out of sane bounds "
                 "-- REJECTED, not applied", (long)scale_x1000, (long)scale_y1000,
                 (long)offset_x_i, (long)offset_y_i);
        if (out_persisted) {
            *out_persisted = false;
        }
        return -1;
    }

    bool persisted = touch_set_calibration(scale_x1000, offset_x_i, scale_y1000, offset_y_i);
    if (out_persisted) {
        *out_persisted = persisted;
    }

    int32_t max_err = 0;
    for (int i = 0; i < n; i++) {
        int32_t cx = (int32_t)measured_x[i] * scale_x1000 / 1000 + offset_x_i;
        int32_t cy = (int32_t)measured_y[i] * scale_y1000 / 1000 + offset_y_i;
        int32_t dx = cx - expected_x[i];
        int32_t dy = cy - expected_y[i];
        int32_t err = (int32_t)lround(sqrt((double)(dx * dx + dy * dy)));
        if (err > max_err) max_err = err;
    }
    return max_err;
}

/* GT911 registers */
#define GT_REG_CFG_VER   0x8047
#define GT_REG_PRODUCT   0x8140
#define GT_REG_STATUS    0x814E
#define GT_REG_POINTS    0x8150
#define GT_REG_CFG_FRESH 0x8100

static i2c_master_bus_handle_t s_bus = NULL;
static uint8_t s_addr = 0;
/* Persistent device handle for the resolved GT911 address, opened once
 * touch_init() succeeds. reg_read()/reg_write() used to open+remove a fresh
 * i2c_master_dev_handle_t on EVERY call (2-3 times per ~20ms poll cycle in
 * lv_port_indev.c's input_sampler_task) -- correctly freed each time, not a
 * leak, but constant driver-internal alloc/free churn that added latency and
 * heap fragmentation risk on the touch hot path. Kept open for the entire
 * device lifetime instead; only the address-probing code (gt911_probe_address(),
 * which must try two candidate addresses before either is known good) still
 * opens its own short-lived handles. */
static i2c_master_dev_handle_t s_dev = NULL;

/* Host 480x800 config (185 bytes @0x8047, [184] = recomputed checksum). */
static const uint8_t s_cfg_480x800[185] = {
    0x81, 0xE0, 0x01, 0x20, 0x03, 0x0A, 0x0C, 0x20, 0x01, 0x08, 0x28, 0x05, 0x50, 0x3C, 0x03, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x89, 0x2A, 0x0B, 0x2D, 0x2B,
    0x0F, 0x0A, 0x00, 0x00, 0x01, 0xA9, 0x03, 0x2D, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x21, 0x59, 0x94, 0xC5, 0x02, 0x07, 0x00, 0x00, 0x04, 0x93, 0x24, 0x00, 0x7D,
    0x2C, 0x00, 0x6B, 0x36, 0x00, 0x5D, 0x42, 0x00, 0x53, 0x50, 0x00, 0x53, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12, 0x14, 0x16, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x04, 0x06, 0x08, 0x0A, 0x0F, 0x10, 0x12, 0x16, 0x18, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22,
    0x24, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   /* [184] = checksum, recomputed */
};

static void rails_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << RAIL_PERIPH_PIN) | (1ULL << RAIL_TOUCH_PIN) |
                        (1ULL << RAIL_SD_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(RAIL_PERIPH_PIN, 1);
    gpio_set_level(RAIL_TOUCH_PIN, 0);   /* touch on (active-low) */
    gpio_set_level(RAIL_SD_PIN, 1);      /* SD pulse start */
    vTaskDelay(pdMS_TO_TICKS(80));
    gpio_set_level(RAIL_SD_PIN, 0);      /* SD on (active-low) */
    vTaskDelay(pdMS_TO_TICKS(120));
}

/* SHARED I2C bus (i2c_bus.c) — RTC/gauge use it too. Never destroy it
 * here (i2c_del_master_bus) or it would break the other drivers; on a
 * touch failure, we simply give up locally (s_addr stays 0), the shared
 * bus survives. */
static esp_err_t i2c_bus_open(void)
{
    return i2c_bus_get(&s_bus);
}

static esp_err_t i2c_dev_open(uint16_t addr, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_bus, &cfg, out);
}

static esp_err_t touch_dev_ensure(void)
{
    if (s_dev) return ESP_OK;
    return i2c_dev_open(s_addr, &s_dev);
}

/* 16-bit register read (>= 2 bytes: 1-byte reads get NACKed,
 * an IDF 5.4 I2C driver quirk validated on this hardware). */
static esp_err_t reg_read(uint16_t reg, uint8_t *out, int len)
{
    if (touch_dev_ensure() != ESP_OK) return ESP_FAIL;
    uint8_t addr16[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};
    return i2c_master_transmit_receive(s_dev, addr16, 2, out, len,
                                       pdMS_TO_TICKS(100));
}

static esp_err_t reg_write(uint16_t reg, const uint8_t *data, int len)
{
    if (touch_dev_ensure() != ESP_OK) return ESP_FAIL;
    uint8_t buf[2 + 185];
    if (len > 185) len = 185;
    buf[0] = (uint8_t)(reg >> 8);
    buf[1] = (uint8_t)(reg & 0xFF);
    memcpy(buf + 2, data, len);
    return i2c_master_transmit(s_dev, buf, (size_t)(2 + len), pdMS_TO_TICKS(200));
}

/* Full POR dance (FreeInk xteink-x4pro-support.md): self-load /
 * config upload is only guaranteed if POR happens with RST asserted.
 * Validated pitfalls: exact delays in us (pdMS_TO_TICKS(2)/(8) = 0 tick),
 * INT low BEFORE rail-off for address 0x5D and config mode. */
static void gt911_por_dance(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << TOUCH_RST_PIN) | (1ULL << TOUCH_INT_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(TOUCH_RST_PIN, 0);   /* RST low, held throughout the POR */
    gpio_set_level(TOUCH_INT_PIN, 0);   /* INT low -> address 0x5D */
    gpio_set_level(RAIL_TOUCH_PIN, 1);  /* rail off */
    esp_rom_delay_us(50000);
    gpio_set_level(RAIL_TOUCH_PIN, 0);  /* rail on: POR with RST asserted */
    esp_rom_delay_us(50000);

    esp_rom_delay_us(2000);             /* RST low 2 ms (doc) */
    gpio_set_level(TOUCH_RST_PIN, 1);   /* release */
    esp_rom_delay_us(8000);             /* INT low 8 ms after release (doc) */
    gpio_set_direction(TOUCH_INT_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(TOUCH_INT_PIN);
    esp_rom_delay_us(60000);            /* wait for config self-load */
}

/* Host config upload in CONFIG UPDATE mode (INT held LOW during the
 * write). Necessary on this batch: OTP config blank (0x8047 == 0x00). */
static bool gt911_upload_config(void)
{
    /* Enter update mode via POR: INT low + RST low BEFORE the rail
       powers on (whichever comes first, measured 23:37). */
    gpio_set_direction(TOUCH_INT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(TOUCH_INT_PIN, 0);
    gpio_set_level(TOUCH_RST_PIN, 0);
    gpio_set_level(RAIL_TOUCH_PIN, 1);
    esp_rom_delay_us(50000);
    gpio_set_level(RAIL_TOUCH_PIN, 0);  /* rail on, POR under reset+INT low */
    esp_rom_delay_us(50000);
    gpio_set_level(TOUCH_RST_PIN, 1);   /* release RST, INT low on the rising edge */
    esp_rom_delay_us(60000);

    uint8_t buf[185];
    memcpy(buf, s_cfg_480x800, 185);
    uint32_t sum = 0;
    for (int i = 0; i < 184; i++) sum += buf[i];
    buf[184] = (uint8_t)((0x100 - (sum & 0xFF)) & 0xFF);  /* total == 0 mod 256 */

    if (reg_write(GT_REG_CFG_VER, buf, 185) != ESP_OK) return false;
    uint8_t fresh[1] = {0x01};
    if (reg_write(GT_REG_CFG_FRESH, fresh, 1) != ESP_OK) return false;
    esp_rom_delay_us(200000);

    /* Release INT: the RAM config stays active without a reset (decisive C2 test). */
    gpio_set_direction(TOUCH_INT_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(TOUCH_INT_PIN);
    esp_rom_delay_us(100000);

    /* Check: the config must read back 0x81. */
    uint8_t ver[2] = {0};
    if (reg_read(GT_REG_CFG_VER, ver, 2) != ESP_OK) return false;
    return ver[0] == 0x81;
}

/* RST/INT-ONLY dance, without power-cycling the touch rail (the rail is
 * already powered once by rails_init()). Same timings (2/8/60 ms) as
 * gt911_por_dance(), but WITHOUT the extra GPIO2 off/on power-cycle it
 * does. Reference: freeink-sdk (docs/xteink-x4pro-support.md, Ghidra RE
 * of the OEM firmware + confirmed on hardware) — on this controller the
 * internal config self-load works with THIS minimal dance; a
 * "0x8047 == 0x00" there is diagnosed as too-short a reset, not a blank
 * OTP. To be tried first before falling back to our dance + host upload
 * (already validated). */
static void gt911_self_load_dance(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << TOUCH_RST_PIN) | (1ULL << TOUCH_INT_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(TOUCH_RST_PIN, 0);   /* RST low */
    gpio_set_level(TOUCH_INT_PIN, 0);   /* INT low -> address 0x5D */
    esp_rom_delay_us(2000);             /* RST low 2 ms (doc) */
    gpio_set_level(TOUCH_RST_PIN, 1);   /* release */
    esp_rom_delay_us(8000);             /* INT low 8 ms after release (doc) */
    gpio_set_direction(TOUCH_INT_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(TOUCH_INT_PIN);
    esp_rom_delay_us(60000);            /* wait for config self-load */
}

/* Probe 0x5D / 0x14 (GT911 invisible to a classic scan: 16-bit
 * registers). Identified by reading the product ID (>= 2 bytes). */
static bool gt911_probe_address(void)
{
    uint8_t addrs[2] = {0x14, 0x5D};
    for (int a = 0; a < 2; a++) {
        i2c_master_dev_handle_t dev = NULL;
        if (i2c_dev_open(addrs[a], &dev) != ESP_OK) continue;
        uint8_t reg16[2] = {0x81, 0x40};
        uint8_t id[2] = {0};
        esp_err_t err = i2c_master_transmit_receive(dev, reg16, 2, id, 2,
                                                    pdMS_TO_TICKS(100));
        i2c_master_bus_rm_device(dev);
        if (err == ESP_OK) {
            s_addr = addrs[a];
            return true;
        }
    }
    return false;
}

bool touch_init(void)
{
    if (s_bus && s_addr) return true;

    /* Independent of the chip itself -- loaded here (once, whichever path
     * below succeeds or even if none does) rather than after each success
     * path so there's a single call site. settings_store_init() has
     * already run by the time this does (main.c, before board_ui_nav_task
     * starts touch_init() via lv_port_indev_init()). */
    load_calibration();

    rails_init();

    /* Attempt 1: "freeink-style" self-load (rail already powered, RST/INT
     * dance without power-cycle). If 0x8047 comes back non-zero, we keep
     * the auto-loaded config (and potentially the real GT911 capacitive
     * Home key, see touch_read()) without ever touching the host upload. */
    gt911_self_load_dance();
    if (i2c_bus_open() != ESP_OK) {
        ESP_LOGE(TAG, "i2c_bus_open FAILED (bus 39/38)");
        s_bus = NULL;
        return false;
    }
    if (gt911_probe_address()) {
        ESP_LOGI(TAG, "probe OK (self-load), addr 0x%02X", s_addr);
        uint8_t ver[2] = {0};
        if (reg_read(GT_REG_CFG_VER, ver, 2) == ESP_OK && ver[0] != 0x00) {
            ESP_LOGI(TAG, "self-load OK, cfg version 0x%02X (no upload)",
                     ver[0]);
            return true;
        }
        ESP_LOGW(TAG, "self-load FAILED (cfg version 0x%02X) -> fallback dance+upload",
                 ver[0]);
    } else {
        ESP_LOGW(TAG, "probe FAILED after self-load dance -> fallback dance+upload");
    }

    /* Attempt 2 (fallback already validated on this unit): full dance
     * with rail power-cycle + host config upload if needed. Drop attempt 1's
     * cached device handle first -- it was opened (by reg_read() above, via
     * touch_dev_ensure()) against attempt 1's s_addr, which attempt 2 may
     * resolve to a different address. */
    if (s_dev) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    s_addr = 0;
    gt911_por_dance();
    if (!gt911_probe_address()) {
        ESP_LOGE(TAG, "probe: no GT911 address responded (0x14/0x5D)");
        /* DO NOT destroy s_bus: shared bus (i2c_bus.c), RTC/gauge also
         * depend on it. We just give up locally. */
        return false;
    }
    ESP_LOGI(TAG, "probe OK (fallback), addr 0x%02X", s_addr);

    uint8_t ver[2] = {0};
    if (reg_read(GT_REG_CFG_VER, ver, 2) != ESP_OK || ver[0] == 0x00) {
        ESP_LOGI(TAG, "cfg version 0x%02X -> uploading host config",
                 ver[0]);
        if (!gt911_upload_config()) {
            ESP_LOGE(TAG, "config upload FAILED");
            return false;   /* the chip doesn't scan without config */
        }
        ESP_LOGI(TAG, "host config uploaded (480x800)");
    }
    return true;
}

/* Rate-limited heartbeat (2026-09-18): a full I2C failure on the status
 * read, or a chip that simply never sets bit7, previously produced ZERO
 * log output at all -- indistinguishable from "this function is never
 * even being called". Logging once a second either way (not on every
 * ~20ms poll) makes that distinction visible on the next hardware run:
 * silence here would mean touch_read() itself isn't running/reachable;
 * "I2C read FAILED" repeating would point at the bus/address, not the
 * touch logic; "status 0x00" repeating while actually pressing the
 * screen would mean the chip itself isn't reporting the contact. */
static int64_t s_last_heartbeat_us = 0;

touch_point_t touch_read(void)
{
    touch_point_t pt = { .pressed = false, .x = -1, .y = -1 };

    int64_t now = esp_timer_get_time();
    bool heartbeat = (now - s_last_heartbeat_us) >= 1000000;   /* 1 s */

    if (!s_bus || !s_addr) {
        if (heartbeat) {
            ESP_LOGW(TAG, "touch_read() called but NOT INITIALIZED (bus=%p addr=0x%02X)",
                     (void *)s_bus, s_addr);
            s_last_heartbeat_us = now;
        }
        return pt;
    }

    uint8_t st[2] = {0};
    esp_err_t err = reg_read(GT_REG_STATUS, st, 2);
    if (err != ESP_OK) {
        if (heartbeat) {
            ESP_LOGW(TAG, "status I2C read FAILED (err=%d)", err);
            s_last_heartbeat_us = now;
        }
        return pt;
    }
    if (!(st[0] & 0x80)) {   /* no new buffer since the last clear */
        if (heartbeat) {
            //ESP_LOGI(TAG, "status 0x%02X (idle, no new buffer)", st[0]);
            s_last_heartbeat_us = now;
        }
        return pt;
    }

    /* bits[3:0] of 0x814E = number of active touch points. 2026-09-18
     * finding: this used to be ignored, so a release event (buffer ready,
     * 0 points, per the GT911 status-register convention) was still
     * parsed as a touch and reported pressed=true with stale/garbage
     * point data -- release edges were only ever detected later, on
     * whatever poll happened to see "no new buffer" instead. Logged
     * unconditionally here (bounded rate: only fires on real chip
     * activity, i.e. while a finger is actually down) so a hardware run
     * can show exactly what the chip reports on every sample, including
     * ones the caller's own edge detection never turns into an action. */
    const uint8_t touch_count = st[0] & 0x0F;
    //ESP_LOGI(TAG, "status 0x%02X count=%d", st[0], touch_count);

    uint8_t pts[8] = {0};
    if (touch_count > 0 && reg_read(GT_REG_POINTS, pts, 8) == ESP_OK) {
        int16_t raw_x = (int16_t)(pts[0] | (pts[1] << 8));
        int16_t raw_y = (int16_t)(pts[2] | (pts[3] << 8));
        /* Home = software zone OR the real GT911 capacitive key (bit
         * 0x10 of 0x814E — freeink-sdk, Ghidra RE of the OEM firmware:
         * "the OEM keys off exactly 0x814E & 0x10"). The bit has never
         * come up with our uploaded config; if self-load
         * (gt911_self_load_dance) ever succeeds, it might work directly.
         * The two coexist, with no risk.
         * Zone RECALIBRATED 2026-09-15 (real log, factory build 12:25):
         * repeated presses on the physical Home pad -> raw (x=2..8, y=693..696),
         * very stable. The old measurement (36,479, doc 01:34) no longer
         * matches anything with the currently uploaded config -> replaced. */
        pt.home = (raw_x < 70 && raw_y >= 660 && raw_y <= 720) ||
                  (st[0] & 0x10) != 0;
        /* AXES SWAPPED (2026-09-18, 5-point crosshair hardware test --
         * superseded the previous "Y rescale + X flip" attempt, which
         * treated the two axes independently and never quite landed:
         * both a Settings-button tap and a Sleep-button tap missed by
         * 20-70px after that fix). Five crosshairs drawn on the touch
         * diagnostic screen (4 corners + center, all inset from the
         * header/footer) showed unambiguously that raw_y tracks the
         * display's HORIZONTAL position and raw_x tracks its VERTICAL
         * position -- e.g. the top-left and bottom-left crosshairs
         * (same logical x, different logical y) produced nearly
         * identical raw_y (~20-30) despite very different raw_x; the
         * top-left and top-right crosshairs (same logical y, different
         * logical x) produced nearly identical raw_x (~410) despite very
         * different raw_y. This matches FreeInkUICore.h's own
         * touchToLogical() for LandscapeClockwise touch orientation
         * (`lx = 1-ny; ly = nx`) -- the transform this driver should
         * have been calling from the start, per DisplayTarget's own
         * touchOrientationFor(Portrait) == LandscapeClockwise. Applied
         * here as a direct linear fit per axis instead (touch_lerp()/
         * touch_lerp_y(), same idea, calibrated straight from measured
         * crosshair positions rather than derived through the
         * library's normalized-coordinate math). A first pass fit each
         * axis as one line end to end; a retest against all 5 crosshairs
         * showed the 4 corners landing within ~13px but the center
         * missing by up to ~100px on Y -- see touch_lerp_y()'s own
         * comment above for why Y is now two line segments instead of
         * one (X stayed a single line; its own center-point error was
         * small enough to be plain tap imprecision, not a real curve). */
        pt.x = touch_lerp(raw_y, TOUCH_X_RAWY_LO, TOUCH_X_LOGICAL_LO,
                           TOUCH_X_RAWY_HI, TOUCH_X_LOGICAL_HI, EINK_H - 1);
        pt.y = touch_lerp_y(raw_x);
        /* Per-unit correction on top of the fixed calibration above --
         * identity until Settings > Touch Calibration has been run once
         * (see this file's "Per-unit correction" section). */
        pt.x = apply_cal_x(pt.x);
        pt.y = apply_cal_y(pt.y);
        pt.raw_x = raw_x;
        pt.raw_y = raw_y;
        pt.pressed = true;
    }

    /* Free the buffer for next time. */
    uint8_t clr_val = 0x00;
    reg_write(GT_REG_STATUS, &clr_val, 1);
    return pt;
}
