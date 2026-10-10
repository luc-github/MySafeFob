/*
 Project: MySafeFob  ui_screens.h
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
 * @file ui_screens.h
 * @brief MySafeFob App — internal (component-private, not installed
 *        outside boards/x4pro/app) navigation contract shared between
 *        ui_nav.cpp (the orchestrator: task loop, activity/sleep, the
 *        Screen array) and each ui_screen_*.cpp (one per screen, see
 *        docs/ROADMAP.md's LVGL split). Not part of the public API in
 *        ui_nav.h.
 *
 *        Each build_xxx() below builds its screen once (called from
 *        ui_nav.cpp's build_screens(), at board_ui_nav_task startup),
 *        returning the screen object plus, via the two output params, the
 *        lv_group_t it created for itself (lv_port_indev_new_group() --
 *        see lv_port_indev.h: one group per screen, never shared) and the
 *        top-right battery/charge label add_back_header()/build_home()
 *        placed on it (ui_nav.cpp keeps refreshing whichever one belongs
 *        to the currently visible screen).
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus

enum class Screen { Home, Settings, SettingsControls, SettingsAbout, SettingsDisplay, TouchDiag, Security, Time, Alerts, OwnerInfo,
                    SettingsDevice, SettingsSecurityData, KeyboardTest, Names, Account, AccountEdit,
                    FieldEdit, kCount };

/**
 * @brief Switches to screen `s`: refreshes its battery label, forces the
 *        next flush to be a full GC refresh (UI-SPECS.md §1.1: full
 *        refresh on a screen-type change), and loads it. Defined in
 *        ui_nav.cpp; called both from there (back/menu navigation, via
 *        each screen's own event callbacks) and nowhere else.
 */
void switch_screen(Screen s);

/**
 * @brief Enters deep sleep (never returns on success) — shared by Home's
 *        "Sleep now" button (ui_screen_home.cpp) and ui_nav.cpp's own
 *        ADR-012 idle-timeout check. `reason` is only used for the log
 *        line.
 */
void enter_sleep_from_idle_or_menu(const char *reason);

/**
 * @brief Routes Left/Right and the confirm pulses (Home pad, Power short
 *        press) to `group` instead of the current screen's group, while a
 *        modal is open; nullptr restores the screen's group. Defined in
 *        ui_nav.cpp, used by ui_confirm_show() (ui_widgets.cpp).
 */
void ui_nav_set_modal_group(lv_group_t *group);

/**
 * @brief Re-applies frontlight.h's PWM outputs from the current
 *        settings_store.h values. Defined in ui_screen_settings_display.cpp
 *        (owner of the Frontlight/Color/Intensity controls); also called
 *        once from ui_nav.cpp's board_ui_nav_task at boot, right after
 *        frontlight_init(), so the persisted preference takes effect
 *        before any screen is shown.
 */
void apply_frontlight_from_settings(void);

lv_obj_t *build_home(lv_group_t **group_out, lv_obj_t **battery_label_out);
lv_obj_t *build_settings(lv_group_t **group_out, lv_obj_t **battery_label_out);
lv_obj_t *build_settings_device(lv_group_t **group_out, lv_obj_t **battery_label_out);
lv_obj_t *build_settings_security_data(lv_group_t **group_out, lv_obj_t **battery_label_out);
lv_obj_t *build_settings_controls(lv_group_t **group_out, lv_obj_t **battery_label_out);
lv_obj_t *build_settings_display(lv_group_t **group_out, lv_obj_t **battery_label_out);
lv_obj_t *build_settings_about(lv_group_t **group_out, lv_obj_t **battery_label_out);
lv_obj_t *build_touch_diag(lv_group_t **group_out, lv_obj_t **battery_label_out);
lv_obj_t *build_security(lv_group_t **group_out, lv_obj_t **battery_label_out);
lv_obj_t *build_time(lv_group_t **group_out, lv_obj_t **battery_label_out);
lv_obj_t *build_alerts(lv_group_t **group_out, lv_obj_t **battery_label_out);
lv_obj_t *build_owner_info(lv_group_t **group_out, lv_obj_t **battery_label_out);
lv_obj_t *build_keyboard_test(lv_group_t **group_out, lv_obj_t **battery_label_out);
lv_obj_t *build_names(lv_group_t **group_out, lv_obj_t **battery_label_out);
lv_obj_t *build_account(lv_group_t **group_out, lv_obj_t **battery_label_out);
lv_obj_t *build_account_edit(lv_group_t **group_out, lv_obj_t **battery_label_out);
lv_obj_t *build_field_edit(lv_group_t **group_out, lv_obj_t **battery_label_out);

/** @brief Opens EDIT on a copy of account `id` (ui_screen_account_edit.cpp). */
void account_edit_open(uint16_t id);

/**
 * @brief Opens EDIT for a new account; `letter` ('A'-'Z') prefills the name
 *        and Back returns to that letter's NAMES ('#' too, without
 *        prefill); 0 = from HOME.
 */
void account_edit_open_new(char letter);

/** @brief Opens NAMES for `letter` ('A'-'Z' or '#'), ADR-019 (ui_screen_names.cpp). */
void names_open(char letter);

/** @brief Opens the ACCOUNT page of record `id`, ADR-019 (ui_screen_account.cpp). */
void account_open(uint16_t id);

/** @brief esp_timer time of the last user input (ADR-012 activity, ui_nav.cpp). */
int64_t ui_nav_last_activity_us(void);

#endif /* __cplusplus */
