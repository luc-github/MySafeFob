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
 *        ADR-019), see accounts.h. Fixed sample data in RAM, removals last
 *        until the next boot. The TOTP secrets are public test values
 *        (RFC 6238 test key, common demo key): the codes are real codes for
 *        those keys, nothing else. To be replaced by secret_store.
 *
 *        The data covers: a letter with more than one page of names (G),
 *        accounts with only one part, with all three, a non-letter name
 *        ('#'), and letters with no account.
 */
#include "accounts.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "totp_engine.h"

typedef struct {
    const char *name;
    const char *totp_b32;    /* NULL = no TOTP */
    uint8_t digits;
    uint8_t period;
    const char *username;    /* NULL = no login */
    const char *password;
    const char *notes;
    uint8_t rcv_total;       /* 0 = no recovery codes */
    uint8_t rcv_unused;
    bool removed;
} dummy_account_t;

#define RFC_KEY "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ" /* "12345678901234567890" */
#define DEMO_KEY "JBSWY3DPEHPK3PXP"

static dummy_account_t s_accounts[] = {
    {"GitHub", RFC_KEY, 6, 30, "octocat", "correct-horse-battery", "", 10, 9, false},
    {"GitLab", DEMO_KEY, 6, 30, NULL, NULL, NULL, 0, 0, false},
    {"Google perso", DEMO_KEY, 6, 30, "luc.perso", "Pa55word!", "", 0, 0, false},
    {"Google pro", NULL, 0, 0, "luc.pro", "Pr0-Secret", "SSO via Okta", 0, 0, false},
    {"Gandi", RFC_KEY, 8, 30, NULL, NULL, NULL, 0, 0, false},
    {"Garmin", NULL, 0, 0, "runner", "5k-every-day", "", 0, 0, false},
    {"Gmail old", NULL, 0, 0, "old.address", "legacy", "Do not use", 6, 2, false},
    {"AWS", RFC_KEY, 6, 30, NULL, NULL, NULL, 0, 0, false},
    {"Amazon", NULL, 0, 0, "luc", "buy-more", "", 0, 0, false},
    {"Bank", NULL, 0, 0, "12345678", "0000-not-real", "Card PIN not stored here", 0, 0, false},
    {"Bitwarden", DEMO_KEY, 6, 30, NULL, NULL, NULL, 8, 8, false},
    {"Cloudflare", RFC_KEY, 6, 30, "admin@example.com", "cf-pass", "", 0, 0, false},
    {"Discord", DEMO_KEY, 6, 30, NULL, NULL, NULL, 0, 0, false},
    {"Dropbox", NULL, 0, 0, "luc", "box-pass", "", 0, 0, false},
    {"Home Wi-Fi", NULL, 0, 0, "MySSID", "wifi-pass-123", "WPA2", 0, 0, false},
    {"Mastodon", DEMO_KEY, 6, 60, NULL, NULL, NULL, 0, 0, false},
    {"Microsoft", RFC_KEY, 6, 30, "luc@example.com", "ms-pass", "", 0, 0, false},
    {"NAS admin", NULL, 0, 0, "admin", "nas-pass", "192.168.1.10", 0, 0, false},
    {"PayPal", RFC_KEY, 6, 30, NULL, NULL, NULL, 0, 0, false},
    {"Proton", DEMO_KEY, 6, 30, "luc@proton.me", "proton-pass", "", 10, 10, false},
    {"Reddit", NULL, 0, 0, "u_luc", "reddit-pass", "", 0, 0, false},
    {"1und1", NULL, 0, 0, "kunde", "einsundeins", "", 0, 0, false},
};
#define ACCOUNT_COUNT ((int)(sizeof(s_accounts) / sizeof(s_accounts[0])))

/* Indexes into s_accounts sorted by name (case-insensitive); id = index + 1. */
static int s_sorted[ACCOUNT_COUNT];
static bool s_sorted_ready;

static int cmp_names(const void *a, const void *b)
{
    return strcasecmp(s_accounts[*(const int *)a].name, s_accounts[*(const int *)b].name);
}

static void ensure_sorted(void)
{
    if (s_sorted_ready) return;
    for (int i = 0; i < ACCOUNT_COUNT; i++) s_sorted[i] = i;
    qsort(s_sorted, ACCOUNT_COUNT, sizeof(s_sorted[0]), cmp_names);
    s_sorted_ready = true;
}

static dummy_account_t *find(uint16_t id)
{
    if (id == 0 || id > ACCOUNT_COUNT) return NULL;
    dummy_account_t *a = &s_accounts[id - 1];
    return a->removed ? NULL : a;
}

char accounts_index_letter(const char *name)
{
    char c = (char)toupper((unsigned char)name[0]);
    return (c >= 'A' && c <= 'Z') ? c : '#';
}

static bool matches(const dummy_account_t *a, char letter)
{
    return !a->removed && (letter == ACCOUNTS_ALL_LETTERS || accounts_index_letter(a->name) == letter);
}

int accounts_count(char letter)
{
    int n = 0;
    for (int i = 0; i < ACCOUNT_COUNT; i++) {
        if (matches(&s_accounts[i], letter)) n++;
    }
    return n;
}

int accounts_list(char letter, int first, int max, account_summary_t *out)
{
    ensure_sorted();
    int seen = 0, written = 0;
    for (int k = 0; k < ACCOUNT_COUNT && written < max; k++) {
        const dummy_account_t *a = &s_accounts[s_sorted[k]];
        if (!matches(a, letter)) continue;
        if (seen++ < first) continue;
        out[written].id = (uint16_t)(s_sorted[k] + 1);
        strlcpy(out[written].name, a->name, sizeof(out[written].name));
        written++;
    }
    return written;
}

bool accounts_get(uint16_t id, account_t *out)
{
    const dummy_account_t *a = find(id);
    if (!a) return false;
    memset(out, 0, sizeof(*out));
    out->id = id;
    strlcpy(out->name, a->name, sizeof(out->name));
    out->has_totp = a->totp_b32 != NULL;
    out->digits = a->digits;
    out->period = a->period;
    out->has_login = a->username != NULL;
    if (out->has_login) {
        strlcpy(out->username, a->username, sizeof(out->username));
        strlcpy(out->password, a->password, sizeof(out->password));
        strlcpy(out->notes, a->notes ? a->notes : "", sizeof(out->notes));
    }
    out->has_recovery = a->rcv_total > 0;
    out->rcv_total = a->rcv_total;
    out->rcv_unused = a->rcv_unused;
    return true;
}

bool accounts_totp(uint16_t id, time_t utc, char *code, int *seconds_left)
{
    const dummy_account_t *a = find(id);
    if (!a || !a->totp_b32) return false;
    uint8_t secret[64];
    size_t len = sizeof(secret);
    if (base32_decode(a->totp_b32, secret, &len) != ESP_OK) return false;
    uint8_t left = 0;
    esp_err_t err = totp_generate(secret, len, utc, a->digits, a->period, code, &left);
    memset(secret, 0, sizeof(secret));
    if (err != ESP_OK) return false;
    *seconds_left = left;
    return true;
}

bool accounts_remove(uint16_t id)
{
    dummy_account_t *a = find(id);
    if (!a) return false;
    a->removed = true;
    return true;
}
