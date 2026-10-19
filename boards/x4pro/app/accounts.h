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
 *        Profiles (2026-10-17): every account belongs to one profile (a
 *        person sharing the device: "Luc", "Pat"...), empty = the default
 *        profile. A profile exists while at least one account uses it;
 *        names match case-insensitively. One profile is active at a time:
 *        accounts_count() and accounts_list() only see its accounts, and
 *        a name is unique within its profile. The active profile is kept
 *        across unlocks (settings_store, as a hash).
 *
 *        Backed by secret_store (accounts_store.c): the records are
 *        decrypted into RAM when the store is unlocked, every change is
 *        saved (encrypted) at once, and the RAM copy is wiped on lock.
 *        While locked every query returns nothing.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "secret_store.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ACCOUNTS_NAME_MAX MSF_NAME_MAX
#define ACCOUNTS_PROFILE_MAX MSF_PROFILE_MAX
#define ACCOUNTS_PROFILE_DEFAULT_LABEL "Default"   /* how the empty profile is shown */
#define ACCOUNTS_USER_MAX MSF_USER_MAX
#define ACCOUNTS_PASSWORD_MAX MSF_PASSWORD_MAX
#define ACCOUNTS_PIN_MAX MSF_PIN_FIELD_MAX     /* digits only */
#define ACCOUNTS_NOTE_MAX MSF_NOTE_MAX         /* one line: website, e-mail, short note */
#define ACCOUNTS_IDENT_MAX MSF_IDENT_MAX       /* ID: customer / account number, e-mail... */
#define ACCOUNTS_EXTRA_MAX MSF_EXTRA_MAX       /* Extra: one more free line */
#define ACCOUNTS_TOTP_KEY_MAX MSF_TOTP_KEY_MAX /* Base32 of a 64-byte secret */
#define ACCOUNTS_RCV_MAX MSF_RCV_MAX           /* recovery codes per account */
#define ACCOUNTS_RCV_CODE_MAX MSF_RCV_CODE_MAX
#define ACCOUNTS_ALL_LETTERS 0                 /* `letter` argument: every account */

typedef struct {
    uint16_t id;
    char name[ACCOUNTS_NAME_MAX + 1];
} account_summary_t;

/** The record itself is the store's (secret_store.h). */
typedef msf_account_t account_t;

typedef enum {
    ACCOUNTS_OK = 0,
    ACCOUNTS_ERR_NOT_FOUND,
    ACCOUNTS_ERR_NAME_EMPTY,
    ACCOUNTS_ERR_NAME_EXISTS,    /* same name in the same profile, case-insensitive */
    ACCOUNTS_ERR_TOTP_KEY,       /* not valid Base32, or too long */
    ACCOUNTS_ERR_FULL,
    ACCOUNTS_ERR_LOCKED,         /* store not unlocked */
    ACCOUNTS_ERR_STORE,          /* the encrypted write failed */
} accounts_result_t;

/* ---- Store state (secret_store) ---------------------------------------- */

/** @brief Opens the store (call once at boot, after the flash is usable). */
void accounts_init(void);

bool accounts_is_unlocked(void);
bool accounts_store_exists(void);

/**
 * @brief Unlocks with the device PIN (6 digits, ~1 s of key derivation).
 * @return ESP_OK, ESP_ERR_INVALID_RESPONSE (wrong PIN), ESP_ERR_TIMEOUT
 *         (back-off running), or another store error.
 */
int accounts_unlock(const char *pin);

/** @brief Creates the store protected by `pin` (none must exist) and unlocks it. */
int accounts_create(const char *pin);

/** @brief Changes the device PIN (unlocked only). */
int accounts_change_pin(const char *new_pin);

/** @brief Re-checks the device PIN while unlocked (before a backup export or import). */
int accounts_verify_pin(const char *pin);

/** @brief Wipes the decrypted records and the key from RAM. */
void accounts_lock(void);

/** @brief Adds the sample accounts (public test TOTP keys); skips existing names. */
int accounts_load_demo(void);

/* ---- Records ------------------------------------------------------------- */

/** @brief Empty record with the TOTP defaults (6 digits, 30 s). */
void accounts_init_record(account_t *a);

/** @brief Index letter of a name: its first character as 'A'-'Z', '#' otherwise. */
char accounts_index_letter(const char *name);

/** @brief Available (not yet used) recovery codes of `a`. */
int accounts_rcv_unused(const account_t *a);

/** @brief true if `b32` is a usable TOTP key (valid Base32, 1-64 bytes). */
bool accounts_totp_key_valid(const char *b32);

/** @brief Short English text for a result, for the status line. */
const char *accounts_result_text(accounts_result_t r);

/* ---- Profiles ------------------------------------------------------------ */

/** @brief Active profile's name ("" = default profile); "" while locked. */
const char *accounts_profile(void);

/** @brief Name to show for `profile`: ACCOUNTS_PROFILE_DEFAULT_LABEL for "". */
const char *accounts_profile_label(const char *profile);

/** @brief Number of distinct profiles in the store (0 while locked or empty). */
int accounts_profile_count(void);

/**
 * @brief Existing profile following `current` in name order (wraps
 *        around), into `out` (ACCOUNTS_PROFILE_MAX + 1 bytes).
 * @return false if the store holds no account.
 */
bool accounts_profile_after(const char *current, char *out);

/** @brief Makes the next profile active (HOME's profile button) and remembers it. */
void accounts_profile_next(void);

/**
 * @brief Up to `max` profiles in name order, from the `first`-th one, into
 *        `out` (Settings > Profiles).
 * @return number of entries written.
 */
int accounts_profile_list(int first, int max, char (*out)[ACCOUNTS_PROFILE_MAX + 1]);

/** @brief Number of accounts in `profile` (case-insensitive). */
int accounts_profile_size(const char *profile);

/**
 * @brief Moves every account of `from` to `to`, in one store write (none
 *        if it fails: nothing changes). `to` may be an existing profile
 *        (the two merge) or "" (the default one). The active profile
 *        follows when it was `from`.
 * @return ACCOUNTS_OK, ACCOUNTS_ERR_NOT_FOUND (no such profile),
 *         ACCOUNTS_ERR_NAME_EXISTS (a name of `from` is already in `to`:
 *         its name goes to `clash`, ACCOUNTS_NAME_MAX + 1 bytes, may be
 *         NULL), ACCOUNTS_ERR_LOCKED, ACCOUNTS_ERR_STORE.
 */
accounts_result_t accounts_profile_rename(const char *from, const char *to, char *clash);

/**
 * @brief Deletes `profile` WITH all its accounts, in one store write
 *        (irreversible; the caller confirms first). Records are wiped.
 * @return accounts removed, or -1 (locked, not found, write failed:
 *         nothing changed).
 */
int accounts_profile_delete(const char *profile);

/* ---- Queries (active profile only) --------------------------------------- */

/** @brief Number of accounts of every profile. */
int accounts_total(void);

/** @brief Number of active-profile accounts under `letter` ('A'-'Z', '#', or ACCOUNTS_ALL_LETTERS). */
int accounts_count(char letter);

/**
 * @brief Fills `out` with up to `max` active-profile accounts under
 *        `letter`, sorted by name (case-insensitive), starting at the
 *        `first`-th one.
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

/**
 * @brief Adds `in` as a new account; its id goes to *id_out. Its profile
 *        becomes the active one (as for accounts_update()).
 */
accounts_result_t accounts_add(const account_t *in, uint16_t *id_out);

/** @brief Replaces account `id` with `in` (in->id is ignored). */
accounts_result_t accounts_update(uint16_t id, const account_t *in);

/** @brief Marks recovery code `index` of account `id` as used (F-05b, irreversible). */
bool accounts_rcv_mark_used(uint16_t id, int index);

/**
 * @brief Removes recovery code `index` from `a` (a record being edited):
 *        later codes and their used bits shift down by one.
 */
void accounts_rcv_remove(account_t *a, int index);

/** @brief Appends `code` to `a`'s recovery codes (unused). false if full or empty. */
bool accounts_rcv_append(account_t *a, const char *code);

/** @brief Deletes the account. false if `id` does not exist or the write failed. */
bool accounts_remove(uint16_t id);

/* ---- Backup import / export (ADR-005 amendment 2026-10-11) -------------- */

/* Two modes (user decision 2026-10-11): the backup / PC JSON is the
 * reference. Merge never removes; Replace is the full sync -- of the
 * profiles the backup holds only (2026-10-17): a backup of Pat alone
 * replaces Pat and leaves the other profiles untouched. */
typedef enum {
    ACCOUNTS_IMPORT_MERGE,     /* new names added, differing records overwritten, the rest untouched */
    ACCOUNTS_IMPORT_REPLACE,   /* the backup's profiles become its content (removals included) */
} accounts_import_mode_t;

/* What an import does (preview) or did (import). Records match on
 * (profile, name), case-insensitively; a record is "changed" when any
 * field differs. */
typedef struct {
    int total;       /* records in the backup */
    int invalid;     /* unusable (empty name, bad TOTP key...), never imported */
    int added;       /* names not on the device */
    int changed;     /* names on the device with a different content */
    int unchanged;   /* names on the device with the same content */
    int removed;     /* Replace: device accounts of the backup's profiles not in the backup */
    int full;        /* new ones left out: no room */
} accounts_import_summary_t;

/** @brief Copies every record (unlocked only); returns the count. */
int accounts_export_all(account_t *out, int max);

/** @brief Copies the records of `profile` only (unlocked only); returns the count. */
int accounts_export_profile(const char *profile, account_t *out, int max);

/**
 * @brief Distinct profiles of `n` backup records, in name order, into `out`
 *        (up to `max`). Returns how many there are in total (may be > max).
 */
int accounts_records_profiles(const account_t *in, int n, char (*out)[ACCOUNTS_PROFILE_MAX + 1], int max);

/**
 * @brief Puts every one of `n` backup records in `profile`: a backup made
 *        without profiles (older firmware, a PC JSON) imported into the
 *        right person's profile. Before the preview and the import.
 */
void accounts_records_set_profile(account_t *in, int n, const char *profile);

/** @brief What accounts_import() would do, without changing anything. */
void accounts_import_preview(const account_t *in, int n, accounts_import_mode_t mode,
                             accounts_import_summary_t *sum);

/**
 * @brief Applies `n` backup records according to `mode`, in ONE store
 *        write (none at all if nothing changes): if it fails, the store
 *        and the RAM copy are unchanged. New records get new ids, updated
 *        ones keep theirs. *sum receives the outcome.
 */
accounts_result_t accounts_import(const account_t *in, int n, accounts_import_mode_t mode,
                                  accounts_import_summary_t *sum);

#ifdef __cplusplus
}
#endif
