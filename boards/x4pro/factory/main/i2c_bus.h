/**
 * @file i2c_bus.h
 * @brief MySafeFob Factory — shared I2C bus (SDA=39/SCL=38), a single
 *        i2c_master_bus_handle_t for all peripherals (GT911 touch,
 *        BM8563 RTC, CW2017 gauge). ESP-IDF only allows ONE bus instance
 *        per I2C port — each driver adds/removes its own
 *        i2c_master_dev_handle_t on this shared bus (same pattern as
 *        touch.c for per-device access, already validated on this unit).
 */
#pragma once

#include "driver/i2c_master.h"

/**
 * @brief Returns the shared I2C bus, creating it on first call.
 */
esp_err_t i2c_bus_get(i2c_master_bus_handle_t *out);
