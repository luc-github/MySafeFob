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
 * @brief Encrypts the accounts with `passphrase` and writes a new file.
 * @param profile NULL = every account (full backup); else only that
 *        profile's (2026-10-17). The file name never shows the profile:
 *        it is plaintext on the card.
 * @param name_out receives the file name (BACKUP_NAME_MAX).
 * @param count_out (may be NULL) receives the number of accounts written.
 * @return ESP_OK; ESP_ERR_NOT_FOUND (no SD card / not FAT); ESP_ERR_INVALID_ARG
 *         (passphrase length); ESP_ERR_INVALID_STATE (store locked); ESP_FAIL
 *         (write error).
 */
esp_err_t backup_export(const char *passphrase, const char *profile, char *name_out, int *count_out);

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

#define BACKUP_PATH_MAX 48   /* "/sdcard/MSF/RESCUE01.MSR", "/sdcard/<name>.json" */

/**
 * @brief Plaintext leftovers on the card (SD sanity check, 2026-10-11):
 *        MSF/RESCUE*.MSR (factory rescue copies, PIN-only protection) and
 *        *.json in the root or MSF/ whose first bytes contain "mysafefob"
 *        (msf_backup.py / msf_editor.html exports; other apps' JSON is
 *        never listed). Paths as "/sdcard/...".
 * @return number found (<= max), -1 if there is no usable card.
 */
int backup_find_plaintext(char (*paths)[BACKUP_PATH_MAX], int max);

/**
 * @brief Overwrites each file with zeros, then deletes it (best effort: an
 *        SD card's own wear levelling may keep old blocks).
 * @return number deleted.
 */
int backup_wipe_files(char (*paths)[BACKUP_PATH_MAX], int count);

#ifdef __cplusplus
}
#endif
