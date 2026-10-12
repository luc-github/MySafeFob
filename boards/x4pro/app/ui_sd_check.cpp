/*
 Project: MySafeFob  ui_sd_check.cpp
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
 * @file ui_sd_check.cpp
 * @brief MySafeFob App — SD sanity check (2026-10-11 request): after an
 *        unlock, at most once an hour (several boots in a row do not repeat
 *        it), the card is scanned in a worker task for plaintext leftovers
 *        (backup_find_plaintext()); if any, the shared confirmation offers
 *        to wipe and delete them. Nothing shown without a card or files.
 *        Setting SdCheck (Backup menu), last run in SdCheckLastS.
 */
#include "ui_screens.h"
#include "ui_widgets.h"

extern "C" {
#include "backup.h"
#include "sdcard.h"
#include "settings_store.h"
#include "time_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

#include "app_log_workaround.h"

#include <atomic>
#include <cstdio>
#include <cstring>

static const char *TAG = "sd_check";

static constexpr int kMaxFiles = 8;
static constexpr uint32_t kMinIntervalS = 3600;

static char s_paths[kMaxFiles][BACKUP_PATH_MAX];
static int s_found;
static std::atomic<bool> s_busy{false};
static std::atomic<bool> s_done{false};
static bool s_wipe;   /* the worker's job: false = scan, true = wipe */
static char s_message[160];

static void worker_task(void *)
{
    if (s_wipe) s_found = backup_wipe_files(s_paths, s_found);
    else s_found = backup_find_plaintext(s_paths, kMaxFiles);
    s_done = true;
    vTaskDelete(nullptr);
}

static bool start(bool wipe)
{
    if (s_busy) return false;
    s_wipe = wipe;
    s_done = false;
    s_busy = true;
    if (xTaskCreate(worker_task, "sd_check", 8192, nullptr, 2, nullptr) != pdPASS) {
        s_busy = false;
        return false;
    }
    return true;
}

void sd_check_on_unlock(void)
{
    if (!settings_store_get_sd_check()) return;
    if (time_service_is_valid()) {
        uint32_t now = static_cast<uint32_t>(time_service_get_utc());
        uint32_t last = settings_store_get_sd_check_last();
        if (last && now >= last && now - last < kMinIntervalS) return;
        settings_store_set_sd_check_last(now);
    }
    if (start(false)) ESP_LOGI(TAG, "scanning the SD card");
}

static void wipe_confirmed(void *)
{
    start(true);
}

/* Short name for the message: what follows "/sdcard/". */
static const char *short_name(const char *path)
{
    static const char kPrefix[] = SD_MOUNT_POINT "/";
    return strncmp(path, kPrefix, sizeof(kPrefix) - 1) == 0 ? path + sizeof(kPrefix) - 1 : path;
}

void sd_check_poll(void)
{
    if (!s_busy || !s_done) return;
    s_busy = false;
    if (s_wipe) {
        ESP_LOGI(TAG, "%d file(s) wiped", s_found);
        return;
    }
    if (s_found <= 0) return;   /* no card, or nothing to clean */
    if (!accounts_is_unlocked() || ui_confirm_is_open()) return;   /* next unlock will ask again */
    /* ui_confirm_show() keeps 160 characters: two names at most. */
    int pos = snprintf(s_message, sizeof(s_message), "Secrets in clear or PIN-only:\n");
    for (int i = 0; i < s_found && i < 2; i++) {
        pos += snprintf(s_message + pos, sizeof(s_message) - pos, "%.28s\n", short_name(s_paths[i]));
    }
    if (s_found > 2) snprintf(s_message + pos, sizeof(s_message) - pos, "and %d more", s_found - 2);
    const ui_confirm_t cfg = {"Delete from the SD card?", s_message, "Delete", wipe_confirmed, nullptr, nullptr, true};
    ui_confirm_show(&cfg);
}
