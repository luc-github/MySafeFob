/* 
 Project: MySafeFob  i2c_bus.h
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
 * @file i2c_bus.h
 * @brief MySafeFob App — shared I2C bus (SDA=39/SCL=38), a single
 *        i2c_master_bus_handle_t for all peripherals (GT911 touch, and
 *        future RTC/gauge). ESP-IDF only allows ONE bus instance per I2C
 *        port — each driver adds/removes its own
 *        i2c_master_dev_handle_t on this shared bus (same pattern as
 *        touch.c for per-device access, already validated on this unit).
 */
#pragma once

#include "driver/i2c_master.h"

/**
 * @brief Returns the shared I2C bus, creating it on first call.
 */
esp_err_t i2c_bus_get(i2c_master_bus_handle_t *out);
