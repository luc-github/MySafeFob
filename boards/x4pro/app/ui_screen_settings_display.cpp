/*
 Project: MySafeFob  ui_screen_settings_display.cpp
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
 * @file ui_screen_settings_display.cpp
 * @brief MySafeFob App — Settings > Display (frontlight on/off, color mix,
 *        intensity).
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "lv_port_indev.h"
#include "lv_port_disp.h"

extern "C" {
#include "settings_store.h"
#include "frontlight.h"
#include "esp_log.h"
}

#include "app_log_workaround.h"

#include <cstdio>

/* Logged on every value change (2026-09-22, diagnosing "+ takes 4-6s to show
 * 100%"): app_log_workaround.h's ESP_LOGI already timestamps every line in
 * ms, so pairing this with lv_port_indev.c's touch press/release log and
 * lv_port_disp.c's per-flush "done in N ms" log in the same serial capture
 * shows exactly where the time between a tap and its visible result goes
 * (touch edge -> this callback -> which flush -> how long that flush took). */
static const char *TAG = "ui_display";

static constexpr uint32_t kColorMixPresets[] = {0, 50, 100};
static const char *const kColorMixLabels[] = {"Warm", "Neutral", "Cool"};
static constexpr int kColorMixPresetCount = 3;
static constexpr int kIntensityStep = 10;

static lv_obj_t *s_color_value_label;
static lv_obj_t *s_intensity_value_label;
static lv_obj_t *s_color_prev_btn, *s_color_next_btn;
static lv_obj_t *s_intensity_prev_btn, *s_intensity_next_btn;
static lv_obj_t *s_color_row, *s_intensity_row;
static lv_group_t *s_display_group;
static int s_color_index;
static int s_intensity_value;

void apply_frontlight_from_settings(void)
{
    frontlight_apply(settings_store_get_frontlight_on(),
                      static_cast<uint8_t>(settings_store_get_frontlight_color()),
                      static_cast<uint8_t>(settings_store_get_frontlight_intensity()));
}

/* Whole rows, not just the +/- buttons' borders/DISABLED state (2026-09-22
 * request: "pourquoi ne pas tout cacher ? texte et +/-" -- there's nothing
 * for Color/Intensity to mean while Frontlight is off, so hide the label
 * text too, not merely grey out the buttons). LV_OBJ_FLAG_HIDDEN also
 * pulls a row out of the flex layout entirely (LVGL skips hidden children
 * when it lays out a flex container), so make_content()'s column closes
 * the gap by itself instead of leaving a blank row-shaped hole.
 *
 * The +/- buttons are also pulled out of the group while hidden (not just
 * DISABLED), so Left/Right navigation skips straight over them instead of
 * stopping on an invisible, unusable focus target -- re-added in the same
 * order they were first added (lv_group_add_obj() always appends) when
 * Frontlight is switched back on. */
static void set_display_extra_visible(bool visible)
{
    lv_obj_t *rows[] = {s_color_row, s_intensity_row};
    for (lv_obj_t *row : rows) {
        if (visible) {
            lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_t *btns[] = {s_color_prev_btn, s_color_next_btn, s_intensity_prev_btn, s_intensity_next_btn};
    for (lv_obj_t *btn : btns) {
        if (visible) {
            lv_group_add_obj(s_display_group, btn);
        } else {
            lv_group_remove_obj(btn);
        }
    }
}

/* Deferred via ui_defer() -- see its own doc comment (ui_widgets.h) for the
 * incident this control caused and the resulting standing rule: EVERY
 * click/value-changed callback in this app defers through it, not just
 * this one. This toggle was the concrete case that found the problem
 * (set_display_extra_visible()'s lv_group_add_obj()/lv_group_remove_obj()
 * on the very group whose click triggered it), occasionally dropping a
 * rapid second tap's effect entirely. */
static void frontlight_deferred(void *user_data)
{
    bool on = static_cast<bool>(reinterpret_cast<intptr_t>(user_data));
    settings_store_set_frontlight_on(on);
    apply_frontlight_from_settings();
    set_display_extra_visible(on);
    /* The 2026-09-20 "toggle forces a full GC refresh" lesson
     * (docs/ROADMAP.md) was about a full-width lv_switch's large-area
     * fill flip -- the round toggle itself (2026-09-21) is a small 36px
     * indicator a fast DU refresh handles fine. But showing/hiding the
     * whole Color/Intensity rows (2026-09-22) reflows the rest of the
     * column underneath them, a bigger and less predictable-shaped delta
     * than a single small indicator -- back to forcing a full refresh
     * for THIS toggle specifically, so the reflow doesn't leave DU
     * ghosting behind. */
    lv_port_disp_request_full_refresh();
}

static void frontlight_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ESP_LOGI(TAG, "frontlight -> %s", on ? "on" : "off");
    ui_defer(frontlight_deferred, reinterpret_cast<void *>(static_cast<intptr_t>(on)));
}

static void set_color_index(int index)
{
    s_color_index = (index + kColorMixPresetCount) % kColorMixPresetCount;
    ESP_LOGI(TAG, "color -> %s", kColorMixLabels[s_color_index]);
    settings_store_set_frontlight_color(kColorMixPresets[s_color_index]);
    apply_frontlight_from_settings();
    lv_label_set_text(s_color_value_label, kColorMixLabels[s_color_index]);
}

static void color_step_deferred(void *user_data)
{
    int delta = static_cast<int>(reinterpret_cast<intptr_t>(user_data));
    set_color_index(s_color_index + delta);
}

static void color_prev_cb(lv_event_t *e) { (void)e; ui_defer(color_step_deferred, reinterpret_cast<void *>(static_cast<intptr_t>(-1))); }
static void color_next_cb(lv_event_t *e) { (void)e; ui_defer(color_step_deferred, reinterpret_cast<void *>(static_cast<intptr_t>(1))); }

static void set_intensity_value(int value)
{
    s_intensity_value = value < 0 ? 0 : (value > 100 ? 100 : value);
    ESP_LOGI(TAG, "intensity -> %d%%", s_intensity_value);
    settings_store_set_frontlight_intensity(static_cast<uint32_t>(s_intensity_value));
    apply_frontlight_from_settings();
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", s_intensity_value);
    lv_label_set_text(s_intensity_value_label, buf);
}

static void intensity_step_deferred(void *user_data)
{
    int delta = static_cast<int>(reinterpret_cast<intptr_t>(user_data));
    set_intensity_value(s_intensity_value + delta * kIntensityStep);
}

static void intensity_prev_cb(lv_event_t *e) { (void)e; ui_defer(intensity_step_deferred, reinterpret_cast<void *>(static_cast<intptr_t>(-1))); }
static void intensity_next_cb(lv_event_t *e) { (void)e; ui_defer(intensity_step_deferred, reinterpret_cast<void *>(static_cast<intptr_t>(1))); }

lv_obj_t *build_settings_display(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;
    s_display_group = group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, "Display", Screen::Settings, group, battery_label_out);
    lv_obj_t *content = make_content(screen);

    bool on = settings_store_get_frontlight_on();

    make_round_toggle_row(content, group, "Frontlight", on, frontlight_switch_cb);

    s_color_row = make_stepper_row(content, group, "Color", color_prev_cb, color_next_cb,
                                   &s_color_value_label);
    lv_obj_t *color_controls = lv_obj_get_child(s_color_row, 1);
    s_color_prev_btn = lv_obj_get_child(color_controls, 0);
    s_color_next_btn = lv_obj_get_child(color_controls, 2);
    s_color_index = nearest_preset_index(kColorMixPresets, kColorMixPresetCount,
                                         settings_store_get_frontlight_color());
    lv_label_set_text(s_color_value_label, kColorMixLabels[s_color_index]);

    s_intensity_row = make_stepper_row(content, group, "Intensity", intensity_prev_cb, intensity_next_cb,
                                       &s_intensity_value_label);
    lv_obj_t *intensity_controls = lv_obj_get_child(s_intensity_row, 1);
    s_intensity_prev_btn = lv_obj_get_child(intensity_controls, 0);
    s_intensity_next_btn = lv_obj_get_child(intensity_controls, 2);
    s_intensity_value = static_cast<int>(settings_store_get_frontlight_intensity());
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", s_intensity_value);
    lv_label_set_text(s_intensity_value_label, buf);

    set_display_extra_visible(on);

    return screen;
}
