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
 * @brief MySafeFob App — HOME (ADR-019, UI-SPECS.md §2.2 amendment): the
 *        alphabet. Header: Settings (top left), Power = sleep now (top
 *        right, mirror of Settings), alert button left of Power while an
 *        alert is active. Body: letter buttons A-Z and '#' (letters without
 *        any account disabled) and "+ Add". A letter opens NAMES.
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "lv_port_indev.h"
#include "alerts.h"

extern "C" {
#include "accounts.h"
}

#include "esp_log.h"
#include "app_log_workaround.h"

#include <cstdio>

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

/* Letter grid: 5 columns, rows at fixed y placed with touch_safe_y() (X4
 * Pro touch band, ui_widgets.h): 4 rows above it, 2 below. */
static constexpr int kCols = 5;
static constexpr int kLetterCount = 27;   /* A-Z, '#' */
static constexpr int32_t kCellW = 76;
static constexpr int32_t kCellGap = 16;
static constexpr int32_t kGridTop = 176;
static constexpr int32_t kRowPitch = kMinTouchTarget + 26;
static constexpr int32_t kGridLeft = (480 - (kCols * kCellW + (kCols - 1) * kCellGap)) / 2;

static lv_obj_t *s_letter_btns[kLetterCount];
/* Header line between Settings and the right-hand buttons: "N entries"
 * (accounts, information only). */
static lv_obj_t *s_info;

static char letter_at(int i)
{
    return i < 26 ? static_cast<char>('A' + i) : '#';
}

static void letter_deferred(void *user_data)
{
    char letter = static_cast<char>(reinterpret_cast<intptr_t>(user_data));
    if (accounts_count(letter) > 0) names_open(letter);
}

static void letter_cb(lv_event_t *e)
{
    ui_defer(letter_deferred, lv_event_get_user_data(e));
}

static void add_deferred(void *)
{
    account_edit_open_new(0);
}

static void add_cb(lv_event_t *)
{
    ui_defer(add_deferred, nullptr);
}

static void show_entry_count(void)
{
    int n = accounts_count(ACCOUNTS_ALL_LETTERS);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d %s", n, n == 1 ? "entry" : "entries");
    lv_label_set_text(s_info, buf);
}

/* Letters without any account: disabled (no frame, not focusable, taps
 * ignored). Re-evaluated on every visit, accounts may have been removed. */
static void update_letters(void)
{
    for (int i = 0; i < kLetterCount; i++) {
        if (accounts_count(letter_at(i)) > 0) lv_obj_remove_state(s_letter_btns[i], LV_STATE_DISABLED);
        else lv_obj_add_state(s_letter_btns[i], LV_STATE_DISABLED);
    }
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
    update_letters();
    show_entry_count();
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

    /* Power = sleep now (ADR-019, was the "Sleep now" button), mirror of
     * Settings on the right. */
    lv_obj_t *power_btn = make_button(screen, LV_SYMBOL_POWER);
    lv_obj_set_style_min_width(power_btn, kMinTouchTarget, 0);
    lv_obj_align(power_btn, LV_ALIGN_TOP_RIGHT, -16, kBackTopMargin);
    lv_obj_add_event_cb(power_btn, sleep_now_cb, LV_EVENT_CLICKED, nullptr);

    /* ADR-018 alert button, just left of Power, only while an alert is
     * active. Focus order: Settings, alert, Power, letters, + Add. */
    s_alert_btn = make_button(screen, LV_SYMBOL_WARNING);
    lv_obj_set_style_min_width(s_alert_btn, kMinTouchTarget, 0);
    lv_obj_align(s_alert_btn, LV_ALIGN_TOP_RIGHT, -16 - kMinTouchTarget - 16, kBackTopMargin);
    lv_obj_add_event_cb(s_alert_btn, open_alerts_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(s_alert_btn, group);
    lv_obj_add_flag(s_alert_btn, LV_OBJ_FLAG_HIDDEN);
    add_to_group(power_btn, group);
    lv_obj_add_event_cb(screen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);

    /* "N entries" between Settings and the right-hand buttons, on their row. */
    s_info = lv_label_create(screen);
    lv_obj_align(s_info, LV_ALIGN_TOP_MID, 0, kBackTopMargin + 14);

    int32_t y = kGridTop;
    for (int i = 0; i < kLetterCount; i++) {
        int col = i % kCols;
        if (i > 0 && col == 0) y += kRowPitch;
        y = touch_safe_y(y, kMinTouchTarget);
        char text[2] = {letter_at(i), '\0'};
        lv_obj_t *btn = make_button(screen, text);
        lv_obj_set_style_min_width(btn, 0, 0);
        lv_obj_set_size(btn, kCellW, kMinTouchTarget);
        lv_obj_set_ext_click_area(btn, 0);   /* gaps < 2 x kExtClickMargin */
        lv_obj_set_style_text_font(lv_obj_get_child(btn, 0), &lv_font_montserrat_32, 0);
        lv_obj_set_style_border_width(btn, 0, LV_STATE_DISABLED);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, kGridLeft + col * (kCellW + kCellGap), y);
        lv_obj_add_event_cb(btn, letter_cb, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(letter_at(i))));
        add_to_group(btn, group);
        s_letter_btns[i] = btn;
    }

    /* "+ Add" fills the rest of the last row (after Z and #). */
    int col = kLetterCount % kCols;
    lv_obj_t *add = make_button(screen, "+ Add");
    lv_obj_set_style_min_width(add, 0, 0);
    lv_obj_set_size(add, (kCols - col) * kCellW + (kCols - col - 1) * kCellGap, kMinTouchTarget);
    lv_obj_set_ext_click_area(add, 0);
    lv_obj_align(add, LV_ALIGN_TOP_LEFT, kGridLeft + col * (kCellW + kCellGap), y);
    lv_obj_add_event_cb(add, add_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(add, group);

    /* The name, large, in the gap the touch band leaves between rows 4 and
     * 5 (2026-10-09 request): not a touch target, so the gap looks
     * deliberate. Centered on the gap between the two rows around it. */
    lv_obj_t *brand = lv_label_create(screen);
    lv_label_set_text(brand, "MySafeFob");
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_32, 0);
    int32_t gap_top = kGridTop + 3 * kRowPitch + kMinTouchTarget;
    int32_t gap_bottom = touch_safe_y(kGridTop + 4 * kRowPitch, kMinTouchTarget);
    lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, (gap_top + gap_bottom - lv_font_montserrat_32.line_height) / 2);

    update_letters();
    show_entry_count();
    return screen;
}
