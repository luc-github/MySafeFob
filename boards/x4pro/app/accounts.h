/*
 Project: MySafeFob  accounts.h
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
 * @file accounts.h
 * @brief MySafeFob App — account records as the UI sees them (ADR-019: one
 *        record per account, unique name, optional TOTP / login / PIN /
 *        note / recovery codes, sorted by name, indexed by first letter).
 *        A part is present when its field is not empty.
 *
 *        For now backed by DUMMY data in RAM (accounts_dummy.c, ROADMAP 8.0
 *        P8): the same calls will be served by secret_store (INTERFACES.md
 *        §4.2) once the encrypted store exists, without changing the
 *        screens.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ACCOUNTS_NAME_MAX 48
#define ACCOUNTS_USER_MAX 64
#define ACCOUNTS_PASSWORD_MAX 128
#define ACCOUNTS_PIN_MAX 12          /* digits only */
#define ACCOUNTS_NOTE_MAX 64         /* one line: website, e-mail, short note */
#define ACCOUNTS_TOTP_KEY_MAX 103    /* Base32 of a 64-byte secret */
#define ACCOUNTS_RCV_MAX 32          /* recovery codes per account */
#define ACCOUNTS_RCV_CODE_MAX 16
#define ACCOUNTS_ALL_LETTERS 0       /* `letter` argument: every account */

typedef struct {
    uint16_t id;
    char name[ACCOUNTS_NAME_MAX + 1];
} account_summary_t;

typedef struct {
    uint16_t id;
    char name[ACCOUNTS_NAME_MAX + 1];
    char totp_key[ACCOUNTS_TOTP_KEY_MAX + 1];   /* Base32, empty = no TOTP */
    uint8_t digits;                              /* 6/8 */
    uint8_t period;                              /* 30/60 s */
    char username[ACCOUNTS_USER_MAX + 1];
    char password[ACCOUNTS_PASSWORD_MAX + 1];
    char pin[ACCOUNTS_PIN_MAX + 1];
    char note[ACCOUNTS_NOTE_MAX + 1];
    uint8_t rcv_count;
    uint32_t rcv_used_mask;                      /* bit i = code i used */
    char rcv_codes[ACCOUNTS_RCV_MAX][ACCOUNTS_RCV_CODE_MAX + 1];
} account_t;

typedef enum {
    ACCOUNTS_OK = 0,
    ACCOUNTS_ERR_NOT_FOUND,
    ACCOUNTS_ERR_NAME_EMPTY,
    ACCOUNTS_ERR_NAME_EXISTS,    /* same name, case-insensitive */
    ACCOUNTS_ERR_TOTP_KEY,       /* not valid Base32, or too long */
    ACCOUNTS_ERR_FULL,
} accounts_result_t;

/** @brief Empty record with the TOTP defaults (6 digits, 30 s). */
void accounts_init_record(account_t *a);

/** @brief Index letter of a name: its first character as 'A'-'Z', '#' otherwise. */
char accounts_index_letter(const char *name);

/** @brief Unused recovery codes of `a`. */
int accounts_rcv_unused(const account_t *a);

/** @brief true if `b32` is a usable TOTP key (valid Base32, 1-64 bytes). */
bool accounts_totp_key_valid(const char *b32);

/** @brief Short English text for a result, for the status line. */
const char *accounts_result_text(accounts_result_t r);

/** @brief Number of accounts under `letter` ('A'-'Z', '#', or ACCOUNTS_ALL_LETTERS). */
int accounts_count(char letter);

/**
 * @brief Fills `out` with up to `max` accounts under `letter`, sorted by
 *        name (case-insensitive), starting at the `first`-th one.
 * @return number of entries written.
 */
int accounts_list(char letter, int first, int max, account_summary_t *out);

/** @brief Full record, secrets in clear: only for the account and edit pages. */
bool accounts_get(uint16_t id, account_t *out);

/**
 * @brief TOTP code of account `id` at `utc`: `code` (>= 9 bytes) and the
 *        seconds left in its period. false if the account has no TOTP.
 */
bool accounts_totp(uint16_t id, time_t utc, char *code, int *seconds_left);

/** @brief Adds `in` as a new account; its id goes to *id_out. */
accounts_result_t accounts_add(const account_t *in, uint16_t *id_out);

/** @brief Replaces account `id` with `in` (in->id is ignored). */
accounts_result_t accounts_update(uint16_t id, const account_t *in);

/** @brief Deletes the account. false if `id` does not exist. */
bool accounts_remove(uint16_t id);

#ifdef __cplusplus
}
#endif
