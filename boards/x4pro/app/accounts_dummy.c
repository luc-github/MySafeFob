/*
 Project: MySafeFob  accounts_dummy.c
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
 * @file accounts_dummy.c
 * @brief MySafeFob App — DUMMY account records for the UI (ROADMAP 8.0 P8,
 *        ADR-019), see accounts.h. Records live in RAM (PSRAM, allocated on
 *        first use) and are seeded from a sample table: adds, edits and
 *        removals work but last until the next boot. The TOTP keys are
 *        public test values (RFC 6238 test key, common demo key). To be
 *        replaced by secret_store.
 *
 *        The samples cover: a letter with more than one page of names (G),
 *        accounts with one part or with all of them, a non-letter name
 *        ('#'), and letters with no account.
 */
#include "accounts.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "totp_engine.h"

#define MAX_ACCOUNTS 64

typedef struct {
    bool used;
    account_t rec;   /* rec.id = slot index + 1 */
} slot_t;

static slot_t *s_slots;   /* MAX_ACCOUNTS slots, PSRAM */

typedef struct {
    const char *name, *totp_key;
    uint8_t digits, period;
    const char *username, *password, *pin, *note;
    uint8_t rcv_count, rcv_used;
} sample_t;

#define RFC_KEY "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ" /* "12345678901234567890" */
#define DEMO_KEY "JBSWY3DPEHPK3PXP"

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

static bool ensure_init(void)
{
    if (s_slots) return true;
    s_slots = heap_caps_calloc(MAX_ACCOUNTS, sizeof(slot_t), MALLOC_CAP_SPIRAM);
    if (!s_slots) return false;
    for (size_t i = 0; i < sizeof(kSamples) / sizeof(kSamples[0]) && i < MAX_ACCOUNTS; i++) {
        const sample_t *s = &kSamples[i];
        account_t *a = &s_slots[i].rec;
        accounts_init_record(a);
        a->id = (uint16_t)(i + 1);
        strlcpy(a->name, s->name, sizeof(a->name));
        strlcpy(a->totp_key, s->totp_key, sizeof(a->totp_key));
        a->digits = s->digits;
        a->period = s->period;
        strlcpy(a->username, s->username, sizeof(a->username));
        strlcpy(a->password, s->password, sizeof(a->password));
        strlcpy(a->pin, s->pin, sizeof(a->pin));
        strlcpy(a->note, s->note, sizeof(a->note));
        a->rcv_count = s->rcv_count;
        for (int c = 0; c < s->rcv_count; c++) {
            /* Fake codes "1A2B-0000", "1A2B-0001"... */
            static const char kHex[] = "0123456789ABCDEF";
            char code[ACCOUNTS_RCV_CODE_MAX + 1] = "1A2B-0000";
            code[7] = kHex[(c >> 4) & 0xF];
            code[8] = kHex[c & 0xF];
            strlcpy(a->rcv_codes[c], code, sizeof(a->rcv_codes[c]));
            if (c < s->rcv_used) a->rcv_used_mask |= 1u << c;
        }
        s_slots[i].used = true;
    }
    return true;
}

static account_t *find(uint16_t id)
{
    if (!ensure_init() || id == 0 || id > MAX_ACCOUNTS || !s_slots[id - 1].used) return NULL;
    return &s_slots[id - 1].rec;
}

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
    }
    return "Error";
}

static bool matches(const slot_t *s, char letter)
{
    return s->used && (letter == ACCOUNTS_ALL_LETTERS || accounts_index_letter(s->rec.name) == letter);
}

int accounts_count(char letter)
{
    if (!ensure_init()) return 0;
    int n = 0;
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        if (matches(&s_slots[i], letter)) n++;
    }
    return n;
}

static int cmp_slots(const void *a, const void *b)
{
    return strcasecmp(s_slots[*(const int *)a].rec.name, s_slots[*(const int *)b].rec.name);
}

int accounts_list(char letter, int first, int max, account_summary_t *out)
{
    if (!ensure_init()) return 0;
    int order[MAX_ACCOUNTS];
    int n = 0;
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        if (matches(&s_slots[i], letter)) order[n++] = i;
    }
    qsort(order, n, sizeof(order[0]), cmp_slots);
    int written = 0;
    for (int k = first; k < n && written < max; k++) {
        out[written].id = s_slots[order[k]].rec.id;
        strlcpy(out[written].name, s_slots[order[k]].rec.name, sizeof(out[written].name));
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

/* Checks shared by add and update; `self` = id allowed to own the name. */
static accounts_result_t validate(const account_t *in, uint16_t self)
{
    if (!in->name[0]) return ACCOUNTS_ERR_NAME_EMPTY;
    if (in->totp_key[0] && !accounts_totp_key_valid(in->totp_key)) return ACCOUNTS_ERR_TOTP_KEY;
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        if (s_slots[i].used && s_slots[i].rec.id != self && strcasecmp(s_slots[i].rec.name, in->name) == 0) {
            return ACCOUNTS_ERR_NAME_EXISTS;
        }
    }
    return ACCOUNTS_OK;
}

accounts_result_t accounts_add(const account_t *in, uint16_t *id_out)
{
    if (!ensure_init()) return ACCOUNTS_ERR_FULL;
    accounts_result_t r = validate(in, 0);
    if (r != ACCOUNTS_OK) return r;
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        if (!s_slots[i].used) {
            s_slots[i].rec = *in;
            s_slots[i].rec.id = (uint16_t)(i + 1);
            s_slots[i].used = true;
            if (id_out) *id_out = s_slots[i].rec.id;
            return ACCOUNTS_OK;
        }
    }
    return ACCOUNTS_ERR_FULL;
}

accounts_result_t accounts_update(uint16_t id, const account_t *in)
{
    account_t *a = find(id);
    if (!a) return ACCOUNTS_ERR_NOT_FOUND;
    accounts_result_t r = validate(in, id);
    if (r != ACCOUNTS_OK) return r;
    *a = *in;
    a->id = id;
    return ACCOUNTS_OK;
}

bool accounts_remove(uint16_t id)
{
    account_t *a = find(id);
    if (!a) return false;
    memset(a, 0, sizeof(*a));   /* wipe the secrets, not just the flag */
    s_slots[id - 1].used = false;
    return true;
}
