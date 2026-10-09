/*
 Project: MySafeFob  battery.c
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
 * @file battery.c
 * @brief MySafeFob Factory — CW2017 gauge (I2C) + charge detection (GPIO21).
 *
 * Register map, BATINFO profile and init/recovery sequence ported from
 * freeink-sdk (libs/hardware/BatteryMonitor/src/BatteryMonitor.cpp,
 * MIT License, Copyright (c) 2026 FreeInk), itself recovered from the X4 Pro
 * OEM firmware. Rewritten for ESP-IDF's i2c_master driver.
 *
 * Copy of boards/x4pro/app/battery.c (2026-10-08, deliberate update of the
 * frozen factory copy, ADR-010 pt.3) without the console-only
 * battery_get_status()/battery_reload_profile().
 */
#include "battery.h"
#include "hw_config.h"
#include "i2c_bus.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "battery";

#define REG_VERSION   0x00   /* 0xA0 while starting; running versions match 0x0D/0x0F */
#define REG_VCELL_H   0x02   /* 14-bit VCELL, big-endian over 0x02/0x03 */
#define REG_SOC       0x04   /* integer percent */
#define REG_MODE      0x08   /* soft-reset / sleep control */
#define REG_SOC_ALERT 0x0B   /* bit7 = profile loaded / update enable */
#define REG_BATINFO   0x10   /* 80-byte profile, 0x10..0x5F */

#define MODE_NORMAL   0x00
#define MODE_RESTART  0x30
#define MODE_DEFAULT  0xF0
#define UPDATE_FLAG   0x80

#define INIT_RETRY_US 1000000

/* The profile the OEM firmware uploads, specific to the X4 Pro's cell. */
static const uint8_t kBatInfo[80] = {
    0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xaa, 0xbf, 0xb5, 0xb4, 0xa4, 0x9c, 0xeb, 0xe2,
    0xdf, 0xe5, 0xca, 0xa0, 0x8a, 0x62, 0x53, 0x48, 0x40, 0x3a, 0x32, 0xb1, 0xae, 0xda, 0xb5, 0xff,
    0xff, 0xff, 0xe8, 0xdb, 0xd9, 0xd6, 0xd4, 0xd2, 0xd0, 0xcb, 0xc3, 0xbc, 0x9e, 0x87, 0x7b, 0x71,
    0x72, 0x7c, 0x8c, 0xa3, 0xb7, 0xc8, 0xa5, 0x4f, 0x00, 0x00, 0xab, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x23};

static bool s_gpio_ready = false;
static bool s_profile_ok = false;
static int64_t s_last_init_us = 0;
static bool s_init_attempted = false;

/* One lock around every gauge transaction sequence (same as the app). */
static SemaphoreHandle_t s_lock = NULL;
static StaticSemaphore_t s_lock_buf;
static portMUX_TYPE s_lock_init = portMUX_INITIALIZER_UNLOCKED;

static void lock(void)
{
    taskENTER_CRITICAL(&s_lock_init);
    if (!s_lock) s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    taskEXIT_CRITICAL(&s_lock_init);
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

static bool read_charging(void)
{
    if (!s_gpio_ready) {
        gpio_config_t io = { .pin_bit_mask = 1ULL << CHARGE_PIN, .mode = GPIO_MODE_INPUT };
        gpio_config(&io);
        s_gpio_ready = true;
    }
    return gpio_get_level(CHARGE_PIN) != 0;
}

static i2c_master_dev_handle_t gauge_open(void)
{
    i2c_master_bus_handle_t bus = NULL;
    if (i2c_bus_get(&bus) != ESP_OK) return NULL;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = GAUGE_I2C_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) != ESP_OK) return NULL;
    return dev;
}

static void gauge_close(i2c_master_dev_handle_t dev)
{
    if (dev) i2c_master_bus_rm_device(dev);
}

/* The gauge occasionally NACKs a read (seen in the app, 2026-10-08): retry
 * a few times before failing, same as the app's copy. */
#define READ_ATTEMPTS 3

static bool read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *out)
{
    for (int attempt = 0; attempt < READ_ATTEMPTS; attempt++) {
        if (i2c_master_transmit_receive(dev, &reg, 1, out, 1, pdMS_TO_TICKS(100)) == ESP_OK) return true;
        vTaskDelay(1);
    }
    return false;
}

static bool write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, 2, pdMS_TO_TICKS(100)) == ESP_OK;
}

static bool version_is_running(uint8_t version)
{
    return (version & 0xFD) == 0x0D;
}

/* Soft reset: MODE 0xF0 -> 0x30 -> 0x00, 20 ms apart (OEM sequence). */
static bool gauge_reset(i2c_master_dev_handle_t dev)
{
    if (!write_reg(dev, REG_MODE, MODE_DEFAULT)) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!write_reg(dev, REG_MODE, MODE_RESTART)) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!write_reg(dev, REG_MODE, MODE_NORMAL)) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    return true;
}

/* Waits (~1 s max) for the engine to leave its 0xA0 startup state, then
 * (~3 s max) for a valid SoC. */
static bool wait_until_ready(i2c_master_dev_handle_t dev)
{
    bool running = false;
    for (int i = 0; i < 50 && !running; i++) {
        uint8_t version = 0;
        running = read_reg(dev, REG_VERSION, &version) && version_is_running(version);
        if (!running) vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!running) return false;
    for (int i = 0; i < 30; i++) {
        uint8_t soc = 0;
        if (read_reg(dev, REG_SOC, &soc) && soc <= 100) return true;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}

/* Update flag set and every resident BATINFO byte equal to kBatInfo. An I2C
 * error (return false) is kept apart from a real mismatch, so a transient
 * bus failure never triggers a partial profile rewrite. */
static bool profile_matches(i2c_master_dev_handle_t dev, bool *matches)
{
    uint8_t config = 0;
    if (!read_reg(dev, REG_SOC_ALERT, &config)) return false;
    if ((config & UPDATE_FLAG) == 0) {
        *matches = false;
        return true;
    }
    for (uint8_t i = 0; i < sizeof(kBatInfo); i++) {
        uint8_t stored = 0;
        if (!read_reg(dev, (uint8_t)(REG_BATINFO + i), &stored)) return false;
        if (stored != kBatInfo[i]) {
            *matches = false;
            return true;
        }
    }
    *matches = true;
    return true;
}

static bool ensure_profile(i2c_master_dev_handle_t dev, bool force)
{
    uint8_t mode = 0;
    uint8_t version = 0;
    if (!read_reg(dev, REG_MODE, &mode) || !read_reg(dev, REG_VERSION, &version)) return false;

    bool matches = false;
    if (!profile_matches(dev, &matches)) return false;

    bool restart = force || mode != MODE_NORMAL || !version_is_running(version);
    if (force || !matches) {
        ESP_LOGW(TAG, "BATINFO profile %s, uploading", matches ? "forced" : "missing");
        for (uint8_t i = 0; i < sizeof(kBatInfo); i++) {
            if (!write_reg(dev, (uint8_t)(REG_BATINFO + i), kBatInfo[i])) return false;
        }
        if (!write_reg(dev, REG_SOC_ALERT, UPDATE_FLAG)) return false;
        vTaskDelay(pdMS_TO_TICKS(20));
        restart = true;
    }

    if (!restart) {
        uint8_t soc = 0;
        if (!read_reg(dev, REG_SOC, &soc)) return false;
        if (soc <= 100) return true;
        /* A running gauge should never report SoC > 100: one controlled restart. */
    }
    if (!gauge_reset(dev)) return false;
    bool ready = wait_until_ready(dev);
    ESP_LOGI(TAG, "gauge restarted, %s", ready ? "ready" : "NOT ready");
    return ready;
}

bool battery_read(uint8_t *soc_percent, bool *charging)
{
    bool chg = read_charging();
    if (charging) *charging = chg;

    lock();
    i2c_master_dev_handle_t dev = gauge_open();
    bool ok = false;
    if (dev) {
        if (!s_profile_ok) {
            int64_t now = esp_timer_get_time();
            if (!s_init_attempted || now - s_last_init_us >= INIT_RETRY_US) {
                s_init_attempted = true;
                s_last_init_us = now;
                s_profile_ok = ensure_profile(dev, false);
            }
        }
        if (s_profile_ok) {
            /* A sleeping/not-ready gauge is not a valid SoC source: drop back
             * to the bounded recovery on the next call. */
            uint8_t mode = 0, version = 0, soc = 0;
            if (read_reg(dev, REG_MODE, &mode) && mode == MODE_NORMAL &&
                read_reg(dev, REG_VERSION, &version) && version_is_running(version) &&
                read_reg(dev, REG_SOC, &soc) && soc <= 100) {
                if (soc_percent) *soc_percent = soc;
                ok = true;
            } else {
                s_profile_ok = false;
            }
        }
    }
    gauge_close(dev);
    unlock();
    return ok;
}
