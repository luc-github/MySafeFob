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
 *        (GPIO21, active-HIGH). Sequences validated on x4pro-probe (cmd_gauge/
 *        cmd_charge), formulas see docs/hardware-specs.md.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Reads the battery percentage (reg 0x04) and charge state (GPIO21).
 * @return true if the gauge responded (soc_percent filled in; charging is
 *         always filled in, independent of I2C).
 */
bool battery_read(uint8_t *soc_percent, bool *charging);
