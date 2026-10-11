/*
 Project: MySafeFob  cmd_backup.c
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
 * @file cmd_backup.c
 * @brief MySafeFob — `backup` console command: encrypted SD backups
 *        (ROADMAP 8.0 P11) before the on-device screens exist (step A).
 *
 *   backup list                              files in /MSF, newest first
 *   backup export <pin> <passphrase>         every account -> new .msfx
 *   backup show <file> <passphrase>          decrypt and summarize, no change
 *   backup import <file> <pin> <passphrase> merge|replace
 *
 * merge: new names added, differing accounts overwritten, the rest untouched.
 * replace: the store becomes the backup (accounts not in it are removed).
 *
 * export and import ask the device PIN again (2026-10-11): an unlocked
 * device on a serial cable must not be enough to copy every secret out, or
 * to slip modified accounts in. A wrong PIN counts like a wrong unlock
 * (failure counter, back-off). show changes nothing and reveals only names.
 *
 * Quote a passphrase that contains spaces. Development aid, like `store`:
 * the passphrase is typed on the PC here.
 */
#include "cmd_backup.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "backup.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "ui_nav.h"

static const char *err_text(esp_err_t err)
{
    switch (err) {
    case ESP_ERR_NOT_FOUND: return "No SD card, or file not found.";
    case ESP_ERR_INVALID_ARG: return "The passphrase must be 8 to 64 characters.";
    case ESP_ERR_INVALID_STATE: return "The store is locked.";
    case ESP_ERR_INVALID_RESPONSE: return "Wrong passphrase, or the file was altered.";
    case ESP_ERR_INVALID_VERSION: return "Not a MySafeFob backup, or a newer format.";
    case ESP_ERR_INVALID_SIZE: return "Too many accounts, or a damaged file.";
    case ESP_ERR_NO_MEM: return "Out of memory.";
    default: return "Write or read error.";
    }
}

static void print_date(int64_t created)
{
    if (created <= 0) {
        printf("(no date)");
        return;
    }
    time_t t = (time_t)created;
    struct tm tm;
    gmtime_r(&t, &tm);
    printf("%04d-%02d-%02d %02d:%02d UTC", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
}

static int do_list(void)
{
    backup_file_t files[16];
    int n = backup_list(files, 16);
    if (n < 0) {
        printf("%s\n", err_text(ESP_ERR_NOT_FOUND));
        return 1;
    }
    if (n == 0) printf("No backup in " BACKUP_DIR ".\n");
    for (int i = 0; i < n; i++) {
        printf("  %-26s %3u account(s)  ", files[i].name, files[i].count);
        print_date(files[i].created);
        printf("\n");
    }
    return 0;
}

/* The device PIN, re-checked before export / import. Prints why on failure. */
static bool pin_ok(const char *pin)
{
    printf("Checking the PIN...\n");
    esp_err_t err = accounts_verify_pin(pin);
    if (err == ESP_OK) return true;
    if (err == ESP_ERR_INVALID_RESPONSE) printf("Wrong PIN (%lu consecutive).\n", (unsigned long)msf_store_fail_count());
    else if (err == ESP_ERR_TIMEOUT) printf("Too many failures: wait %lu s.\n", (unsigned long)msf_store_backoff_remaining_s());
    else if (err == ESP_ERR_INVALID_STATE) printf("%s\n", err_text(ESP_ERR_INVALID_STATE));
    else printf("Error: %s\n", esp_err_to_name(err));
    return false;
}

/* show / import: decrypt into a PSRAM buffer, then summarize or merge. */
static int do_load(const char *name, const char *pass, const char *mode_text)
{
    accounts_import_mode_t mode = ACCOUNTS_IMPORT_MERGE;
    if (mode_text) {
        if (!strcmp(mode_text, "merge")) mode = ACCOUNTS_IMPORT_MERGE;
        else if (!strcmp(mode_text, "replace")) mode = ACCOUNTS_IMPORT_REPLACE;
        else {
            printf("Mode: merge | replace\n");
            return 1;
        }
    }
    if (!accounts_is_unlocked()) {
        printf("%s\n", err_text(ESP_ERR_INVALID_STATE));
        return 1;
    }
    account_t *recs = heap_caps_calloc(MSF_STORE_MAX_ACCOUNTS, sizeof(account_t), MALLOC_CAP_SPIRAM);
    if (!recs) {
        printf("%s\n", err_text(ESP_ERR_NO_MEM));
        return 1;
    }
    int n = 0;
    printf("Decrypting (a few seconds)...\n");
    esp_err_t err = backup_load(name, pass, recs, MSF_STORE_MAX_ACCOUNTS, &n);
    int rc = 1;
    if (err != ESP_OK) {
        printf("%s\n", err_text(err));
    } else {
        accounts_import_summary_t sum;
        if (!mode_text) {
            accounts_import_preview(recs, n, ACCOUNTS_IMPORT_MERGE, &sum);
            printf("%d account(s) in the backup, %d unusable.\n", sum.total, sum.invalid);
            printf("merge:   %d new, %d changed, %d unchanged", sum.added, sum.changed, sum.unchanged);
            if (sum.full) printf(", %d left out (no room)", sum.full);
            accounts_import_preview(recs, n, ACCOUNTS_IMPORT_REPLACE, &sum);
            printf("\nreplace: same, and %d account(s) removed from the device\n", sum.removed);
            rc = 0;
        } else {
            accounts_result_t r = accounts_import(recs, n, mode, &sum);
            if (r == ACCOUNTS_OK) {
                printf("Imported: %d new, %d changed, %d unchanged, %d removed, %d unusable, %d no room.\n",
                       sum.added, sum.changed, sum.unchanged, sum.removed, sum.invalid, sum.full);
                rc = 0;
            } else {
                printf("%s Nothing was changed.\n", accounts_result_text(r));
            }
        }
    }
    memset(recs, 0, MSF_STORE_MAX_ACCOUNTS * sizeof(account_t));
    heap_caps_free(recs);
    return rc;
}

static int cmd_backup(int argc, char **argv)
{
    board_activity_notify();
    const char *sub = argc > 1 ? argv[1] : "";
    if (!strcmp(sub, "list") && argc == 2) return do_list();
    if (!strcmp(sub, "export") && argc == 4) {
        if (!pin_ok(argv[2])) return 1;
        char name[BACKUP_NAME_MAX];
        printf("Encrypting (a few seconds)...\n");
        esp_err_t err = backup_export(argv[3], name);
        if (err != ESP_OK) {
            printf("%s\n", err_text(err));
            return 1;
        }
        printf("Written: " BACKUP_DIR "/%s\n", name);
        return 0;
    }
    if (!strcmp(sub, "show") && argc == 4) return do_load(argv[2], argv[3], NULL);
    if (!strcmp(sub, "import") && argc == 6) {
        if (!pin_ok(argv[3])) return 1;
        return do_load(argv[2], argv[4], argv[5]);
    }
    printf("Usage: backup list | export <pin> <passphrase> | show <file> <passphrase> |\n"
           "       import <file> <pin> <passphrase> merge|replace\n");
    return 1;
}

esp_err_t cmd_backup_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "backup",
        .help = "Encrypted SD backups: list | export <pin> <pass> | show <file> <pass> | "
                "import <file> <pin> <pass> merge|replace. Quote a passphrase with spaces.",
        .func = &cmd_backup,
    };
    return esp_console_cmd_register(&cmd);
}
