/*
 Project: MySafeFob  ui_screen_settings_controls.cpp
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
 * @file ui_screen_settings_controls.cpp
 * @brief MySafeFob App — Settings > Controls (Power short-press confirm,
 *        auto-sleep timeout).
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "lv_port_indev.h"

extern "C" {
#include "settings_store.h"
}

static constexpr uint32_t kAutoSleepPresets[] = {0, 30, 60, 120};
static const char *const kAutoSleepLabels[] = {"Off", "30s", "1min", "2min"};
static constexpr int kAutoSleepPresetCount = 4;

static void power_confirm_deferred(void *user_data)
{
    bool checked = static_cast<bool>(reinterpret_cast<intptr_t>(user_data));
    settings_store_set_power_short_confirm(checked);
}

static void power_confirm_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ui_defer(power_confirm_deferred, reinterpret_cast<void *>(static_cast<intptr_t>(checked)));
}

static lv_obj_t *s_idle_value_label;
static int s_idle_index;

static void set_idle_index(int index)
{
    s_idle_index = (index + kAutoSleepPresetCount) % kAutoSleepPresetCount;
    settings_store_set_idle_timeout_s(kAutoSleepPresets[s_idle_index]);
    lv_label_set_text(s_idle_value_label, kAutoSleepLabels[s_idle_index]);
}

static void idle_step_deferred(void *user_data)
{
    int delta = static_cast<int>(reinterpret_cast<intptr_t>(user_data));
    set_idle_index(s_idle_index + delta);
}

static void idle_prev_cb(lv_event_t *e) { (void)e; ui_defer(idle_step_deferred, reinterpret_cast<void *>(static_cast<intptr_t>(-1))); }
static void idle_next_cb(lv_event_t *e) { (void)e; ui_defer(idle_step_deferred, reinterpret_cast<void *>(static_cast<intptr_t>(1))); }

lv_obj_t *build_settings_controls(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, "Controls", Screen::Settings, group, battery_label_out);
    lv_obj_t *content = make_content(screen);

    make_round_toggle_row(content, group, "Power short press",
                          settings_store_get_power_short_confirm(), power_confirm_switch_cb);

    make_stepper_row(content, group, "Auto-sleep", idle_prev_cb, idle_next_cb, &s_idle_value_label);
    s_idle_index = nearest_preset_index(kAutoSleepPresets, kAutoSleepPresetCount,
                                        settings_store_get_idle_timeout_s());
    lv_label_set_text(s_idle_value_label, kAutoSleepLabels[s_idle_index]);

    return screen;
}
