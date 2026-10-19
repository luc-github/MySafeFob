/*
 Project: MySafeFob  accounts_store.c
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
 * @file accounts_store.c
 * @brief MySafeFob App — accounts.h over secret_store (was the RAM-only
 *        dummy store, accounts_dummy.c, until 2026-10-10). While unlocked
 *        the records live decrypted in PSRAM (s_recs); every change is
 *        written to the encrypted store at once, and a failed write rolls
 *        the RAM copy back. Lock wipes them. The former sample data is
 *        now `accounts_load_demo()` (console `store demo`), never loaded
 *        by default.
 *
 *        Profiles (2026-10-17): s_profile is the active one; queries and
 *        the name-uniqueness check are limited to it. It is restored at
 *        unlock from settings_store's hash, and falls back to the first
 *        profile (name order) when no account uses it any more.
 *        "Default" (any case) IS the empty profile (2026-10-17): compared
 *        as "" everywhere and stored as "" (canon()), so typing it never
 *        makes a second "Default" next to the real one.
 */
#include "accounts.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "settings_store.h"
#include "totp_engine.h"

#include "app_log_workaround.h"

static const char *TAG = "accounts";

static account_t *s_recs;   /* MSF_STORE_MAX_ACCOUNTS records, PSRAM */
static int s_count;
static char s_profile[ACCOUNTS_PROFILE_MAX + 1];   /* active profile, "" = default */

/* ---- Store state -------------------------------------------------------- */

static bool ensure_buffer(void)
{
    if (!s_recs) s_recs = heap_caps_calloc(MSF_STORE_MAX_ACCOUNTS, sizeof(account_t), MALLOC_CAP_SPIRAM);
    return s_recs != NULL;
}

static void wipe_records(void)
{
    if (s_recs) memset(s_recs, 0, MSF_STORE_MAX_ACCOUNTS * sizeof(account_t));
    s_count = 0;
    memset(s_profile, 0, sizeof(s_profile));
}

static void restore_profile(void);

void accounts_init(void)
{
    msf_store_init();
    ensure_buffer();
}

bool accounts_is_unlocked(void)
{
    return msf_store_state() == MSF_STORE_UNLOCKED;
}

bool accounts_store_exists(void)
{
    msf_store_state_t st = msf_store_state();
    return st == MSF_STORE_LOCKED || st == MSF_STORE_UNLOCKED;
}

int accounts_unlock(const char *pin)
{
    if (!ensure_buffer()) return ESP_ERR_NO_MEM;
    wipe_records();
    int n = 0;
    esp_err_t err = msf_store_unlock(pin, s_recs, MSF_STORE_MAX_ACCOUNTS, &n);
    if (err == ESP_OK) {
        s_count = n;
        restore_profile();
    } else {
        wipe_records();
    }
    return err;
}

int accounts_create(const char *pin)
{
    if (!ensure_buffer()) return ESP_ERR_NO_MEM;
    wipe_records();
    return msf_store_create(pin);
}

int accounts_change_pin(const char *new_pin)
{
    return msf_store_change_pin(new_pin, s_recs, s_count);
}

int accounts_verify_pin(const char *pin)
{
    return msf_store_verify_pin(pin);
}

void accounts_lock(void)
{
    wipe_records();
    msf_store_lock();
}

/* Writes the current RAM records; false (and logged) if it failed. */
static bool persist(void)
{
    esp_err_t err = msf_store_save(s_recs, s_count);
    if (err != ESP_OK) ESP_LOGE(TAG, "save FAILED (%s)", esp_err_to_name(err));
    return err == ESP_OK;
}

static account_t *find(uint16_t id)
{
    if (!accounts_is_unlocked() || id == 0) return NULL;
    for (int i = 0; i < s_count; i++) {
        if (s_recs[i].id == id) return &s_recs[i];
    }
    return NULL;
}

/* ---- Records ------------------------------------------------------------ */

void accounts_init_record(account_t *a)
{
    memset(a, 0, sizeof(*a));
    a->digits = 6;
    a->period = 30;
}

char accounts_index_letter(const char *name)
{
    char c = (char)toupper((unsigned char)name[0]);
    return (c >= 'A' && c <= 'Z') ? c : '#';
}

int accounts_rcv_unused(const account_t *a)
{
    int n = 0;
    for (int i = 0; i < a->rcv_count; i++) {
        if (!(a->rcv_used_mask & (1u << i))) n++;
    }
    return n;
}

bool accounts_totp_key_valid(const char *b32)
{
    uint8_t secret[64];
    size_t len = sizeof(secret);
    bool ok = b32 && b32[0] && base32_decode(b32, secret, &len) == ESP_OK;
    memset(secret, 0, sizeof(secret));
    return ok;
}

const char *accounts_result_text(accounts_result_t r)
{
    switch (r) {
    case ACCOUNTS_OK: return "Saved";
    case ACCOUNTS_ERR_NOT_FOUND: return "Account not found";
    case ACCOUNTS_ERR_NAME_EMPTY: return "The name is empty";
    case ACCOUNTS_ERR_NAME_EXISTS: return "This name already exists";
    case ACCOUNTS_ERR_TOTP_KEY: return "Invalid TOTP key";
    case ACCOUNTS_ERR_FULL: return "No room for more accounts";
    case ACCOUNTS_ERR_LOCKED: return "Store locked";
    case ACCOUNTS_ERR_STORE: return "Write failed, not saved";
    }
    return "Error";
}

/* ---- Profiles ----------------------------------------------------------- */

/* The empty profile under the name it is shown with: "" for "Default". */
static const char *canon(const char *profile)
{
    return strcasecmp(profile, ACCOUNTS_PROFILE_DEFAULT_LABEL) == 0 ? "" : profile;
}

static void canon_in_place(char *profile)
{
    if (!canon(profile)[0]) profile[0] = '\0';
}

static bool profile_eq(const char *a, const char *b)
{
    return strcasecmp(canon(a), canon(b)) == 0;
}

/* What settings_store keeps: CRC32 of the lower-case name, 0 for "". */
static uint32_t profile_hash(const char *profile)
{
    profile = canon(profile);
    char low[ACCOUNTS_PROFILE_MAX + 1];
    size_t n = strnlen(profile, ACCOUNTS_PROFILE_MAX);
    if (n == 0) return 0;
    for (size_t i = 0; i < n; i++) low[i] = (char)tolower((unsigned char)profile[i]);
    return esp_rom_crc32_le(0, (const uint8_t *)low, n);
}

static bool profile_used(const char *profile)
{
    for (int i = 0; i < s_count; i++) {
        if (profile_eq(s_recs[i].profile, profile)) return true;
    }
    return false;
}

static void select_profile(const char *profile)
{
    profile = canon(profile);
    if (strcmp(s_profile, profile) == 0) return;
    strlcpy(s_profile, profile, sizeof(s_profile));
    settings_store_set_profile_hash(profile_hash(profile));
    ESP_LOGI(TAG, "active profile: %s", accounts_profile_label(profile));
}

/* Keeps the active profile one that some account uses (after a change). */
static void check_profile(void)
{
    char first[ACCOUNTS_PROFILE_MAX + 1];
    if (s_count == 0) {   /* nothing left: back to the default profile */
        select_profile("");
        return;
    }
    if (profile_used(s_profile)) return;
    if (accounts_profile_after(NULL, first)) select_profile(first);
}

/* At unlock: the profile whose hash settings_store kept, else the first. */
static void restore_profile(void)
{
    uint32_t want = settings_store_get_profile_hash();
    for (int i = 0; i < s_count; i++) {
        if (profile_hash(s_recs[i].profile) == want) {
            strlcpy(s_profile, s_recs[i].profile, sizeof(s_profile));
            return;
        }
    }
    s_profile[0] = '\0';
    check_profile();
}

const char *accounts_profile(void)
{
    return s_profile;
}

const char *accounts_profile_label(const char *profile)
{
    return profile[0] ? profile : ACCOUNTS_PROFILE_DEFAULT_LABEL;
}

int accounts_profile_count(void)
{
    if (!accounts_is_unlocked()) return 0;
    int n = 0;
    for (int i = 0; i < s_count; i++) {
        int j = 0;
        while (j < i && !profile_eq(s_recs[j].profile, s_recs[i].profile)) j++;
        if (j == i) n++;   /* first record of its profile */
    }
    return n;
}

/* `current` NULL: the first profile. */
bool accounts_profile_after(const char *current, char *out)
{
    const char *next = NULL, *first = NULL;
    if (!accounts_is_unlocked()) return false;
    for (int i = 0; i < s_count; i++) {
        const char *p = canon(s_recs[i].profile);
        if (!first || strcasecmp(p, first) < 0) first = p;
        if (current && strcasecmp(p, canon(current)) > 0 && (!next || strcasecmp(p, next) < 0)) next = p;
    }
    if (!first) return false;
    strlcpy(out, next ? next : first, ACCOUNTS_PROFILE_MAX + 1);
    return true;
}

void accounts_profile_next(void)
{
    char next[ACCOUNTS_PROFILE_MAX + 1];
    if (accounts_profile_after(s_profile, next)) select_profile(next);
}

int accounts_profile_list(int first, int max, char (*out)[ACCOUNTS_PROFILE_MAX + 1])
{
    char cur[ACCOUNTS_PROFILE_MAX + 1];
    if (!accounts_profile_after(NULL, cur)) return 0;
    int k = 0, written = 0;
    for (;;) {   /* walks the profiles in name order until it wraps */
        if (k >= first && written < max) strlcpy(out[written++], cur, ACCOUNTS_PROFILE_MAX + 1);
        k++;
        char next[ACCOUNTS_PROFILE_MAX + 1];
        accounts_profile_after(cur, next);
        if (written >= max || strcasecmp(next, cur) <= 0) break;
        strlcpy(cur, next, sizeof(cur));
    }
    return written;
}

int accounts_profile_size(const char *profile)
{
    if (!accounts_is_unlocked()) return 0;
    int n = 0;
    for (int i = 0; i < s_count; i++) {
        if (profile_eq(s_recs[i].profile, profile)) n++;
    }
    return n;
}

static bool same_key(const account_t *a, const char *profile, const char *name);

accounts_result_t accounts_profile_rename(const char *from, const char *to, char *clash)
{
    if (!accounts_is_unlocked()) return ACCOUNTS_ERR_LOCKED;
    if (!profile_used(from)) return ACCOUNTS_ERR_NOT_FOUND;
    to = canon(to);
    bool same = profile_eq(from, to);   /* spelling change only: no clash possible */
    for (int i = 0; i < s_count && !same; i++) {
        if (!profile_eq(s_recs[i].profile, from)) continue;
        for (int j = 0; j < s_count; j++) {
            if (same_key(&s_recs[j], to, s_recs[i].name)) {
                if (clash) strlcpy(clash, s_recs[i].name, ACCOUNTS_NAME_MAX + 1);
                return ACCOUNTS_ERR_NAME_EXISTS;
            }
        }
    }
    /* Old profile strings, to put back if the write fails. */
    char (*old)[ACCOUNTS_PROFILE_MAX + 1] = heap_caps_malloc((size_t)s_count * sizeof(*old), MALLOC_CAP_SPIRAM);
    if (!old) return ACCOUNTS_ERR_STORE;
    bool was_active = profile_eq(s_profile, from);
    int moved = 0;
    for (int i = 0; i < s_count; i++) {
        memcpy(old[i], s_recs[i].profile, sizeof(old[i]));
        if (profile_eq(s_recs[i].profile, from)) {
            strlcpy(s_recs[i].profile, to, sizeof(s_recs[i].profile));
            moved++;
        }
    }
    accounts_result_t r = ACCOUNTS_OK;
    if (!persist()) {
        for (int i = 0; i < s_count; i++) memcpy(s_recs[i].profile, old[i], sizeof(old[i]));
        r = ACCOUNTS_ERR_STORE;
    } else if (was_active) {
        select_profile(to);
    }
    heap_caps_free(old);
    ESP_LOGI(TAG, "profile rename: %d accounts %s", moved, r == ACCOUNTS_OK ? "moved" : "-- WRITE FAILED, unchanged");
    return r;
}

int accounts_profile_delete(const char *profile)
{
    if (!accounts_is_unlocked() || !profile_used(profile)) return -1;
    account_t *before = heap_caps_malloc((size_t)s_count * sizeof(account_t), MALLOC_CAP_SPIRAM);
    if (!before) return -1;
    int before_count = s_count;
    memcpy(before, s_recs, (size_t)s_count * sizeof(account_t));
    int kept = 0;
    for (int i = 0; i < s_count; i++) {
        if (!profile_eq(s_recs[i].profile, profile)) s_recs[kept++] = s_recs[i];
    }
    int removed = s_count - kept;
    memset(&s_recs[kept], 0, (size_t)removed * sizeof(account_t));   /* wipe the secrets, not just the count */
    s_count = kept;
    if (!persist()) {
        memcpy(s_recs, before, (size_t)before_count * sizeof(account_t));
        s_count = before_count;
        removed = -1;
    } else {
        check_profile();
    }
    memset(before, 0, (size_t)before_count * sizeof(account_t));
    heap_caps_free(before);
    ESP_LOGI(TAG, "profile delete: %d accounts%s", removed < 0 ? 0 : removed,
             removed < 0 ? " -- WRITE FAILED, unchanged" : " removed");
    return removed;
}

/* ---- Queries ------------------------------------------------------------ */

static bool matches(const account_t *a, char letter)
{
    if (!profile_eq(a->profile, s_profile)) return false;
    return letter == ACCOUNTS_ALL_LETTERS || accounts_index_letter(a->name) == letter;
}

int accounts_total(void)
{
    return accounts_is_unlocked() ? s_count : 0;
}

int accounts_count(char letter)
{
    if (!accounts_is_unlocked()) return 0;
    int n = 0;
    for (int i = 0; i < s_count; i++) {
        if (matches(&s_recs[i], letter)) n++;
    }
    return n;
}

static int cmp_recs(const void *a, const void *b)
{
    return strcasecmp(s_recs[*(const int *)a].name, s_recs[*(const int *)b].name);
}

int accounts_list(char letter, int first, int max, account_summary_t *out)
{
    if (!accounts_is_unlocked()) return 0;
    static int order[MSF_STORE_MAX_ACCOUNTS];   /* 1 KB: kept off the UI task's stack (UI task only) */
    int n = 0;
    for (int i = 0; i < s_count; i++) {
        if (matches(&s_recs[i], letter)) order[n++] = i;
    }
    qsort(order, n, sizeof(order[0]), cmp_recs);
    int written = 0;
    for (int k = first; k < n && written < max; k++) {
        out[written].id = s_recs[order[k]].id;
        strlcpy(out[written].name, s_recs[order[k]].name, sizeof(out[written].name));
        written++;
    }
    return written;
}

bool accounts_get(uint16_t id, account_t *out)
{
    const account_t *a = find(id);
    if (!a) return false;
    *out = *a;
    return true;
}

bool accounts_totp(uint16_t id, time_t utc, char *code, int *seconds_left)
{
    const account_t *a = find(id);
    if (!a || !a->totp_key[0]) return false;
    uint8_t secret[64];
    size_t len = sizeof(secret);
    if (base32_decode(a->totp_key, secret, &len) != ESP_OK) return false;
    uint8_t left = 0;
    esp_err_t err = totp_generate(secret, len, utc, a->digits, a->period, code, &left);
    memset(secret, 0, sizeof(secret));
    if (err != ESP_OK) return false;
    *seconds_left = left;
    return true;
}

static bool same_key(const account_t *a, const char *profile, const char *name)
{
    return profile_eq(a->profile, profile) && strcasecmp(a->name, name) == 0;
}

/* Checks shared by add and update; `self` = id allowed to own the name. */
static accounts_result_t validate(const account_t *in, uint16_t self)
{
    if (!in->name[0]) return ACCOUNTS_ERR_NAME_EMPTY;
    if (in->totp_key[0] && !accounts_totp_key_valid(in->totp_key)) return ACCOUNTS_ERR_TOTP_KEY;
    for (int i = 0; i < s_count; i++) {
        if (s_recs[i].id != self && same_key(&s_recs[i], in->profile, in->name)) return ACCOUNTS_ERR_NAME_EXISTS;
    }
    return ACCOUNTS_OK;
}

static uint16_t next_id(void)
{
    uint16_t max = 0;
    for (int i = 0; i < s_count; i++) {
        if (s_recs[i].id > max) max = s_recs[i].id;
    }
    return (uint16_t)(max + 1);
}

accounts_result_t accounts_add(const account_t *in, uint16_t *id_out)
{
    if (!accounts_is_unlocked()) return ACCOUNTS_ERR_LOCKED;
    accounts_result_t r = validate(in, 0);
    if (r != ACCOUNTS_OK) return r;
    if (s_count >= MSF_STORE_MAX_ACCOUNTS) return ACCOUNTS_ERR_FULL;
    s_recs[s_count] = *in;
    s_recs[s_count].id = next_id();
    canon_in_place(s_recs[s_count].profile);
    s_count++;
    if (!persist()) {
        s_count--;
        memset(&s_recs[s_count], 0, sizeof(account_t));
        return ACCOUNTS_ERR_STORE;
    }
    if (id_out) *id_out = s_recs[s_count - 1].id;
    select_profile(in->profile);   /* show what was just saved */
    return ACCOUNTS_OK;
}

accounts_result_t accounts_update(uint16_t id, const account_t *in)
{
    account_t *a = find(id);
    if (!a) return accounts_is_unlocked() ? ACCOUNTS_ERR_NOT_FOUND : ACCOUNTS_ERR_LOCKED;
    accounts_result_t r = validate(in, id);
    if (r != ACCOUNTS_OK) return r;
    account_t old = *a;
    *a = *in;
    a->id = id;
    canon_in_place(a->profile);
    if (!persist()) {
        *a = old;
        r = ACCOUNTS_ERR_STORE;
    } else {
        select_profile(in->profile);   /* show what was just saved */
    }
    memset(&old, 0, sizeof(old));
    return r;
}

bool accounts_rcv_mark_used(uint16_t id, int index)
{
    account_t *a = find(id);
    if (!a || index < 0 || index >= a->rcv_count) return false;
    uint32_t old = a->rcv_used_mask;
    a->rcv_used_mask |= 1u << index;
    if (!persist()) {
        a->rcv_used_mask = old;
        return false;
    }
    return true;
}

void accounts_rcv_remove(account_t *a, int index)
{
    if (index < 0 || index >= a->rcv_count) return;
    for (int i = index; i + 1 < a->rcv_count; i++) {
        memcpy(a->rcv_codes[i], a->rcv_codes[i + 1], sizeof(a->rcv_codes[i]));
    }
    a->rcv_count--;
    memset(a->rcv_codes[a->rcv_count], 0, sizeof(a->rcv_codes[a->rcv_count]));
    uint32_t low = a->rcv_used_mask & ((1u << index) - 1u);
    uint32_t high = index + 1 < 32 ? (a->rcv_used_mask >> (index + 1)) << index : 0;
    a->rcv_used_mask = low | high;
}

bool accounts_rcv_append(account_t *a, const char *code)
{
    if (!code || !code[0] || a->rcv_count >= ACCOUNTS_RCV_MAX) return false;
    strlcpy(a->rcv_codes[a->rcv_count], code, sizeof(a->rcv_codes[a->rcv_count]));
    a->rcv_used_mask &= ~(1u << a->rcv_count);
    a->rcv_count++;
    return true;
}

bool accounts_remove(uint16_t id)
{
    account_t *a = find(id);
    if (!a) return false;
    int i = (int)(a - s_recs);
    account_t removed = *a;
    memmove(&s_recs[i], &s_recs[i + 1], (size_t)(s_count - i - 1) * sizeof(account_t));
    s_count--;
    memset(&s_recs[s_count], 0, sizeof(account_t));   /* wipe the secrets, not just the count */
    bool ok = persist();
    if (!ok) {   /* put it back where it was */
        memmove(&s_recs[i + 1], &s_recs[i], (size_t)(s_count - i) * sizeof(account_t));
        s_recs[i] = removed;
        s_count++;
    }
    memset(&removed, 0, sizeof(removed));
    if (ok) check_profile();
    return ok;
}

/* ---- Backup import / export -------------------------------------------- */

int accounts_export_all(account_t *out, int max)
{
    if (!accounts_is_unlocked()) return 0;
    int n = s_count < max ? s_count : max;
    memcpy(out, s_recs, (size_t)n * sizeof(account_t));
    return n;
}

int accounts_export_profile(const char *profile, account_t *out, int max)
{
    if (!accounts_is_unlocked()) return 0;
    int n = 0;
    for (int i = 0; i < s_count && n < max; i++) {
        if (profile_eq(s_recs[i].profile, profile)) out[n++] = s_recs[i];
    }
    return n;
}

int accounts_records_profiles(const account_t *in, int n, char (*out)[ACCOUNTS_PROFILE_MAX + 1], int max)
{
    /* Same walk as accounts_profile_after(), over the backup's records. */
    const char *cur = NULL;
    int total = 0;
    for (;;) {
        const char *next = NULL;
        for (int i = 0; i < n; i++) {
            const char *p = canon(in[i].profile);
            if ((!cur || strcasecmp(p, cur) > 0) && (!next || strcasecmp(p, next) < 0)) next = p;
        }
        if (!next) break;
        if (total < max) strlcpy(out[total], next, ACCOUNTS_PROFILE_MAX + 1);
        total++;
        cur = next;
    }
    return total;
}

void accounts_records_set_profile(account_t *in, int n, const char *profile)
{
    for (int i = 0; i < n; i++) strlcpy(in[i].profile, canon(profile), sizeof(in[i].profile));
}

/* A backup record the store can take: same checks as add/update, plus sane
 * TOTP parameters (a PC-made file may carry anything). */
static bool import_valid(const account_t *a)
{
    if (!a->name[0]) return false;
    if (a->totp_key[0] && !accounts_totp_key_valid(a->totp_key)) return false;
    if (a->totp_key[0] && (a->digits < 6 || a->digits > 8 || a->period == 0)) return false;
    return a->rcv_count <= ACCOUNTS_RCV_MAX;
}

static int find_key(const account_t *a)
{
    for (int i = 0; i < s_count; i++) {
        if (same_key(&s_recs[i], a->profile, a->name)) return i;
    }
    return -1;
}

/* Field by field: bytes after a string's terminator may differ. */
static bool same_record(const account_t *a, const account_t *b)
{
    if (strcmp(a->name, b->name) || strcmp(canon(a->profile), canon(b->profile)) || strcmp(a->totp_key, b->totp_key) || strcmp(a->username, b->username) ||
        strcmp(a->password, b->password) || strcmp(a->pin, b->pin) || strcmp(a->note, b->note) ||
        strcmp(a->ident, b->ident) || strcmp(a->extra, b->extra)) {
        return false;
    }
    if (a->totp_key[0] && (a->digits != b->digits || a->period != b->period)) return false;
    if (a->rcv_count != b->rcv_count) return false;
    uint32_t used = a->rcv_count >= 32 ? 0xFFFFFFFFu : (1u << a->rcv_count) - 1u;
    if ((a->rcv_used_mask & used) != (b->rcv_used_mask & used)) return false;
    for (int i = 0; i < a->rcv_count; i++) {
        if (strcmp(a->rcv_codes[i], b->rcv_codes[i])) return false;
    }
    return true;
}

/* Replace only touches the profiles the backup holds (2026-10-17). */
static bool profile_in_backup(const account_t *in, int n, const char *profile)
{
    for (int i = 0; i < n; i++) {
        if (profile_eq(in[i].profile, profile) && import_valid(&in[i])) return true;
    }
    return false;
}

/* A device record Replace removes: its profile is in the backup, it is not. */
static bool replace_removes(const account_t *in, int n, const account_t *rec);

static bool in_backup(const account_t *in, int n, const account_t *rec)
{
    for (int i = 0; i < n; i++) {
        if (same_key(&in[i], rec->profile, rec->name) && import_valid(&in[i])) return true;
    }
    return false;
}

static bool replace_removes(const account_t *in, int n, const account_t *rec)
{
    return profile_in_backup(in, n, rec->profile) && !in_backup(in, n, rec);
}

void accounts_import_preview(const account_t *in, int n, accounts_import_mode_t mode,
                             accounts_import_summary_t *sum)
{
    memset(sum, 0, sizeof(*sum));
    sum->total = n;
    int room = MSF_STORE_MAX_ACCOUNTS - s_count;
    for (int i = 0; i < n; i++) {
        if (!import_valid(&in[i])) {
            sum->invalid++;
            continue;
        }
        int at = find_key(&in[i]);
        if (at >= 0 && same_record(&s_recs[at], &in[i])) sum->unchanged++;
        else if (at >= 0) sum->changed++;
        else sum->added++;
    }
    if (mode == ACCOUNTS_IMPORT_REPLACE) {
        for (int i = 0; i < s_count; i++) {
            if (replace_removes(in, n, &s_recs[i])) sum->removed++;
        }
        room += sum->removed;
    }
    if (sum->added > room) {
        sum->full = sum->added - room;
        sum->added = room;
    }
}

accounts_result_t accounts_import(const account_t *in, int n, accounts_import_mode_t mode,
                                  accounts_import_summary_t *sum)
{
    if (!accounts_is_unlocked()) {
        memset(sum, 0, sizeof(*sum));
        return ACCOUNTS_ERR_LOCKED;
    }
    accounts_import_preview(in, n, mode, sum);
    if (sum->added == 0 && sum->changed == 0 && sum->removed == 0) return ACCOUNTS_OK;   /* nothing to write */

    /* Kept to put back if the single write below fails. */
    account_t *before = heap_caps_malloc(MSF_STORE_MAX_ACCOUNTS * sizeof(account_t), MALLOC_CAP_SPIRAM);
    if (!before) return ACCOUNTS_ERR_STORE;
    memcpy(before, s_recs, MSF_STORE_MAX_ACCOUNTS * sizeof(account_t));
    int before_count = s_count;

    if (mode == ACCOUNTS_IMPORT_REPLACE) {   /* drop what the backup does not have, keep ids of the rest */
        int kept = 0;
        for (int i = 0; i < s_count; i++) {
            if (!replace_removes(in, n, &s_recs[i])) s_recs[kept++] = s_recs[i];
        }
        memset(&s_recs[kept], 0, (size_t)(s_count - kept) * sizeof(account_t));
        s_count = kept;
    }
    for (int i = 0; i < n; i++) {
        const account_t *a = &in[i];
        if (!import_valid(a)) continue;
        int at = find_key(a);
        if (at >= 0) {
            uint16_t id = s_recs[at].id;
            s_recs[at] = *a;
            s_recs[at].id = id;
            canon_in_place(s_recs[at].profile);
        } else if (s_count < MSF_STORE_MAX_ACCOUNTS) {
            s_recs[s_count] = *a;
            s_recs[s_count].id = next_id();
            canon_in_place(s_recs[s_count].profile);
            s_count++;
        }
    }

    accounts_result_t r = ACCOUNTS_OK;
    if (!persist()) {
        memcpy(s_recs, before, MSF_STORE_MAX_ACCOUNTS * sizeof(account_t));
        s_count = before_count;
        r = ACCOUNTS_ERR_STORE;
    }
    check_profile();
    memset(before, 0, MSF_STORE_MAX_ACCOUNTS * sizeof(account_t));
    heap_caps_free(before);
    ESP_LOGI(TAG, "import (%s): %d added, %d changed, %d unchanged, %d removed, %d invalid, %d no room%s",
             mode == ACCOUNTS_IMPORT_REPLACE ? "replace" : "merge", sum->added, sum->changed, sum->unchanged,
             sum->removed, sum->invalid, sum->full, r == ACCOUNTS_OK ? "" : " -- WRITE FAILED, unchanged");
    return r;
}

/* ---- Demo data (console `store demo`) ----------------------------------- */

typedef struct {
    const char *name, *totp_key;
    uint8_t digits, period;
    const char *username, *password, *pin, *note;
    uint8_t rcv_count, rcv_used;
} sample_t;

#define RFC_KEY "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ" /* "12345678901234567890" */
#define DEMO_KEY "JBSWY3DPEHPK3PXP"

/* Public test TOTP keys only. Covers: a letter with two pages (G), one-part
 * and all-part accounts, a '#' name. */
static const sample_t kSamples[] = {
    {"GitHub", RFC_KEY, 6, 30, "octocat", "correct-horse-battery", "", "github.com", 10, 1},
    {"GitLab", DEMO_KEY, 6, 30, "", "", "", "", 0, 0},
    {"Google perso", DEMO_KEY, 6, 30, "luc.perso", "Pa55word!", "", "", 0, 0},
    {"Google pro", "", 6, 30, "luc.pro", "Pr0-Secret", "", "SSO via Okta", 0, 0},
    {"Gandi", RFC_KEY, 8, 30, "", "", "", "", 0, 0},
    {"Garmin", "", 6, 30, "runner", "5k-every-day", "", "", 0, 0},
    {"Gmail old", "", 6, 30, "old.address", "legacy", "", "Do not use", 6, 4},
    {"AWS", RFC_KEY, 6, 30, "", "", "", "", 0, 0},
    {"Amazon", "", 6, 30, "luc", "buy-more", "", "", 0, 0},
    {"Bank", "", 6, 30, "12345678", "0000-not-real", "1234", "Card ending 0000", 0, 0},
    {"Bitwarden", DEMO_KEY, 6, 30, "", "", "", "", 8, 0},
    {"Cloudflare", RFC_KEY, 6, 30, "admin@example.com", "cf-pass", "", "", 0, 0},
    {"Discord", DEMO_KEY, 6, 30, "", "", "", "", 0, 0},
    {"Dropbox", "", 6, 30, "luc", "box-pass", "", "", 0, 0},
    {"Home Wi-Fi", "", 6, 30, "MySSID", "wifi-pass-123", "", "WPA2", 0, 0},
    {"Mastodon", DEMO_KEY, 6, 60, "", "", "", "", 0, 0},
    {"Microsoft", RFC_KEY, 6, 30, "luc@example.com", "ms-pass", "", "", 0, 0},
    {"NAS admin", "", 6, 30, "admin", "nas-pass", "", "192.168.1.10", 0, 0},
    {"PayPal", RFC_KEY, 6, 30, "", "", "", "", 0, 0},
    {"Proton", DEMO_KEY, 6, 30, "luc@proton.me", "proton-pass", "", "", 10, 0},
    {"Reddit", "", 6, 30, "u_luc", "reddit-pass", "", "", 0, 0},
    {"1und1", "", 6, 30, "kunde", "einsundeins", "", "", 0, 0},
};

int accounts_load_demo(void)
{
    if (!accounts_is_unlocked()) return -1;
    int added = 0;
    for (size_t i = 0; i < sizeof(kSamples) / sizeof(kSamples[0]) && s_count < MSF_STORE_MAX_ACCOUNTS; i++) {
        const sample_t *s = &kSamples[i];
        account_t a;
        accounts_init_record(&a);
        strlcpy(a.name, s->name, sizeof(a.name));
        if (validate(&a, 0) != ACCOUNTS_OK) continue;   /* name already there */
        strlcpy(a.totp_key, s->totp_key, sizeof(a.totp_key));
        a.digits = s->digits;
        a.period = s->period;
        strlcpy(a.username, s->username, sizeof(a.username));
        strlcpy(a.password, s->password, sizeof(a.password));
        strlcpy(a.pin, s->pin, sizeof(a.pin));
        strlcpy(a.note, s->note, sizeof(a.note));
        for (int c = 0; c < s->rcv_count; c++) {
            static const char kHex[] = "0123456789ABCDEF";
            char code[ACCOUNTS_RCV_CODE_MAX + 1] = "1A2B-0000";
            code[7] = kHex[(c >> 4) & 0xF];
            code[8] = kHex[c & 0xF];
            accounts_rcv_append(&a, code);
            if (c < s->rcv_used) a.rcv_used_mask |= 1u << c;
        }
        a.id = next_id();
        s_recs[s_count++] = a;
        added++;
    }
    if (added && !persist()) {   /* one write for the whole batch */
        s_count -= added;
        memset(&s_recs[s_count], 0, (size_t)added * sizeof(account_t));
        return -1;
    }
    return added;
}
