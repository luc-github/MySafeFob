/*
 Project: MySafeFob  ui_screen_owner.cpp
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
 * @file ui_screen_owner.cpp
 * @brief MySafeFob App — Settings > Owner info (F-19, UI-SPECS.md §2.17):
 *        opt-in toggle + the contact text drawn on the sleep screen
 *        (splash.cpp). The text field and its keyboard only show while the
 *        toggle is On. The text is saved on Enter and when leaving the
 *        screen; both settings are re-read each time the screen is shown,
 *        so a change made from the console (`setting`) shows up here too.
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "ui_keyboard.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "settings_store.h"

#include <cstdio>
#include <cstring>

static char s_text[SETTINGS_OWNER_INFO_MAX + 1];
static int s_text_len;
static bool s_dirty;
static lv_obj_t *s_toggle;
static lv_obj_t *s_edit_box;
static lv_obj_t *s_keyboard;
static lv_obj_t *s_field_label;
static lv_obj_t *s_status_label;

static void show_text(void)
{
    char buf[SETTINGS_OWNER_INFO_MAX + 2];
    snprintf(buf, sizeof(buf), "%s_", s_text);
    lv_label_set_text(s_field_label, buf);
}

static void show_status(const char *text)
{
    if (strcmp(lv_label_get_text(s_status_label), text) != 0) lv_label_set_text(s_status_label, text);
}

static void show_count(void)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%d/%d", s_text_len, SETTINGS_OWNER_INFO_MAX);
    show_status(buf);
}

static void load_text(void)
{
    settings_store_get_owner_info(s_text, sizeof(s_text));
    s_text_len = static_cast<int>(strlen(s_text));
    s_dirty = false;
    show_text();
    show_count();
}

static void save_text(void)
{
    if (!s_dirty) return;
    if (settings_store_set_owner_info(s_text) == ESP_OK) {
        s_dirty = false;
        show_status("Saved");
    } else {
        show_status("Save failed");
    }
}

static void set_edit_visible(bool visible)
{
    lv_obj_t *objs[] = {s_edit_box, s_keyboard};
    for (lv_obj_t *obj : objs) {
        if (visible) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void kb_char(char c, void *)
{
    if (s_text_len < SETTINGS_OWNER_INFO_MAX) {
        s_text[s_text_len++] = c;
        s_text[s_text_len] = '\0';
        s_dirty = true;
        show_text();
        show_count();
    }
}

static void kb_backspace(void *)
{
    if (s_text_len > 0) {
        s_text[--s_text_len] = '\0';
        s_dirty = true;
        show_text();
        show_count();
    }
}

static void kb_enter(void *)
{
    s_dirty = true;   /* explicit save, even if nothing changed */
    save_text();
}

/* Same pattern as Display's Frontlight toggle: showing/hiding a large
 * block reflows the screen, so this one asks for a full refresh. Hidden
 * containers drop out of focus navigation by themselves (lv_group skips
 * children of hidden parents). */
static void show_toggle_deferred(void *user_data)
{
    bool on = static_cast<bool>(reinterpret_cast<intptr_t>(user_data));
    settings_store_set_owner_info_show(on);
    set_edit_visible(on);
    lv_port_disp_request_full_refresh();
}

static void show_toggle_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    ui_defer(show_toggle_deferred, reinterpret_cast<void *>(static_cast<intptr_t>(on)));
}

static void screen_loaded_cb(lv_event_t *)
{
    bool on = settings_store_get_owner_info_show();
    if (on) lv_obj_add_state(s_toggle, LV_STATE_CHECKED);
    else lv_obj_clear_state(s_toggle, LV_STATE_CHECKED);
    set_edit_visible(on);
    load_text();
}

static void screen_unloaded_deferred(void *)
{
    save_text();
}

static void screen_unloaded_cb(lv_event_t *)
{
    ui_defer(screen_unloaded_deferred, nullptr);
}

lv_obj_t *build_owner_info(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, "Owner info", Screen::SettingsSecurityData, group, battery_label_out);
    lv_obj_t *content = make_content(screen);

    bool on = settings_store_get_owner_info_show();
    lv_obj_t *row = make_round_toggle_row(content, group, "Show on sleep screen", on, show_toggle_cb);
    s_toggle = lv_obj_get_child(row, -1);

    s_edit_box = lv_obj_create(content);
    lv_obj_remove_style_all(s_edit_box);
    lv_obj_set_size(s_edit_box, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_edit_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_edit_box, 8, 0);

    lv_obj_t *caption_row = lv_obj_create(s_edit_box);
    lv_obj_remove_style_all(caption_row);
    lv_obj_set_size(caption_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_t *caption = lv_label_create(caption_row);
    lv_label_set_text(caption, "Phone / email");
    lv_obj_align(caption, LV_ALIGN_LEFT_MID, 0, 0);
    s_status_label = lv_label_create(caption_row);
    lv_label_set_text(s_status_label, "");
    lv_obj_align(s_status_label, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *field = lv_obj_create(s_edit_box);
    lv_obj_remove_style_all(field);
    lv_obj_set_size(field, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(field, kMinTouchTarget, 0);
    lv_obj_set_style_border_width(field, kButtonBorderWidth, 0);
    lv_obj_set_style_border_color(field, lv_color_black(), 0);
    lv_obj_set_style_radius(field, kButtonRadius, 0);
    lv_obj_set_style_pad_all(field, 10, 0);
    s_field_label = lv_label_create(field);
    lv_obj_set_width(s_field_label, LV_PCT(100));
    lv_label_set_long_mode(s_field_label, LV_LABEL_LONG_WRAP);

    static const ui_keyboard_cb_t kCb = {kb_char, kb_backspace, kb_enter, nullptr};
    s_keyboard = ui_keyboard_create(screen, group, &kCb);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);

    set_edit_visible(on);
    load_text();

    lv_obj_add_event_cb(screen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    lv_obj_add_event_cb(screen, screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, nullptr);
    return screen;
}
