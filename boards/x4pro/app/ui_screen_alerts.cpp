/*
 Project: MySafeFob  ui_screen_alerts.cpp
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
 * @file ui_screen_alerts.cpp
 * @brief MySafeFob App — Alerts screen (ADR-018, UI-SPECS.md §2.21): one
 *        block per active alert (title, explanation, optional action
 *        button), opened from HOME's warning button. Content is rebuilt
 *        every time the screen is shown.
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "lv_port_indev.h"
#include "alerts.h"

#include <cstdio>

static lv_obj_t *s_content;
static lv_group_t *s_group;

static void open_time_deferred(void *)
{
    switch_screen(Screen::Time);
}

static void open_time_cb(lv_event_t *)
{
    ui_defer(open_time_deferred, nullptr);
}

static void add_alert_block(alert_id_t id)
{
    lv_obj_t *title = lv_label_create(s_content);
    char title_text[64];
    snprintf(title_text, sizeof(title_text), LV_SYMBOL_WARNING " %s", alerts_title(id));
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);

    lv_obj_t *body = lv_label_create(s_content);
    char body_text[320];
    alerts_describe(id, body_text, sizeof(body_text));
    lv_label_set_text(body, body_text);
    lv_obj_set_width(body, LV_PCT(100));
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);

    /* Both current alerts are fixed from Settings > Time. */
    lv_obj_t *btn = make_button(s_content, "Set the time");
    lv_obj_add_event_cb(btn, open_time_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(btn, s_group);
}

static void rebuild_content(void)
{
    lv_obj_clean(s_content);   /* deleted buttons leave the group on their own */
    int shown = 0;
    alerts_evaluate();
    for (int i = 0; i < ALERT_COUNT; i++) {
        if (alerts_is_active(static_cast<alert_id_t>(i))) {
            add_alert_block(static_cast<alert_id_t>(i));
            shown++;
        }
    }
    if (shown == 0) {
        lv_obj_t *none = lv_label_create(s_content);
        lv_label_set_text(none, "No active alerts");
    }
}

static void screen_loaded_cb(lv_event_t *)
{
    rebuild_content();
}

lv_obj_t *build_alerts(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    s_group = lv_port_indev_new_group();
    *group_out = s_group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, "Alerts", Screen::Home, s_group, battery_label_out);
    s_content = make_content(screen);
    lv_obj_set_flex_align(s_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_event_cb(screen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);

    return screen;
}
