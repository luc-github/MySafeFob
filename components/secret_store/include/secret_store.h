/*
 Project: MySafeFob  secret_store.h
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
 * @file secret_store.h
 * @brief MySafeFob — encrypted account store (F-04, ADR-002, ADR-004,
 *        ADR-019; INTERFACES.md §1).
 *
 *  - Lives in the raw `secrets` partition, no filesystem. Two blob slots
 *    written alternately with a sequence number: a power cut while writing
 *    leaves the previous blob intact (atomic replace).
 *  - Blob = plaintext header (magic, KDF parameters, salt, nonce, sequence,
 *    length) + AES-256-GCM(serialized records), the header authenticated
 *    as GCM additional data. Key = Argon2id(PIN, salt, m=2 MiB, t=1, p=1),
 *    ~1 s on the S3 (ADR-004 decision 2026-10-10); the parameters used are
 *    stored in the header.
 *  - PIN: 6 digits. Failure counter in a separate plaintext control sector:
 *    incremented BEFORE each check (a power cut cannot skip it), reset on
 *    success; from the 3rd failure a back-off delay (10 s, 30 s, 2 min,
 *    10 min, then 1 h) is enforced, counted from the last failure or from
 *    boot. No automatic erase (user decision 2026-10-10). A forgotten PIN
 *    loses the data (only an SD export can restore it).
 *  - This component only stores and protects an array of records; the
 *    account logic (sorting, letters, checks) is the caller's. The caller
 *    owns the decrypted records and must wipe them on lock.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Accounts the device holds: set per board (boards/<board>/store_config.cmake,
 * passed as a compile definition to the app and the factory); this default
 * only applies to a board that sets nothing. Bounded by the store format
 * (checked at compile time in secret_store.c). */
#ifndef MSF_STORE_MAX_ACCOUNTS
#define MSF_STORE_MAX_ACCOUNTS 250
#endif
#define MSF_PIN_LEN 6
#define MSF_NAME_MAX 48
#define MSF_USER_MAX 64
#define MSF_PASSWORD_MAX 128
#define MSF_PIN_FIELD_MAX 12       /* an account's own PIN (bank card...), not the device PIN */
#define MSF_NOTE_MAX 64
#define MSF_IDENT_MAX 64          /* customer / account number, e-mail... (2026-10-13) */
#define MSF_EXTRA_MAX 64          /* free line next to the note (2026-10-13) */
#define MSF_TOTP_KEY_MAX 103       /* Base32 text of a 64-byte secret */
#define MSF_RCV_MAX 32
#define MSF_RCV_CODE_MAX 16

/** One account record (ADR-019). A part is present when its field is not empty. */
typedef struct {
    uint16_t id;
    char name[MSF_NAME_MAX + 1];
    char totp_key[MSF_TOTP_KEY_MAX + 1];   /* Base32, empty = no TOTP */
    uint8_t digits;                         /* 6/8 */
    uint8_t period;                         /* 30/60 s */
    char username[MSF_USER_MAX + 1];
    char password[MSF_PASSWORD_MAX + 1];
    char pin[MSF_PIN_FIELD_MAX + 1];
    char note[MSF_NOTE_MAX + 1];
    char ident[MSF_IDENT_MAX + 1];
    char extra[MSF_EXTRA_MAX + 1];
    uint8_t rcv_count;
    uint32_t rcv_used_mask;                 /* bit i = code i used */
    char rcv_codes[MSF_RCV_MAX][MSF_RCV_CODE_MAX + 1];
} msf_account_t;

typedef enum {
    MSF_STORE_BLANK,      /* no store yet: create one */
    MSF_STORE_LOCKED,     /* a store exists, key not in RAM */
    MSF_STORE_UNLOCKED,   /* key in RAM: msf_store_save() works */
    MSF_STORE_ERROR,      /* partition missing or unreadable */
} msf_store_state_t;

/** @brief Reads the partition (control sector, slot headers). Call once at boot. */
esp_err_t msf_store_init(void);

msf_store_state_t msf_store_state(void);

/** @brief true if `pin` is 6 decimal digits. */
bool msf_store_pin_valid(const char *pin);

/**
 * @brief Creates an empty store protected by `pin` (BLANK only) and leaves
 *        it unlocked. ESP_ERR_INVALID_ARG for a bad PIN, ESP_ERR_INVALID_STATE
 *        if a store already exists.
 */
esp_err_t msf_store_create(const char *pin);

/**
 * @brief Checks `pin` and decrypts the records into `out` (up to `max`).
 * @return ESP_OK (unlocked, *count set), ESP_ERR_INVALID_RESPONSE (wrong
 *         PIN, counted), ESP_ERR_TIMEOUT (back-off running: wait
 *         msf_store_backoff_remaining_s()), ESP_ERR_INVALID_STATE (no
 *         store), ESP_ERR_NO_MEM, or a flash error.
 */
esp_err_t msf_store_unlock(const char *pin, msf_account_t *out, int max, int *count);

/** @brief Encrypts and writes `count` records (UNLOCKED only), into the other slot. */
esp_err_t msf_store_save(const msf_account_t *records, int count);

/**
 * @brief New PIN for the unlocked store: new salt, new key, records
 *        rewritten (the caller passes its current records).
 */
esp_err_t msf_store_change_pin(const char *new_pin, const msf_account_t *records, int count);

/**
 * @brief Re-checks the PIN of the UNLOCKED store (confirmation before a
 *        backup export or import, 2026-10-11) without changing its state.
 *        Same rules as an unlock: back-off, failures counted and persisted,
 *        so it is no faster way to try PINs.
 * @return ESP_OK, ESP_ERR_INVALID_RESPONSE (wrong PIN), ESP_ERR_TIMEOUT
 *         (back-off), ESP_ERR_INVALID_STATE (not unlocked).
 */
esp_err_t msf_store_verify_pin(const char *pin);

/**
 * @brief Copy of the newest store slot, exactly as on flash (header |
 *        ciphertext | tag), UNLOCKED only: the factory's rescue export
 *        (2026-10-11, user decision). Still encrypted with the PIN-derived
 *        key; tools/msf_backup.py decrypts it with the PIN. PSRAM, free with
 *        msf_backup_free().
 * @return ESP_ERR_INVALID_STATE (not unlocked), ESP_ERR_NO_MEM, ESP_FAIL.
 */
esp_err_t msf_store_rescue_image(uint8_t **image_out, size_t *len_out);

/** @brief Wipes the key from RAM. The caller wipes its decrypted records. */
void msf_store_lock(void);

/** @brief Consecutive wrong PINs (persisted). */
uint32_t msf_store_fail_count(void);

/** @brief Seconds before the next PIN attempt is accepted (0 = now). */
uint32_t msf_store_backoff_remaining_s(void);

/** @brief Erases the whole partition (dev / reset): back to BLANK. */
esp_err_t msf_store_erase(void);

/* ---- Backup file (.msfx, ADR-005 + amendment 2026-10-11) ------------------
 * A file image in RAM; reading/writing the SD card is the caller's job.
 * Plaintext header (authenticated as GCM AAD) | AES-256-GCM(records, same
 * TLV serialization as the store) | tag. Key = Argon2id(passphrase), its
 * own salt and more passes than the PIN's (used rarely). Format in
 * INTERFACES.md §3; tools/msf_backup.py reads and writes it on a PC. */

#define MSF_BACKUP_PASS_MIN 8
#define MSF_BACKUP_PASS_MAX 64

typedef struct {
    int64_t created;    /* UTC seconds, 0 = unknown (clock not set) */
    uint16_t count;     /* records in the file */
} msf_backup_info_t;

/**
 * @brief Encrypts `count` records with `passphrase` into a new file image
 *        (PSRAM, free it with msf_backup_free()). About 3 s (Argon2id).
 * @return ESP_ERR_INVALID_ARG (passphrase length), ESP_ERR_NO_MEM,
 *         ESP_ERR_INVALID_SIZE (records do not fit).
 */
esp_err_t msf_backup_seal(const char *passphrase, const msf_account_t *records, int count, int64_t created,
                          uint8_t **file_out, size_t *len_out);

/** @brief Reads the plaintext header only (no passphrase needed). */
esp_err_t msf_backup_peek(const uint8_t *file, size_t len, msf_backup_info_t *info);

/**
 * @brief Checks the passphrase and decrypts the records into `out`. Nothing
 *        else is touched: the caller decides what to do with them.
 * @return ESP_OK, ESP_ERR_INVALID_VERSION (not a backup, or a newer format),
 *         ESP_ERR_INVALID_RESPONSE (wrong passphrase, or the file was
 *         altered), ESP_ERR_INVALID_SIZE (more than `max` records),
 *         ESP_ERR_NO_MEM.
 */
esp_err_t msf_backup_open(const char *passphrase, const uint8_t *file, size_t len, msf_account_t *out, int max,
                          int *count, msf_backup_info_t *info);

void msf_backup_free(uint8_t *file);

#ifdef __cplusplus
}
#endif
