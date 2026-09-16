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
