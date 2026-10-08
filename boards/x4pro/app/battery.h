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
 * @brief MySafeFob App — CW2017 gauge (I2C 0x63) + charge detection
 *        (GPIO21, active-HIGH). Formulas see docs/hardware-specs.md.
 *
 *        The CW2017 reports 0% until its 80-byte BATINFO battery profile is
 *        resident (the profile lives in the gauge's RAM and is lost when the
 *        cell is fully drained or the gauge resets). battery_read() checks
 *        the profile and uploads it when missing (sequence from freeink-sdk's
 *        BatteryMonitor, MIT, recovered from the X4 Pro OEM firmware).
 *
 *        Diverged on 2026-10-08 from boards/x4pro/factory/main/battery.{c,h}
 *        (profile upload added here only; the factory copy stays frozen,
 *        ADR-010 pt.3).
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

/** @brief Raw gauge state, for the `battery` console command. */
typedef struct {
    bool i2c_ok;          /* gauge answered */
    bool charging;        /* GPIO21 */
    uint8_t version;      /* reg 0x00: 0xA0 while starting, 0x0D/0x0F running */
    uint8_t mode;         /* reg 0x08: 0x00 = normal */
    bool update_flag;     /* reg 0x0B bit 7: profile marked as loaded */
    bool profile_match;   /* resident BATINFO equals the X4 Pro profile */
    uint8_t soc;          /* reg 0x04, % */
    uint16_t vcell_mv;    /* regs 0x02/0x03 */
} battery_status_t;

/** @brief Reads every gauge register above without changing anything. */
bool battery_get_status(battery_status_t *status);

/**
 * @brief Uploads the BATINFO profile (force = true: even if it already
 *        matches), restarts the gauge and waits until it reports a SoC.
 */
bool battery_reload_profile(bool force);
