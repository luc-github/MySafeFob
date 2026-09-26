/*
 Project: MySafeFob  ui_screen_home.cpp
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
 * @file ui_screen_home.cpp
 * @brief MySafeFob App — Home screen (Settings icon + "Sleep now").
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "lv_port_indev.h"
#include "alerts.h"

#include "esp_log.h"
#include "app_log_workaround.h"

static const char *TAG = "home";

/* ADR-018 warning button: created once, shown only while an alert is
 * active (hidden objects are skipped by LVGL focus, so no empty stop). */
static lv_obj_t *s_alert_btn;

static void open_settings_deferred(void *user_data)
{
    (void)user_data;
    switch_screen(Screen::Settings);
}

static void open_settings_cb(lv_event_t *e)
{
    (void)e;
    ui_defer(open_settings_deferred, nullptr);
}

static void open_alerts_deferred(void *user_data)
{
    (void)user_data;
    switch_screen(Screen::Alerts);
}

static void open_alerts_cb(lv_event_t *e)
{
    (void)e;
    ui_defer(open_alerts_deferred, nullptr);
}

/* Alerts are re-checked every time HOME is shown (boot, wake, Back), never
 * from a timer (no refresh without an interaction). */
static void screen_loaded_cb(lv_event_t *e)
{
    (void)e;
    bool show = alerts_evaluate() > 0;
    if (show) {
        lv_obj_remove_flag(s_alert_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_alert_btn, LV_OBJ_FLAG_HIDDEN);
    }
    ESP_LOGI(TAG, "alert button %s", show ? "shown" : "hidden");
}

static void sleep_now_deferred(void *user_data)
{
    (void)user_data;
    enter_sleep_from_idle_or_menu("menu");
}

static void sleep_now_cb(lv_event_t *e)
{
    (void)e;
    ui_defer(sleep_now_deferred, nullptr);
}

lv_obj_t *build_home(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;

    lv_obj_t *screen = make_screen();

    /* Same header shape as every other screen (battery top-right, divider,
     * a button below it) but with no title/Back -- Home has neither a
     * parent screen nor a name of its own -- and "Settings" (icon,
     * 2026-09-21 user request) in Back's usual slot instead. */
    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), kHeaderInfoHeight);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(bar, 16, 0);
    lv_obj_set_style_pad_top(bar, kHeaderContentPadTop, 0);   /* see add_back_header()'s twin comment */
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    *battery_label_out = make_battery_label(bar);

    add_divider_below(screen, bar);

    /* Icon-only, not the 140px make_button() default meant for text labels
     * like "< Back" (2026-09-21 request: "peut etre plus petit pas besoin
     * qu'il soit si large"). */
    lv_obj_t *settings_btn = make_button(screen, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_min_width(settings_btn, kMinTouchTarget, 0);
    lv_obj_align(settings_btn, LV_ALIGN_TOP_LEFT, 16, kBackTopMargin);
    lv_obj_add_event_cb(settings_btn, open_settings_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(settings_btn, group);

    /* Same size/row as Settings, on the right (ADR-018). Added to the group
     * right after Settings so the focus order is Settings, alert, content. */
    s_alert_btn = make_button(screen, LV_SYMBOL_WARNING);
    lv_obj_set_style_min_width(s_alert_btn, kMinTouchTarget, 0);
    lv_obj_align(s_alert_btn, LV_ALIGN_TOP_RIGHT, -16, kBackTopMargin);
    lv_obj_add_event_cb(s_alert_btn, open_alerts_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(s_alert_btn, group);
    lv_obj_add_flag(s_alert_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(screen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);

    lv_obj_t *content = make_content(screen);

    lv_obj_t *title = lv_label_create(content);
    lv_label_set_text(title, "MySafeFob");

    lv_obj_t *sleep_btn = make_button(content, "Sleep now");
    lv_obj_set_size(sleep_btn, LV_PCT(70), 70);
    lv_obj_add_event_cb(sleep_btn, sleep_now_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(sleep_btn, group);

    lv_obj_t *hint = lv_label_create(content);
    lv_label_set_text(hint, "Left/Right = focus\nTouch/Power = confirm");
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

    return screen;
}
