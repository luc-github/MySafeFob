/*
 Project: MySafeFob  ui_screen_settings_about.cpp
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
 * @file ui_screen_settings_about.cpp
 * @brief MySafeFob App — Settings > About (version, build date, free RAM).
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "lv_port_indev.h"
#include "time_service.h"
#include "settings_store.h"

extern "C" {
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
}

#include <cstdio>

static lv_obj_t *s_label;

/* Rebuilt every time the screen is shown (not only at boot), so the RAM and
 * time figures are current; never refreshed while it stays on screen. */
static void update_about_text(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    /* MALLOC_CAP_DEFAULT covers every heap region (internal + the 8MB
     * PSRAM) LVGL/the app could allocate from -- the combined figure a
     * user asking "how much RAM" (2026-09-21 request) actually means,
     * not just one region. */
    size_t ram_total = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    size_t ram_free = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    char time_text[32] = "not set";
    if (time_service_is_valid()) {
        time_service_dt_t dt;
        time_service_epoch_to_dt(time_service_get_utc(), &dt);
        snprintf(time_text, sizeof(time_text), "%04d-%02d-%02d %02d:%02d:%02d", dt.year, dt.month, dt.day,
                 dt.hour, dt.minute, dt.second);
    }
    /* Last synchronisation (date, source, offset the clock had just before
     * it), then the drift per day measured at each recorded sync, newest
     * first (offset divided by the time since the previous sync): the older
     * records are only used to show whether that drift stays constant. */
    char sync_text[160] = "none";
    uint32_t epoch, src;
    int32_t delta;
    if (settings_store_get_time_sync(0, &epoch, &delta, &src)) {
        static const char *const kSources[] = {"manual", "Wi-Fi", "BLE"};
        time_service_dt_t sd;
        time_service_epoch_to_dt(static_cast<time_t>(epoch), &sd);
        int n = snprintf(sync_text, sizeof(sync_text), "%04d-%02d-%02d %s", sd.year, sd.month, sd.day,
                         src < 3 ? kSources[src] : "?");
        if (delta == TIME_SYNC_DELTA_UNKNOWN) {
            n += snprintf(sync_text + n, sizeof(sync_text) - n, " offset ?");
        } else {
            n += snprintf(sync_text + n, sizeof(sync_text) - n, " offset %+lds", static_cast<long>(delta));
        }
        bool first = true;
        for (int i = 0; i < SETTINGS_TIME_SYNC_HISTORY - 1 && n < static_cast<int>(sizeof(sync_text)); i++) {
            uint32_t e, pe, sr, psr;
            int32_t d, pd;
            if (!settings_store_get_time_sync(i, &e, &d, &sr) || !settings_store_get_time_sync(i + 1, &pe, &pd, &psr)) break;
            if (d == TIME_SYNC_DELTA_UNKNOWN || e <= pe + 3600) continue;
            long tenths = static_cast<long>(static_cast<int64_t>(d) * 864000 / (e - pe));
            long mag = tenths < 0 ? -tenths : tenths;
            n += snprintf(sync_text + n, sizeof(sync_text) - n, "%s%c%ld.%ld", first ? "\ndrift s/day: " : ", ",
                          tenths < 0 ? '-' : '+', mag / 10, mag % 10);
            first = false;
        }
    }
    /* Next sync due (last sync + TimeSyncMaxAgeS, ADR-018): same function
     * HOME's alert uses, so the date shown here is exactly when the alert
     * appears. Minutes are shown because tests use short thresholds. */
    char next_text[64];
    time_t due;
    switch (time_service_next_sync_due(&due)) {
    case TIME_SYNC_DUE_DISABLED:
        snprintf(next_text, sizeof(next_text), "alert off");
        break;
    case TIME_SYNC_DUE_NEVER_SYNCED:
        snprintf(next_text, sizeof(next_text), "now (never synced)");
        break;
    default: {
        time_service_dt_t dd;
        time_service_epoch_to_dt(due, &dd);
        int n = snprintf(next_text, sizeof(next_text), "%04d-%02d-%02d %02d:%02d", dd.year, dd.month, dd.day,
                         dd.hour, dd.minute);
        long left = static_cast<long>(due - time_service_get_utc());
        if (left <= 0) {
            snprintf(next_text + n, sizeof(next_text) - n, " overdue");
        } else if (left >= 86400) {
            snprintf(next_text + n, sizeof(next_text) - n, " (in %ld d)", left / 86400);
        } else if (left >= 3600) {
            snprintf(next_text + n, sizeof(next_text) - n, " (in %ld h)", left / 3600);
        } else {
            snprintf(next_text + n, sizeof(next_text) - n, " (in %ld min)", (left + 59) / 60);
        }
        break;
    }
    }
    char buf[500];
    snprintf(buf, sizeof(buf),
             "MySafeFob\nboard: x4pro\nversion: %s\nbuilt: %s %s\nRAM: %u/%u KB free\nUTC: %s\nlast sync: %s\n"
             "next sync: %s",
             app_desc->version, app_desc->date, app_desc->time, static_cast<unsigned>(ram_free / 1024),
             static_cast<unsigned>(ram_total / 1024), time_text, sync_text, next_text);
    lv_label_set_text(s_label, buf);
}

static void screen_loaded_cb(lv_event_t *)
{
    update_about_text();
}

lv_obj_t *build_settings_about(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, "About", Screen::Settings, group, battery_label_out);
    lv_obj_t *content = make_content(screen);

    s_label = lv_label_create(content);
    update_about_text();
    lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(screen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);

    return screen;
}
