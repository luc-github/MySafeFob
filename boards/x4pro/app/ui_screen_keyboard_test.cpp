/*
 Project: MySafeFob  ui_screen_keyboard_test.cpp
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
 * @file ui_screen_keyboard_test.cpp
 * @brief MySafeFob App — Settings > Device > Keyboard test (ROADMAP 8.0 P7):
 *        tries the three ui_keyboard modes (full, Base32, numeric) before a
 *        real screen uses them (TOTP entry). Typed text is shown, never
 *        stored. Same "testable on its own" approach as the Security PIN
 *        keypad; to be removed once the TOTP entry screen exists.
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "ui_keyboard.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

#include <cstdio>

static constexpr int kModeCount = 3;
static constexpr int kMaxText = 40;
static const char *const kModeText[kModeCount] = {"abc", "B32", "123"};
static const ui_keyboard_mode_t kModes[kModeCount] = {UI_KEYBOARD_FULL, UI_KEYBOARD_BASE32, UI_KEYBOARD_NUMERIC};

static lv_obj_t *s_mode_btn[kModeCount];
static lv_obj_t *s_keyboards[kModeCount];
static lv_obj_t *s_field_label;
static char s_text[kMaxText + 1];
static int s_len;

static void show_text(void)
{
    char buf[kMaxText + 8];
    snprintf(buf, sizeof(buf), "%s_  (%d)", s_text, s_len);
    lv_label_set_text(s_field_label, buf);
}

static void kb_char(char c, void *)
{
    if (s_len < kMaxText) {
        s_text[s_len++] = c;
        s_text[s_len] = '\0';
        show_text();
    }
}

static void kb_backspace(void *)
{
    if (s_len > 0) {
        s_text[--s_len] = '\0';
        show_text();
    }
}

static void kb_enter(void *)
{
    s_len = 0;
    s_text[0] = '\0';
    show_text();
}

static void select_mode(int mode)
{
    for (int i = 0; i < kModeCount; i++) {
        bool active = (i == mode);
        if (active) lv_obj_remove_flag(s_keyboards[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_keyboards[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_mode_btn[i], active ? lv_color_black() : lv_color_white(), 0);
        lv_obj_set_style_bg_opa(s_mode_btn[i], LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(s_mode_btn[i], active ? lv_color_white() : lv_color_black(), 0);
    }
}

static void mode_deferred(void *user_data)
{
    select_mode(static_cast<int>(reinterpret_cast<intptr_t>(user_data)));
    lv_port_disp_request_full_refresh();   /* the whole keyboard changes */
}

static void mode_cb(lv_event_t *e)
{
    ui_defer(mode_deferred, lv_event_get_user_data(e));
}

lv_obj_t *build_keyboard_test(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, "Keyboard test", Screen::SettingsDevice, group, battery_label_out);
    lv_obj_t *content = make_content(screen);
    lv_obj_set_style_pad_row(content, 4, 0);

    lv_obj_t *modes = lv_obj_create(content);
    lv_obj_remove_style_all(modes);
    lv_obj_set_size(modes, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(modes, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(modes, kFocusOutlineSlack, 0);
    lv_obj_set_style_pad_column(modes, 12, 0);
    for (int i = 0; i < kModeCount; i++) {
        s_mode_btn[i] = make_button(modes, kModeText[i]);
        lv_obj_set_style_min_width(s_mode_btn[i], 120, 0);
        lv_obj_set_ext_click_area(s_mode_btn[i], 0);
        lv_obj_add_event_cb(s_mode_btn[i], mode_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        add_to_group(s_mode_btn[i], group);
    }

    lv_obj_t *field = lv_obj_create(content);
    lv_obj_remove_style_all(field);
    lv_obj_set_size(field, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(field, kMinTouchTarget, 0);
    lv_obj_set_style_border_width(field, kButtonBorderWidth, 0);
    lv_obj_set_style_border_color(field, lv_color_black(), 0);
    lv_obj_set_style_radius(field, kButtonRadius, 0);
    lv_obj_set_style_pad_all(field, 10, 0);
    s_field_label = lv_label_create(field);
    lv_obj_set_width(s_field_label, LV_PCT(100));
    lv_label_set_long_mode(s_field_label, LV_LABEL_LONG_WRAP);

    /* All three built once, bottom-anchored on the validated grid; only the
     * selected one is visible (hidden ones drop out of focus navigation). */
    static const ui_keyboard_cb_t kCb = {kb_char, kb_backspace, kb_enter, nullptr};
    for (int i = 0; i < kModeCount; i++) {
        s_keyboards[i] = ui_keyboard_create(screen, group, &kCb, kModes[i]);
        lv_obj_align(s_keyboards[i], LV_ALIGN_BOTTOM_MID, 0, -10);
    }

    s_len = 0;
    s_text[0] = '\0';
    show_text();
    select_mode(1);   /* Base32 first: the mode P7 is mainly for */
    return screen;
}
