/**
 * @file battery.h
 * @brief MySafeFob Factory — jauge CW2017 (I2C 0x63) + detection charge
 *        (GPIO21, actif-HIGH). Sequences validees x4pro-probe (cmd_gauge/
 *        cmd_charge), formules cf. docs/hardware-specs.md.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Lit le pourcentage batterie (reg 0x04) et l'etat de charge (GPIO21).
 * @return true si la jauge a repondu (soc_percent rempli ; charging toujours
 *         rempli, independant de l'I2C).
 */
bool battery_read(uint8_t *soc_percent, bool *charging);
