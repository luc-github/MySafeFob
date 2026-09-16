/* 
 Project: MySafeFob  sdcard.h
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
 * @file sdcard.h
 * @brief MySafeFob Factory — X4 Pro SD (native SDMMC 1-bit).
 *   Pins: CLK=41, CMD=42, DAT0=40, power GPIO5 active-LOW (pulsed on mount).
 *   Sequence validated against freeink-sdk: rail pulsed on mount, no CD/WP
 *   (NC), 40 MHz.
 */
#pragma once

#include "esp_err.h"

esp_err_t sdcard_mount(void);
esp_err_t sdcard_unmount(void);
