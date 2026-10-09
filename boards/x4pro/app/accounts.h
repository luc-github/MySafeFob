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
 *        record per account, unique name, optional TOTP / login / recovery
 *        codes, sorted by name, indexed by first letter).
 *
 *        For now backed by DUMMY data in RAM (accounts_dummy.c, ROADMAP 8.0
 *        P8): the same calls will be served by secret_store (INTERFACES.md
 *        §4.2) once the encrypted store exists, without changing the
 *        screens. No secret ever leaves this API except through
 *        accounts_get() (login) and accounts_totp() (code).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ACCOUNTS_NAME_MAX 48
#define ACCOUNTS_ALL_LETTERS 0   /* `letter` argument: every account */

typedef struct {
    uint16_t id;
    char name[ACCOUNTS_NAME_MAX + 1];
} account_summary_t;

typedef struct {
    uint16_t id;
    char name[ACCOUNTS_NAME_MAX + 1];
    bool has_totp;
    uint8_t digits;     /* 6/8 */
    uint8_t period;     /* 30/60 s */
    bool has_login;
    char username[65];
    char password[129];
    char notes[257];
    bool has_recovery;
    uint8_t rcv_total;
    uint8_t rcv_unused;
} account_t;

/** @brief Index letter of a name: its first character as 'A'-'Z', '#' otherwise. */
char accounts_index_letter(const char *name);

/** @brief Number of accounts under `letter` ('A'-'Z', '#', or ACCOUNTS_ALL_LETTERS). */
int accounts_count(char letter);

/**
 * @brief Fills `out` with up to `max` accounts under `letter`, sorted by
 *        name (case-insensitive), starting at the `first`-th one.
 * @return number of entries written.
 */
int accounts_list(char letter, int first, int max, account_summary_t *out);

/** @brief Full record (login in clear: only for the account page). */
bool accounts_get(uint16_t id, account_t *out);

/**
 * @brief TOTP code of account `id` at `utc`: `code` (>= 9 bytes) and the
 *        seconds left in its period. false if the account has no TOTP.
 */
bool accounts_totp(uint16_t id, time_t utc, char *code, int *seconds_left);

/** @brief Deletes the account. false if `id` does not exist. */
bool accounts_remove(uint16_t id);

#ifdef __cplusplus
}
#endif
