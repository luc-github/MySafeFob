/**
 * @file sdcard.h
 * @brief MySafeFob Factory — SD X4 Pro (SDMMC natif 1-bit, portage PiBot sdcard).
 *   Pins : CLK=41, CMD=42, DAT0=40, power GPIO5 actif-LOW (pulse au mount).
 *   Sequence FreeInk validee : rail pulsee au mount, pas de CD/WP (NC), 40 MHz.
 */
#pragma once

#include "esp_err.h"

esp_err_t sdcard_mount(void);
esp_err_t sdcard_unmount(void);
