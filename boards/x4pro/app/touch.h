/* 
 Project: MySafeFob  touch.h
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
 * @file touch.h
 * @brief MySafeFob App — X4 Pro GT911 touch driver (polling).
 *
 * GT911 driver, with the sequences validated on the x4pro-probe probe:
 *  - POR reset dance (RST=GPIO4, INT=GPIO10, rail GPIO2 active-low)
 *  - CONFIG UPLOAD MANDATORY on every boot (OTP blank from the factory on
 *    this batch: 0x8047 reads 0x00, the panel doesn't scan without host config)
 *  - mapping validated by the 4-corner test: fb_x = raw_y, fb_y = 479 - raw_x
 *
 * Systematic reads of >= 2 bytes (IDF 5.4 I2C driver quirk:
 * 1-byte reads get NACKed).
 *
 * Identical copy of boards/x4pro/factory/main/touch.h (ADR-010: same
 * hardware, no board divergence expected) — kept independent from the
 * factory's per ADR-010 pt.3 (the factory copy is only updated by an
 * explicit action tested on hardware, never automatically alongside the
 * app's copy).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool pressed;       /* true if a finger is down */
    bool home;          /* true if the point is within the Home pad zone
                         * (validated measurement 01:34: raw rx<70, ry 380-580) */
    int16_t x;          /* landscape 800x480 framebuffer coords */
    int16_t y;
} touch_point_t;

/**
 * @brief Init rails + GT911 POR dance + I2C probe + config upload if needed.
 * @return true if the controller responds and scans.
 */
bool touch_init(void);

/**
 * @brief Reads the current touch state (polling, non-blocking).
 *        NEVER reset the chip between two reads (2026-09-12 bug:
 *        re-dancing on every poll prevented scanning).
 */
touch_point_t touch_read(void);
