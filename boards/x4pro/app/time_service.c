/*
 Project: MySafeFob  time_service.c
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
 * @file time_service.c
 * @brief MySafeFob App — system clock + BM8563 RTC (UTC), see time_service.h.
 */
#include "time_service.h"
#include "i2c_bus.h"
#include "hw_config.h"
#include "settings_store.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "app_log_workaround.h"

#include <sys/time.h>

#define TAG "time"

#define RTC_I2C_ADDR 0x51
#define RTC_REG_CONTROL1 0x00
#define RTC_REG_SECONDS 0x02
#define RTC_CTRL1_STOP 0x20
#define kMinValidYear 2024
#define kMaxValidYear 2099

static inline uint8_t to_bcd(int v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static inline int from_bcd(uint8_t v)
{
    return (v >> 4) * 10 + (v & 0x0F);
}

/* Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant). */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int yoe = (int)(y - era * 400);
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = (int)(z - era * 146097);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = yoe + era * 400;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = (int)(yy + (*m <= 2));
}

time_t time_service_dt_to_epoch(const time_service_dt_t *dt)
{
    return (time_t)(days_from_civil(dt->year, dt->month, dt->day) * 86400 + dt->hour * 3600 + dt->minute * 60 +
                    dt->second);
}

void time_service_epoch_to_dt(time_t t, time_service_dt_t *out)
{
    int64_t days = (int64_t)t / 86400;
    int rem = (int)((int64_t)t % 86400);
    if (rem < 0) {
        rem += 86400;
        days--;
    }
    civil_from_days(days, &out->year, &out->month, &out->day);
    out->hour = rem / 3600;
    out->minute = (rem % 3600) / 60;
    out->second = rem % 60;
}

bool time_service_dt_is_valid(const time_service_dt_t *dt)
{
    if (dt->year < kMinValidYear || dt->year > kMaxValidYear) return false;
    if (dt->month < 1 || dt->month > 12) return false;
    if (dt->hour < 0 || dt->hour > 23 || dt->minute < 0 || dt->minute > 59 || dt->second < 0 || dt->second > 59) {
        return false;
    }
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int max_day = kDays[dt->month - 1];
    bool leap = (dt->year % 4 == 0 && dt->year % 100 != 0) || dt->year % 400 == 0;
    if (dt->month == 2 && leap) max_day = 29;
    return dt->day >= 1 && dt->day <= max_day;
}

static esp_err_t rtc_open(i2c_master_dev_handle_t *dev)
{
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_bus_get(&bus);
    if (err != ESP_OK) return err;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RTC_I2C_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(bus, &cfg, dev);
}

static bool rtc_read_regs(i2c_master_dev_handle_t dev, time_service_dt_t *out)
{
    uint8_t reg = RTC_REG_SECONDS;
    uint8_t buf[7] = {0};
    if (i2c_master_transmit_receive(dev, &reg, 1, buf, sizeof(buf), pdMS_TO_TICKS(200)) != ESP_OK) return false;
    if (buf[0] & 0x80) return false; /* VL: voltage low, time not trustworthy */
    out->second = from_bcd(buf[0] & 0x7F);
    out->minute = from_bcd(buf[1] & 0x7F);
    out->hour = from_bcd(buf[2] & 0x3F);
    out->day = from_bcd(buf[3] & 0x3F);
    out->month = from_bcd(buf[5] & 0x1F);
    out->year = 2000 + from_bcd(buf[6]);
    return true;
}

/* Reads the RTC on a second boundary: the seconds register only has 1 s
 * resolution, so a plain read is off by 0-1 s. Polling until it ticks gives
 * the time of that exact edge (~1-2 ms accuracy) at the cost of up to ~1 s. */
static bool rtc_read_dt_aligned(time_service_dt_t *out)
{
    i2c_master_dev_handle_t dev = NULL;
    if (rtc_open(&dev) != ESP_OK) return false;
    time_service_dt_t first;
    bool ok = rtc_read_regs(dev, &first);
    *out = first;
    if (ok) {
        for (int i = 0; i < 1200; i++) {
            esp_rom_delay_us(1000);
            time_service_dt_t cur;
            if (!rtc_read_regs(dev, &cur)) break;
            if (cur.second != first.second) {
                *out = cur;
                break;
            }
        }
    }
    i2c_master_bus_rm_device(dev);
    return ok;
}

/* Writes the RTC so that its seconds counter starts exactly on a second
 * boundary of the system clock (which must already be correct): time for the
 * NEXT second is written with the oscillator stopped (STOP bit), then STOP is
 * cleared when the system clock reaches that second. Blocks 1-2 s. */
static bool rtc_write_aligned(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t target = tv.tv_sec + (tv.tv_usec > 800000 ? 2 : 1);

    time_service_dt_t dt;
    time_service_epoch_to_dt(target, &dt);
    int weekday = (int)((((int64_t)target / 86400) + 4) % 7); /* 1970-01-01 was a Thursday */

    i2c_master_dev_handle_t dev = NULL;
    if (rtc_open(&dev) != ESP_OK) return false;
    uint8_t stop[3] = {RTC_REG_CONTROL1, RTC_CTRL1_STOP, 0x00};
    uint8_t buf[8] = {RTC_REG_SECONDS,
                      to_bcd(dt.second),
                      to_bcd(dt.minute),
                      to_bcd(dt.hour),
                      to_bcd(dt.day),
                      to_bcd(weekday),
                      to_bcd(dt.month),
                      to_bcd(dt.year - 2000)};
    esp_err_t err = i2c_master_transmit(dev, stop, sizeof(stop), pdMS_TO_TICKS(200));
    if (err == ESP_OK) err = i2c_master_transmit(dev, buf, sizeof(buf), pdMS_TO_TICKS(200));
    if (err == ESP_OK) {
        for (;;) {
            gettimeofday(&tv, NULL);
            if (tv.tv_sec >= target) break;
            int64_t remaining_us = (int64_t)(target - tv.tv_sec) * 1000000 - tv.tv_usec;
            if (remaining_us > 20000) vTaskDelay(1);
            else esp_rom_delay_us(200);
        }
        uint8_t run[2] = {RTC_REG_CONTROL1, 0x00};
        err = i2c_master_transmit(dev, run, sizeof(run), pdMS_TO_TICKS(200));
    }
    i2c_master_bus_rm_device(dev);
    return err == ESP_OK;
}

void time_service_init(void)
{
    time_service_dt_t dt;
    if (rtc_read_dt_aligned(&dt) && time_service_dt_is_valid(&dt)) {
        struct timeval tv = {.tv_sec = time_service_dt_to_epoch(&dt), .tv_usec = 1000};
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "clock loaded from RTC: %04d-%02d-%02d %02d:%02d:%02d UTC", dt.year, dt.month, dt.day,
                 dt.hour, dt.minute, dt.second);
    } else {
        ESP_LOGW(TAG, "RTC unset or unreadable, clock not initialized");
    }
}

time_t time_service_get_utc(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
}

bool time_service_is_valid(void)
{
    time_service_dt_t dt;
    time_service_epoch_to_dt(time_service_get_utc(), &dt);
    return dt.year >= kMinValidYear;
}

static void record_sync(time_t new_utc, int32_t delta, time_sync_source_t source)
{
    settings_store_push_time_sync((uint32_t)new_utc, delta, (uint32_t)source);
    ESP_LOGI(TAG, "sync recorded: source %d, offset %ld s", (int)source, (long)delta);
}

bool time_service_set_utc(time_t utc, time_sync_source_t source)
{
    bool old_valid = time_service_is_valid();
    time_t old_utc = time_service_get_utc();
    struct timeval tv = {.tv_sec = utc, .tv_usec = 0};
    settimeofday(&tv, NULL);
    bool ok = rtc_write_aligned();
    ESP_LOGI(TAG, "clock set (epoch %lld), RTC %s", (long long)utc, ok ? "written" : "WRITE FAILED");
    int32_t delta = TIME_SYNC_DELTA_UNKNOWN;
    if (old_valid) {
        int64_t d = (int64_t)utc - (int64_t)old_utc;
        if (d > INT32_MAX - 1) d = INT32_MAX - 1;
        if (d < INT32_MIN + 1) d = INT32_MIN + 1;
        delta = (int32_t)d;
    }
    record_sync(utc, delta, source);
    return ok;
}

bool time_service_commit_system_time(int32_t delta_s, bool delta_valid, time_sync_source_t source)
{
    time_t now = time_service_get_utc();
    bool ok = rtc_write_aligned();
    ESP_LOGI(TAG, "system time committed (epoch %lld), RTC %s", (long long)now, ok ? "written" : "WRITE FAILED");
    record_sync(now, delta_valid ? delta_s : TIME_SYNC_DELTA_UNKNOWN, source);
    return ok;
}
