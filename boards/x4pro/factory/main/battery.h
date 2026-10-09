/* 
 Project: MySafeFob  battery.h
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
 * @file battery.h
 * @brief MySafeFob Factory — CW2017 gauge (I2C 0x63) + charge detection
 *        (GPIO21, active-HIGH). Formulas see docs/hardware-specs.md.
 *
 *        The CW2017 reports 0% until its BATINFO battery profile is resident
 *        (lost when the cell is fully drained): battery_read() uploads it
 *        when missing. Same code as boards/x4pro/app/battery.c (2026-10-08).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Reads the battery percentage (reg 0x04) and charge state (GPIO21).
 *        Makes sure the BATINFO profile is loaded first (at most one attempt
 *        per second while it fails; a successful upload blocks up to ~4 s
 *        while the gauge restarts).
 * @return true if the gauge responded with a valid SoC (soc_percent filled
 *         in; charging is always filled in, independent of I2C).
 */
bool battery_read(uint8_t *soc_percent, bool *charging);
