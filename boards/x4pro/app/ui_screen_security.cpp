/*
 Project: MySafeFob  ui_screen_security.cpp
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
 * @file ui_screen_security.cpp
 * @brief MySafeFob App — Settings > Security: numeric PIN keypad (UI test
 *        only, 2026-09-24). Exactly 6 digits, a correction key and a
 *        validation key. Nothing is stored or checked against anything --
 *        the entered digits live in RAM only and are wiped on leaving the
 *        screen. The real PIN flow (secret_store, ADR-012) is a later task.
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "lv_port_indev.h"

#include <cstdio>
#include <cstring>

static constexpr int kPinLength = 6;

/* Keys are big and spaced so touch hit areas can't overlap (2026-09-22
 * lesson, ui_screen_settings.cpp's kMenuButtonRowGap): no extended click
 * area on these -- at 120x80 they don't need one -- and a 16px gap. The
 * container reserves kFocusOutlineSlack on every side so a focused key's
 * outline ring isn't clipped by its parent. */
static constexpr int32_t kKeyWidth = 120;
static constexpr int32_t kKeyHeight = 80;
static constexpr int32_t kKeyGap = 16;
static constexpr int32_t kEyeSize = 64;

/* Special key codes passed through ui_defer()'s user_data, outside 0-9. */
static constexpr int kKeyBackspace = -1;
static constexpr int kKeyOk = -2;
static constexpr int kKeyToggleShow = -3;

static char s_pin[kPinLength + 1];
static int s_pin_len;
static lv_obj_t *s_pin_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_eye_label;
static bool s_show_pin;

static void show_pin(void)
{
    char buf[kPinLength * 2 + 1];
    int pos = 0;
    for (int i = 0; i < kPinLength; i++) {
        buf[pos++] = i < s_pin_len ? (s_show_pin ? s_pin[i] : '*') : '_';
        if (i + 1 < kPinLength) {
            buf[pos++] = ' ';
        }
    }
    buf[pos] = '\0';
    lv_label_set_text(s_pin_label, buf);
}

static void update_eye(void)
{
    /* The icon shows the action the button will perform. */
    lv_label_set_text(s_eye_label, s_show_pin ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

static void set_status(const char *text)
{
    if (strcmp(lv_label_get_text(s_status_label), text) != 0) {
        lv_label_set_text(s_status_label, text);
    }
}

static void clear_pin(void)
{
    memset(s_pin, 0, sizeof(s_pin));
    s_pin_len = 0;
    show_pin();
}

static void key_deferred(void *user_data)
{
    int key = static_cast<int>(reinterpret_cast<intptr_t>(user_data));
    if (key >= 0 && key <= 9) {
        if (s_pin_len < kPinLength) {
            s_pin[s_pin_len++] = static_cast<char>('0' + key);
            show_pin();
            set_status(s_pin_len == kPinLength ? "Press OK" : "Enter 6 digits");
        } else {
            set_status("6 digits max -- OK or correct");
        }
    } else if (key == kKeyBackspace) {
        if (s_pin_len > 0) {
            s_pin[--s_pin_len] = '\0';
            show_pin();
        }
        set_status("Enter 6 digits");
    } else if (key == kKeyToggleShow) {
        s_show_pin = !s_show_pin;
        update_eye();
        show_pin();
    } else if (key == kKeyOk) {
        if (s_pin_len == kPinLength) {
            set_status("PIN accepted (test only, not saved)");
            clear_pin();
        } else {
            set_status("Need exactly 6 digits");
        }
    }
}

/* Handled synchronously: lv_async_call() does not preserve call order, so
 * deferring fast consecutive taps would reorder the digits. */
static void key_cb(lv_event_t *e)
{
    key_deferred(lv_event_get_user_data(e));
}

static void screen_unloaded_deferred(void *user_data)
{
    (void)user_data;
    s_show_pin = false;
    update_eye();
    clear_pin();
    set_status("Enter 6 digits");
}

static void screen_unloaded_cb(lv_event_t *e)
{
    (void)e;
    ui_defer(screen_unloaded_deferred, nullptr);
}

static lv_obj_t *add_key(lv_obj_t *parent, lv_group_t *group, const char *text, int key)
{
    lv_obj_t *btn = make_button(parent, text);
    lv_obj_set_style_min_width(btn, 0, 0);
    lv_obj_set_size(btn, kKeyWidth, kKeyHeight);
    lv_obj_set_ext_click_area(btn, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(btn, 0), &lv_font_montserrat_32, 0);
    lv_obj_add_event_cb(btn, key_cb, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(key)));
    add_to_group(btn, group);
    return btn;
}

lv_obj_t *build_security(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, "Security", Screen::Settings, group, battery_label_out);
    lv_obj_t *content = make_content(screen);

    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *pin_row = lv_obj_create(content);
    lv_obj_remove_style_all(pin_row);
    lv_obj_set_size(pin_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pin_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pin_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(pin_row, kFocusOutlineSlack, 0);
    lv_obj_set_style_pad_column(pin_row, kKeyGap, 0);
    s_pin_label = lv_label_create(pin_row);
    lv_obj_set_style_text_font(s_pin_label, &lv_font_montserrat_32, 0);
    lv_obj_t *eye = add_key(pin_row, group, LV_SYMBOL_EYE_OPEN, kKeyToggleShow);
    lv_obj_set_size(eye, kEyeSize, kEyeSize);
    s_eye_label = lv_obj_get_child(eye, 0);
    s_show_pin = false;
    s_status_label = lv_label_create(content);
    lv_label_set_text(s_status_label, "Enter 6 digits");

    lv_obj_t *pad = lv_obj_create(content);
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pad, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(pad, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(pad, kFocusOutlineSlack, 0);
    lv_obj_set_style_pad_gap(pad, kKeyGap, 0);
    /* Exactly 3 keys per row: content width minus the container's padding
     * has to fit 3 keys + 2 gaps and not a 4th. */
    lv_obj_set_width(pad, 3 * kKeyWidth + 2 * kKeyGap + 2 * kFocusOutlineSlack);

    static const char *const kDigits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
    for (int i = 0; i < 9; i++) {
        add_key(pad, group, kDigits[i], i + 1);
    }
    add_key(pad, group, LV_SYMBOL_BACKSPACE, kKeyBackspace);
    add_key(pad, group, "0", 0);
    add_key(pad, group, LV_SYMBOL_NEW_LINE, kKeyOk);

    clear_pin();
    lv_obj_add_event_cb(screen, screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, nullptr);

    return screen;
}
