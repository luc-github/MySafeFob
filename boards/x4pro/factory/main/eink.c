/* 
 Project: MySafeFob  eink.c
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
 * @file eink.c
 * @brief MySafeFob Factory — E-Ink UC8279 driver (X4 Pro).
 *
 * Direct port of the sequences validated in the x4pro-probe probe (eink_test.c,
 * einkucinit/einkuc2 mode 0 commands). Do not "simplify" the delays or
 * the register order — every deviation has already been measured to break it.
 */
#include "eink.h"
#include "hw_config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "eink";

/* UC8279 commands (UltraChip batch) */
#define UC_CMD_PANEL_SETTING 0x00   /* PSR  */
#define UC_CMD_POWER_OFF     0x02   /* POF  */
#define UC_CMD_PFS           0x03   /* PFS  */
#define UC_CMD_POWER_ON      0x04   /* PON  */
#define UC_CMD_DTM1          0x10   /* OLD plane */
#define UC_CMD_DRF           0x12   /* display refresh */
#define UC_CMD_DTM2          0x13   /* NEW plane */
#define UC_CMD_PLL           0x30
#define UC_CMD_CDI           0x50   /* SINGLE byte */
#define UC_CMD_TRES          0x61
#define UC_CMD_GSST          0x65
#define UC_CMD_CCSET         0xE0
#define UC_CMD_GATE_SCAN     0xE1
#define UC_CMD_TSSET         0xE5

#define UC_TRES_H            600     /* gates addressed (480 visible) */
#define UC_GATE_OFFSET       120     /* gates 0..119 = non-visible area */

/* ---- Fast DU refresh (partial OTP waveform from stock FW, reverse-
 * engineered from Uc8279X4Driver::startBwRefresh fast=true — reference
 * extracted from freeink-sdk, docs/xteink-x3-uc8279-support.md). KEY
 * POINTS measured from the stock firmware:
 *   - DU = OTP waveform (REG=0), NO host LUT: selection is done via
 *     TSSET 0x5A + CDI 0xD7. The probe froze the UC8279 with REG=1.
 *   - PTIN requires a prior PTL window, otherwise DU scans without
 *     developing (field observation: DRF 443ms, no image).
 *   - OLD plane = previous frame (differential diff); after each
 *     refresh, DTM1 is resynced with the displayed frame.
 *   - A periodic full GC remains mandatory (ghost budget). */
#define UC_CMD_PTL           0x90    /* PTL: partial window */
#define UC_CMD_PTL_IN        0x91    /* PTIN */
#define UC_CMD_PTL_OUT       0x92    /* PTOUT */
#define UC_CDI_FAST          0xD7
#define UC_TSSET_FAST        0x5A
#define EINK_FAST_BUDGET     30      /* fast DUs between two full GCs
                                      * (2026-09-14 session: 10 left
                                      * black->grey->white ghosts on the
                                      * selection bar — a large moving black
                                      * area is the worst case for DU. Raised
                                      * to 30 (2026-09-15 session): the real
                                      * culprit for the ghosting was the
                                      * s_prev bug never updated after a fast
                                      * DU (fixed) — the diff was computed
                                      * against a stale frame, not an
                                      * intrinsic DU limit. To be revalidated
                                      * on hardware; raise further or lower
                                      * again depending on ghosting observed
                                      * over a long session. */

static spi_device_handle_t s_spi = NULL;
static bool s_initialized = false;

static uint8_t *s_prev = NULL;        /* PSRAM: last displayed frame */
static bool s_prev_valid = false;
static uint8_t s_fast_count = 0;
static bool s_screen_on = false;

/* Defined further down in the file (driver's historical order). */
static void write_cmd(uint8_t cmd);
static bool wait_idle(const char *what, uint32_t timeout_ms);

static void cs_low(void)  { gpio_set_level(EINK_CS, 0); }
static void cs_high(void) { gpio_set_level(EINK_CS, 1); }

/* Idempotent PON (stock FW avoids a second PON while the screen is on). */
static esp_err_t power_on(void)
{
    if (s_screen_on) return ESP_OK;
    write_cmd(UC_CMD_POWER_ON);
    if (wait_idle("PON", 2000)) return ESP_ERR_TIMEOUT;
    s_screen_on = true;
    return ESP_OK;
}

/* "Previous frame" buffer in PSRAM (48 KB) — basis of the DU diff. */
static bool prev_alloc(void)
{
    if (s_prev) return true;
    s_prev = heap_caps_malloc(SCREEN_FB_SIZE,
                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_prev) {
        ESP_LOGW(TAG, "PSRAM unavailable for the DU diff — full GC only");
    }
    return s_prev != NULL;
}

static void record_prev(const uint8_t *fb)
{
    if (!prev_alloc()) {
        s_prev_valid = false;
        return;
    }
    memcpy(s_prev, fb, SCREEN_FB_SIZE);
    s_prev_valid = true;
    s_fast_count = 0;   /* new cycle: fast budget reset to 0 */
}

/* Waits for BUSY_N to go back HIGH (idle). true = timeout (abnormal state). */
static bool wait_idle(const char *what, uint32_t timeout_ms)
{
    vTaskDelay(pdMS_TO_TICKS(10));   /* let the controller take BUSY */
    uint64_t start = esp_timer_get_time();
    while (gpio_get_level(EINK_BUSY) == 0) {
        if ((esp_timer_get_time() - start) / 1000 > timeout_ms) {
            ESP_LOGW(TAG, "UC BUSY timeout (%s) after %lu ms", what,
                     (unsigned long)timeout_ms);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));  /* >= 1 tick: feeds the watchdog */
    }
    return false;
}

static void spi_write(const uint8_t *data, size_t len)
{
    /* S3 DMA limited to 32768 B/transaction -> chunk at 16 KB. */
    while (len > 0) {
        size_t chunk = len > 16384 ? 16384 : len;
        spi_transaction_t x = {0};
        x.length = chunk * 8;
        x.tx_buffer = data;
        spi_device_transmit(s_spi, &x);
        data += chunk;
        len -= chunk;
    }
}

static void write_cmd(uint8_t cmd)
{
    gpio_set_level(EINK_DC, 0);
    cs_low();
    spi_write(&cmd, 1);
    cs_high();
}

static void write_data(const uint8_t *data, size_t len)
{
    gpio_set_level(EINK_DC, 1);
    cs_low();
    spi_write(data, len);
    cs_high();
}

static void hw_reset(void)
{
    gpio_set_level(EINK_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EINK_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EINK_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static esp_err_t spi_init(void)
{
    if (s_spi) return ESP_OK;

    spi_bus_config_t bus = {
        .sclk_io_num = EINK_SCLK,
        .mosi_io_num = EINK_MOSI,
        .miso_io_num = GPIO_NUM_NC,     /* write-only */
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = SCREEN_FB_SIZE + 64,
    };
    esp_err_t err = spi_bus_initialize(EINK_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return err;

    spi_device_interface_config_t dev = {
        .clock_speed_hz = EINK_SPI_HZ,
        .mode = 0,
        .spics_io_num = GPIO_NUM_NC,    /* manual CS */
        .queue_size = 1,
    };
    return spi_bus_add_device(EINK_HOST, &dev, &s_spi);
}

/* Init registers, exact order from Uc8279X4Driver (FreeInk) + stock firmware RE.
 * no BTST/PWS: PWR/VDCS/BTST stay panel-programmed (OTP/MTP). */
static void write_init_registers(uint8_t psr0)
{
    write_cmd(UC_CMD_PANEL_SETTING);
    { const uint8_t v[2] = {psr0, 0x4D}; write_data(v, 2); }

    write_cmd(UC_CMD_TRES);
    { const uint8_t v[4] = {0x03, 0x20, 0x02, 0x58}; write_data(v, 4); }  /* 800 x 600 */

    write_cmd(UC_CMD_GSST);
    { const uint8_t v[4] = {0x00, 0x00, 0x00, 0x00}; write_data(v, 4); }

    write_cmd(UC_CMD_PFS);
    { uint8_t v = 0x20; write_data(&v, 1); }

    write_cmd(UC_CMD_PLL);   /* X4 Pro only (stock X4C: no-op) */
    { uint8_t v = 0x0E; write_data(&v, 1); }

    write_cmd(UC_CMD_GATE_SCAN);
    { uint8_t v = 0x02; write_data(&v, 1); }
}

/* Streaming one plane: white pad gates 0..119, 480 fb lines in direct order,
 * white pad up to 600 gates. Final orientation (mode 0, measured 12:18). */
static void stream_plane(uint8_t ram_cmd, const uint8_t *fb)
{
    static uint8_t row[EINK_WB];
    write_cmd(ram_cmd);
    memset(row, 0xFF, sizeof(row));
    for (int y = 0; y < UC_GATE_OFFSET; y++) {
        write_data(row, EINK_WB);
    }
    for (int y = 0; y < EINK_H; y++) {
        write_data(fb + (uint32_t)y * EINK_WB, EINK_WB);
    }
    for (int y = UC_GATE_OFFSET + EINK_H; y < UC_TRES_H; y++) {
        write_data(row, EINK_WB);
    }
}

esp_err_t eink_init(void)
{
    if (s_initialized) return ESP_OK;

    esp_err_t err = spi_init();
    if (err != ESP_OK) return err;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << EINK_CS) | (1ULL << EINK_DC) | (1ULL << EINK_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_config_t busy_io = {
        .pin_bit_mask = 1ULL << EINK_BUSY,
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&busy_io);
    cs_high();

    hw_reset();
    write_init_registers(0x37);   /* REG=1 (host LUT) at init */

    s_initialized = true;
    ESP_LOGI(TAG, "UC8279 init OK (800x480 visible, gates 120-599)");
    return ESP_OK;
}

esp_err_t eink_display_fb(const uint8_t *fb)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    /* NO PSR write here (host state is guaranteed at write time):
       a PSR write before DTM has already frozen the controller (measured 11:21). */

    stream_plane(UC_CMD_DTM2, fb);            /* NEW = frame */
    write_cmd(UC_CMD_DTM1);                   /* OLD = white */
    {
        static uint8_t row[EINK_WB];
        memset(row, 0xFF, sizeof(row));
        for (int y = 0; y < UC_TRES_H; y++) write_data(row, EINK_WB);
    }

    /* Refresh setup — order from stock FW: CDI (1 B), CCSET, TSSET, PON,
       PSR BETWEEN PON and DRF (PON reloads the MTP), DRF. */
    write_cmd(UC_CMD_CDI);
    { uint8_t v = 0x97; write_data(&v, 1); }
    write_cmd(UC_CMD_CCSET);
    { uint8_t v = 0x02; write_data(&v, 1); }
    write_cmd(UC_CMD_TSSET);
    { uint8_t v = 0x1E; write_data(&v, 1); }

    if (power_on() != ESP_OK) return ESP_ERR_TIMEOUT;

    /* FULL rewrite of the registers between PON and DRF with PSR 0x17
       (REG=0, MTP scan) — UC8279 rev v0.2 requirement (measured 11:58). */
    write_init_registers(0x17);

    write_cmd(UC_CMD_DRF);
    {   /* confirm start (BUSY drop) then wait for completion (BUSY HIGH) */
        uint64_t t0 = esp_timer_get_time();
        while (gpio_get_level(EINK_BUSY) == 1 &&
               (esp_timer_get_time() - t0) / 1000 < 50) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (wait_idle("DRF", 20000)) {
        ESP_LOGE(TAG, "DRF timeout — refresh did not complete");
        write_cmd(UC_CMD_POWER_OFF);
        wait_idle("POF-recovery", 3000);
        s_screen_on = false;
        s_prev_valid = false;
        return ESP_ERR_TIMEOUT;
    }
    record_prev(fb);
    return ESP_OK;
}

esp_err_t eink_display_fb_fast(const uint8_t *fb)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    /* A fast DU is only possible on a healthy differential diff:
       previous frame known AND ghost budget not exhausted. Otherwise full. */
    if (!s_prev_valid || !s_prev || s_fast_count >= EINK_FAST_BUDGET) {
        return eink_display_fb(fb);
    }

    /* NEW = frame, OLD = previous frame (diff, no flashing clear). */
    stream_plane(UC_CMD_DTM2, fb);
    stream_plane(UC_CMD_DTM1, s_prev);

    /* Stock FW DU trigger (reverse-engineered from
       Uc8279X4Driver::startBwRefresh fast=true): CDI 0xD7, CCSET, TSSET 0x5A, PFS, gate scan, PON,
       PTIN + FULL PTL window (mandatory — without PTL, DU scans
       without developing), PSR 0x17 (OTP), DRF. */
    write_cmd(UC_CMD_CDI);
    { uint8_t v = UC_CDI_FAST; write_data(&v, 1); }
    write_cmd(UC_CMD_CCSET);
    { uint8_t v = 0x02; write_data(&v, 1); }
    write_cmd(UC_CMD_TSSET);
    { uint8_t v = UC_TSSET_FAST; write_data(&v, 1); }
    write_cmd(UC_CMD_PFS);
    { uint8_t v = 0x20; write_data(&v, 1); }
    write_cmd(UC_CMD_GATE_SCAN);
    { uint8_t v = 0x02; write_data(&v, 1); }

    if (power_on() != ESP_OK) {
        s_prev_valid = false;
        return ESP_ERR_TIMEOUT;
    }

    /* PTL: full window, gate coords with the +120 visible offset,
       PT_SCAN=1. Byte-exact from Uc8279X4Driver. */
    write_cmd(UC_CMD_PTL_IN);
    write_cmd(UC_CMD_PTL);
    {
        const uint8_t v[9] = {
            0x00, 0x00,             /* x start */
            0x03, 0x1F,             /* x end 799|0x07 */
            (uint8_t)(UC_GATE_OFFSET >> 8), (uint8_t)(UC_GATE_OFFSET & 0xFF),
            (uint8_t)((UC_GATE_OFFSET + EINK_H - 1) >> 8),
            (uint8_t)((UC_GATE_OFFSET + EINK_H - 1) & 0xFF),
            0x01,                   /* PT_SCAN */
        };
        write_data(v, sizeof(v));
    }

    write_cmd(UC_CMD_PANEL_SETTING);
    { const uint8_t v[2] = {0x17, 0x4D}; write_data(v, 2); }

    write_cmd(UC_CMD_DRF);
    {
        uint64_t t0 = esp_timer_get_time();
        while (gpio_get_level(EINK_BUSY) == 1 &&
               (esp_timer_get_time() - t0) / 1000 < 50) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (wait_idle("DRF-fast", 20000)) {
        ESP_LOGE(TAG, "DRF fast timeout");
        write_cmd(UC_CMD_PTL_OUT);
        write_cmd(UC_CMD_POWER_OFF);
        wait_idle("POF-recovery", 3000);
        s_screen_on = false;
        s_prev_valid = false;
        return ESP_ERR_TIMEOUT;
    }
    write_cmd(UC_CMD_PTL_OUT);

    /* Resync DTM1 with the displayed frame (the next fast diffs
       against it) — must be done while the screen is still powered, like stock. */
    stream_plane(UC_CMD_DTM1, fb);

    /* BUGFIX: s_prev (software cache of the last frame actually
     * displayed) MUST track every successful fast DU, otherwise the next
     * DU diffs against a stale frame (the last full refresh's) instead
     * of the current screen state -> incorrect toggles accumulate over
     * the budget (ghosting / local inversions), only fixed at the next
     * forced full refresh. */
    memcpy(s_prev, fb, SCREEN_FB_SIZE);

    s_fast_count++;
    return ESP_OK;
}

esp_err_t eink_power_off(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    write_cmd(UC_CMD_POWER_OFF);
    wait_idle("POF", 2000);
    s_screen_on = false;
    /* The bistable image persists: s_prev remains physically valid. */
    return ESP_OK;
}
