/*
 Project: MySafeFob  ui_screen_settings.cpp
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
 * @file ui_screen_settings.cpp
 * @brief MySafeFob App — Settings menus: top level (Device / Time /
 *        Security / About) and its two sub-menus, Device (Controls /
 *        Display / Touch Calibration) and Security (Change PIN,
 *        Lock now). Every entry carries an icon (msf_symbols.h).
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "msf_symbols.h"
#include "lv_port_indev.h"

extern "C" {
#include "accounts.h"
#include "settings_store.h"
}

static void open_screen_deferred(void *user_data)
{
    switch_screen(static_cast<Screen>(reinterpret_cast<intptr_t>(user_data)));
}

static void open_screen_cb(lv_event_t *e)
{
    ui_defer(open_screen_deferred, lv_event_get_user_data(e));
}

/* 2x kExtClickMargin (2026-09-22 bug report: "j'appuis sur Display, une
 * fois sur deux c'est About qui s'ouvre"). Root cause: each menu button's
 * touch hit area extends kExtClickMargin=16px past its own visual box on
 * every side (lv_obj_set_ext_click_area(), make_button()) -- with only
 * make_content()'s default 16px row gap between two adjacent, full-width
 * buttons, their extended hit areas overlapped by 16px right in the
 * middle of that gap, so a tap landing there could resolve to EITHER
 * button. Doubling the gap to 32px (2x the 16px each side contributes) is
 * exactly the point past which they can no longer overlap at all. Menu
 * buttons only -- other screens' rows have their interactive control on
 * one side, not a full-width button repeated top-to-bottom, so this
 * specific overlap doesn't arise there. */
static constexpr int32_t kMenuButtonRowGap = 2 * kExtClickMargin;

static lv_obj_t *add_menu_button(lv_obj_t *parent, const char *label_text, Screen target, lv_group_t *group)
{
    lv_obj_t *btn = make_button(parent, label_text);
    lv_obj_set_width(btn, LV_PCT(90));
    lv_obj_add_event_cb(btn, open_screen_cb, LV_EVENT_CLICKED,
                         reinterpret_cast<void *>(static_cast<intptr_t>(target)));
    add_to_group(btn, group);
    return btn;
}

/* Builds an empty menu screen titled `title` whose Back returns to
 * `back_target`; the caller adds its rows to the returned *content_out. */
static lv_obj_t *make_menu_screen(const char *title, Screen back_target, lv_group_t **group_out,
                                  lv_obj_t **battery_label_out, lv_obj_t **content_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, title, back_target, group, battery_label_out);
    lv_obj_t *content = make_content(screen);
    lv_obj_set_style_pad_row(content, kMenuButtonRowGap, 0);
    *content_out = content;
    return screen;
}

/* Settings is a two-level menu (2026-10-01): every level must fit on one
 * screen, since scrolling on e-paper is slow and error-prone. Keep each
 * menu at 5 rows or fewer; add a group rather than a sixth row. */
lv_obj_t *build_settings(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_obj_t *content;
    lv_obj_t *screen = make_menu_screen(LV_SYMBOL_SETTINGS " Settings", Screen::Home, group_out, battery_label_out, &content);

    add_menu_button(content, MSF_SYMBOL_MOBILE "  Device", Screen::SettingsDevice, *group_out);
    add_menu_button(content, MSF_SYMBOL_CLOCK "  Time", Screen::Time, *group_out);
    add_menu_button(content, MSF_SYMBOL_KEY "  Security", Screen::SettingsSecurity, *group_out);
    add_menu_button(content, MSF_SYMBOL_INFO "  About", Screen::SettingsAbout, *group_out);

    return screen;
}

lv_obj_t *build_settings_device(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_obj_t *content;
    lv_obj_t *screen = make_menu_screen(MSF_SYMBOL_MOBILE " Device", Screen::Settings, group_out, battery_label_out, &content);

    add_menu_button(content, MSF_SYMBOL_SLIDERS "  Controls", Screen::SettingsControls, *group_out);
    add_menu_button(content, MSF_SYMBOL_LIGHTBULB "  Display", Screen::SettingsDisplay, *group_out);
    add_menu_button(content, MSF_SYMBOL_CROSSHAIRS "  Touch Calibration", Screen::TouchDiag, *group_out);

    return screen;
}

/* Security actions (2026-10-11: the former Security sub-screen's
 * two buttons, directly here, one level less). */
static void change_pin_deferred(void *)
{
    pin_open_change();
}

static void change_pin_cb(lv_event_t *)
{
    ui_defer(change_pin_deferred, nullptr);
}

static void lock_now_deferred(void *)
{
    accounts_lock();
    pin_open_unlock();
}

static void lock_now_cb(lv_event_t *)
{
    ui_defer(lock_now_deferred, nullptr);
}

static void add_action_button(lv_obj_t *parent, const char *label_text, lv_event_cb_t cb, lv_group_t *group)
{
    lv_obj_t *btn = make_button(parent, label_text);
    lv_obj_set_width(btn, LV_PCT(90));
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(btn, group);
}

static void pin_symbols_deferred(void *user_data)
{
    settings_store_set_pin_symbols(static_cast<bool>(reinterpret_cast<intptr_t>(user_data)));
}

static void pin_symbols_cb(lv_event_t *e)
{
    bool checked = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    ui_defer(pin_symbols_deferred, reinterpret_cast<void *>(static_cast<intptr_t>(checked)));
}

/* Change PIN, Lock now (Owner info moved to About, 2026-10-11), Symbol
 * keypad. Planned additions, within the 5-row limit: Auto-lock (PIN) delay
 * (UI-SPECS §2.13) and one Backup row (SD export and import, ROADMAP 8.0
 * P11). */
lv_obj_t *build_settings_security(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_obj_t *content;
    lv_obj_t *screen = make_menu_screen(MSF_SYMBOL_KEY " Security", Screen::Settings, group_out, battery_label_out, &content);

    add_action_button(content, MSF_SYMBOL_KEY "  Change PIN", change_pin_cb, *group_out);
    add_action_button(content, MSF_SYMBOL_LOCK "  Lock now", lock_now_cb, *group_out);
    make_round_toggle_row(content, *group_out, MSF_SYMBOL_SMILE " Symbol keypad", settings_store_get_pin_symbols(),
                          pin_symbols_cb);

    return screen;
}
