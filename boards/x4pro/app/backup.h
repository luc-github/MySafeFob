/*
 Project: MySafeFob  backup.h
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
 * @file backup.h
 * @brief MySafeFob App — encrypted backups on the SD card (F-06b, ROADMAP
 *        8.0 P11, ADR-005 + amendment 2026-10-11). Files are
 *        /sdcard/MSF/MSF-YYYYMMDD-HHMM.msfx (local time), never
 *        overwritten; only this encrypted format is read or written on the
 *        device (plaintext JSON lives on the PC: tools/msf_backup.py).
 *        Every call mounts the card and unmounts it before returning. The
 *        export and load calls take seconds (Argon2id): never call them
 *        from the LVGL task.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "accounts.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BACKUP_DIR "/sdcard/MSF"
#define BACKUP_NAME_MAX 32   /* "MSF-20261011-1530-2.msfx" + margin */

typedef struct {
    char name[BACKUP_NAME_MAX];   /* file name inside BACKUP_DIR */
    int64_t created;              /* from the header, 0 = unknown */
    uint16_t count;               /* records, from the header */
} backup_file_t;

/**
 * @brief Encrypts every account with `passphrase` and writes a new file.
 * @param name_out receives the file name (BACKUP_NAME_MAX).
 * @return ESP_OK; ESP_ERR_NOT_FOUND (no SD card / not FAT); ESP_ERR_INVALID_ARG
 *         (passphrase length); ESP_ERR_INVALID_STATE (store locked); ESP_FAIL
 *         (write error).
 */
esp_err_t backup_export(const char *passphrase, char *name_out);

/**
 * @brief Lists the backups, newest first (headers only, no passphrase).
 * @return number of files (<= max), or -1 if there is no usable SD card.
 */
int backup_list(backup_file_t *out, int max);

/**
 * @brief Reads, authenticates and decrypts backup `name` into `out` (up to
 *        MSF_STORE_MAX_ACCOUNTS records). The store is not touched.
 * @return as msf_backup_open(), plus ESP_ERR_NOT_FOUND (no card / no file).
 */
esp_err_t backup_load(const char *name, const char *passphrase, account_t *out, int max, int *count);

#ifdef __cplusplus
}
#endif
