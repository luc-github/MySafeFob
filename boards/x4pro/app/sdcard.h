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
 * @brief MySafeFob App — SD card, native SDMMC 1-bit (slot 1), FAT. Same
 *        sequence as the factory's sdcard.c (hardware-validated
 *        2026-09-13), and the same pattern as PiBot's app-side SD driver:
 *        mounted only for one operation (backup export/import), unmounted
 *        right after, so a card can be removed at any other time.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SD_MOUNT_POINT "/sdcard"

/** @brief Powers the slot and mounts the FAT volume (no format). */
esp_err_t sdcard_mount(void);

esp_err_t sdcard_unmount(void);

#ifdef __cplusplus
}
#endif
