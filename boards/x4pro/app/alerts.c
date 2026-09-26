/*
 Project: MySafeFob  alerts.c
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
 * @file alerts.c
 * @brief MySafeFob App — alert registry (ADR-018), see alerts.h.
 */
#include "alerts.h"
#include "time_service.h"
#include "settings_store.h"

#include "esp_log.h"
#include "app_log_workaround.h"

#include <stdio.h>

#define TAG "alerts"

static bool s_active[ALERT_COUNT];

static const char *const kTitles[ALERT_COUNT] = {
    [ALERT_TIME_NEVER_SYNCED] = "Time never synced",
    [ALERT_TIME_SYNC_STALE] = "Time sync is old",
};

static const char *source_name(uint32_t source)
{
    static const char *const kSources[] = {"manual", "Wi-Fi", "BLE"};
    return source < 3 ? kSources[source] : "?";
}

/* "N d" / "N h" / "N min": tests use thresholds of a few minutes. */
static void format_duration(long seconds, char *buf, size_t len)
{
    if (seconds < 0) seconds = 0;
    if (seconds >= 86400) {
        snprintf(buf, len, "%ld d", seconds / 86400);
    } else if (seconds >= 3600) {
        snprintf(buf, len, "%ld h", seconds / 3600);
    } else {
        snprintf(buf, len, "%ld min", seconds / 60);
    }
}

int alerts_evaluate(void)
{
    time_t due;
    time_sync_due_t state = time_service_next_sync_due(&due);
    s_active[ALERT_TIME_NEVER_SYNCED] = state == TIME_SYNC_DUE_NEVER_SYNCED;
    s_active[ALERT_TIME_SYNC_STALE] = state == TIME_SYNC_DUE_OVERDUE;

    static const char *const kStates[] = {"disabled", "never synced", "ok", "overdue"};
    long left = (state == TIME_SYNC_DUE_OK || state == TIME_SYNC_DUE_OVERDUE)
                    ? (long)(due - time_service_get_utc())
                    : 0;
    ESP_LOGI(TAG, "time sync: %s (max age %lu s, due in %ld s, clock %s)", kStates[state],
             (unsigned long)settings_store_get_time_sync_max_age_s(), left,
             time_service_is_valid() ? "valid" : "NOT set");

    int count = 0;
    for (int i = 0; i < ALERT_COUNT; i++) {
        if (s_active[i]) {
            ESP_LOGI(TAG, "active: %s", kTitles[i]);
            count++;
        }
    }
    ESP_LOGI(TAG, "%d active alert(s)", count);
    return count;
}

bool alerts_is_active(alert_id_t id)
{
    return id < ALERT_COUNT && s_active[id];
}

const char *alerts_title(alert_id_t id)
{
    return id < ALERT_COUNT ? kTitles[id] : "?";
}

void alerts_describe(alert_id_t id, char *buf, size_t len)
{
    switch (id) {
    case ALERT_TIME_NEVER_SYNCED:
        snprintf(buf, len,
                 "No time sync has been recorded. TOTP codes need a clock accurate to about 15 s. "
                 "Set the time over Wi-Fi, BLE or manually.");
        break;
    case ALERT_TIME_SYNC_STALE: {
        uint32_t epoch, source;
        int32_t delta;
        if (!time_service_is_valid()) {
            snprintf(buf, len, "The clock is not set (the RTC may have lost power). Set the time.");
            break;
        }
        if (!settings_store_get_time_sync(0, &epoch, &delta, &source)) {
            buf[0] = '\0';
            break;
        }
        time_service_dt_t dt;
        time_service_epoch_to_dt((time_t)epoch, &dt);
        char age[16];
        format_duration((long)(time_service_get_utc() - (time_t)epoch), age, sizeof(age));
        snprintf(buf, len,
                 "Last sync: %04d-%02d-%02d (%s), %s ago. The clock may have drifted; TOTP codes are "
                 "refused if it is off by more than about 15 s. Sync the time over Wi-Fi or BLE.",
                 dt.year, dt.month, dt.day, source_name(source), age);
        break;
    }
    default:
        buf[0] = '\0';
        break;
    }
}
