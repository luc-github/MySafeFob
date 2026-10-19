/*
 Project: MySafeFob  cmd_store.c
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
 * @file cmd_store.c
 * @brief MySafeFob — `store` console command: the encrypted account store
 *        (secret_store) before the UNLOCK screen exists (ROADMAP 8.2 step A).
 *
 *   store                    state, accounts, failed attempts, back-off
 *   store create <pin>       new store (blank only), unlocked
 *   store unlock <pin>
 *   store lock
 *   store passwd <new-pin>   (unlocked)
 *   store demo               add the sample accounts (unlocked)
 *   store erase ERASE        wipe the store: every account is lost
 *
 * Development aid: the device PIN is typed on the PC here. Whether these
 * subcommands stay in a release build is to be decided with the UNLOCK
 * screen (step B).
 */
#include "cmd_store.h"

#include <stdio.h>
#include <string.h>

#include "accounts.h"
#include "esp_console.h"
#include "ui_nav.h"

static void print_status(void)
{
    static const char *const kState[] = {"blank (no store)", "locked", "unlocked", "ERROR (partition)"};
    msf_store_state_t st = msf_store_state();
    printf("Store     %s\n", kState[st]);
    if (st == MSF_STORE_UNLOCKED) printf("Accounts  %d (%d profiles, active: %s)\n", accounts_total(), accounts_profile_count(), accounts_profile_label(accounts_profile()));
    printf("Failures  %lu", (unsigned long)msf_store_fail_count());
    uint32_t wait = msf_store_backoff_remaining_s();
    if (wait) printf(" (next attempt in %lu s)", (unsigned long)wait);
    printf("\n");
}

static int report(int err, const char *ok_text)
{
    if (err == ESP_OK) {
        printf("%s\n", ok_text);
        print_status();
        return 0;
    }
    if (err == ESP_ERR_INVALID_RESPONSE) printf("Wrong PIN.\n");
    else if (err == ESP_ERR_TIMEOUT) printf("Too many failures: wait %lu s.\n",
                                           (unsigned long)msf_store_backoff_remaining_s());
    else if (err == ESP_ERR_INVALID_ARG) printf("The PIN must be %d digits.\n", MSF_PIN_LEN);
    else if (err == ESP_ERR_INVALID_STATE) printf("Not possible in this state.\n");
    else printf("Error: %s\n", esp_err_to_name(err));
    print_status();
    return 1;
}

static int cmd_store(int argc, char **argv)
{
    board_activity_notify();
    if (argc == 1) {
        print_status();
        return 0;
    }
    const char *sub = argv[1];
    if (!strcmp(sub, "create") && argc == 3) return report(accounts_create(argv[2]), "Store created, unlocked.");
    if (!strcmp(sub, "unlock") && argc == 3) return report(accounts_unlock(argv[2]), "Unlocked.");
    if (!strcmp(sub, "passwd") && argc == 3) return report(accounts_change_pin(argv[2]), "PIN changed.");
    if (!strcmp(sub, "lock") && argc == 2) {
        accounts_lock();
        return report(ESP_OK, "Locked.");
    }
    if (!strcmp(sub, "demo") && argc == 2) {
        int n = accounts_load_demo();
        if (n < 0) return report(ESP_ERR_INVALID_STATE, "");
        printf("%d sample account(s) added.\n", n);
        print_status();
        return 0;
    }
    if (!strcmp(sub, "erase") && argc == 3 && !strcmp(argv[2], "ERASE")) {
        accounts_lock();
        return report(msf_store_erase(), "Store erased: every account is gone.");
    }
    printf("Usage: store [create <pin> | unlock <pin> | lock | passwd <pin> | demo | erase ERASE]\n");
    return 1;
}

esp_err_t cmd_store_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "store",
        .help = "Encrypted account store: status | create <pin> | unlock <pin> | lock | passwd <pin> | demo | "
                "erase ERASE (wipes every account).",
        .func = &cmd_store,
    };
    return esp_console_cmd_register(&cmd);
}
