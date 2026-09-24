/*
 Project: MySafeFob  ui_widgets.cpp
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
 * @file ui_widgets.cpp
 * @brief MySafeFob App — generic LVGL builders shared by every screen (see
 *        ui_widgets.h). Split out of the old monolithic ui_nav.cpp
 *        (docs/ROADMAP.md, one file per screen/component).
 */
#include "ui_widgets.h"
#include "ui_nav.h"

extern "C" {
#include "hw_config.h"
#include "battery.h"
#include "esp_log.h"
}

#include "app_log_workaround.h"

#include <cstdio>
#include <cstring>

static const char *TAG = "ui_widgets";

void ui_defer(lv_async_cb_t fn, void *user_data)
{
    if (lv_async_call(fn, user_data) != LV_RESULT_OK) {
        ESP_LOGE(TAG, "lv_async_call failed (OOM?) -- running deferred callback synchronously");
        fn(user_data);
    }
}

/* NOTE for any new focusable control added later: it must give itself, or
 * its DIRECT parent, kFocusOutlineSlack of room on whichever side(s) it can
 * sit flush against that parent's edge, or its focus outline will get
 * clipped (2026-09-22 bug, "on ne voit pas la partie qui focus a droite...").
 * Every control here already follows it: make_row() reserves it for a row's
 * right-hand child, make_stepper_row()'s "controls" wrapper reserves it
 * separately for prev_btn/next_btn, whose direct parent is that wrapper,
 * not the row. A plain lv_obj_create(parent) sized with LV_SIZE_CONTENT to
 * exactly wrap its children (the pattern used everywhere here) is the case
 * that always needs this -- it has, by construction, zero room of its own. */

void add_to_group(lv_obj_t *obj, lv_group_t *group)
{
    lv_obj_set_style_outline_width(obj, 4, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(obj, lv_color_black(), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(obj, 3, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(obj, kButtonBorderWidth, LV_STATE_PRESSED);
    lv_group_add_obj(group, obj);
}

lv_obj_t *make_button(lv_obj_t *parent, const char *label_text)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_style_min_width(btn, 140, 0);
    lv_obj_set_style_min_height(btn, kMinTouchTarget, 0);
    lv_obj_set_style_radius(btn, kButtonRadius, 0);
    lv_obj_set_style_border_width(btn, kButtonBorderWidth, 0);
    lv_obj_set_ext_click_area(btn, kExtClickMargin);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, label_text);
    lv_obj_center(label);
    return btn;
}

/* A plain lv_obj_create(NULL) screen keeps the base theme's default
 * padding and stays scrollable -- on a paginated e-ink UI nothing should
 * ever scroll, and default padding not accounted for anywhere else here
 * could unbalance flex centering (suspected after a 2026-09-21 hardware
 * test: Home's "Sleep now" button, flex-centered, sat with its right edge
 * almost against the panel border and roughly a third of its own width of
 * margin on the left instead of even margins). */
lv_obj_t *make_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_size(screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    return screen;
}

/* Same fixed-position line every screen sits below, header or not. */
lv_obj_t *make_content(lv_obj_t *screen)
{
    lv_obj_t *content = lv_obj_create(screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100 - kHeaderReservedPct));
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 16, 0);
    lv_obj_set_style_pad_all(content, 16, 0);
    return content;
}

/* pad_right/top/bottom (not pad_left -- the label on the left isn't
 * focusable, no outline to clip) give kFocusOutlineSlack's worth of room
 * for a focused RIGHT-hand child's outline ring, for whichever control
 * sits directly in this row (the round toggle -- make_stepper_row()'s own
 * "controls" wrapper needs and gets its own separate slack, see its twin
 * comment). */
lv_obj_t *make_row(lv_obj_t *parent, const char *label_text)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(row, kFocusOutlineSlack, 0);
    lv_obj_set_style_pad_ver(row, kFocusOutlineSlack, 0);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, label_text);
    return row;
}

/* A synthetic LV_EVENT_CLICKED (board_ui_nav_task's Power-short-press/
 * touch-Home confirm pulse, sent to whatever is focused) reaches this
 * toggle no differently than the "< Back"/stepper buttons it sits next
 * to in its group -- but unlike them, it's CHECKABLE, and its actual
 * check-toggling normally happens through LVGL's own default handler on
 * a real touch release (LV_EVENT_RELEASED), not on LV_EVENT_CLICKED.
 * Without this, a confirm pulse landing on the focused toggle did nothing
 * visible at all (2026-09-21 bug report: "il faut sortir de l'écran pour
 * que power soit actif pour short press"). Toggling manually here on
 * CLICKED closes that gap -- guarded by lv_indev_get_act() so a REAL touch
 * tap (which already flips the state via LVGL's own RELEASED handler,
 * then also fires CLICKED right after) doesn't get double-toggled back to
 * where it started: lv_indev_get_act() is only non-null while LVGL is
 * actively processing that real touch's input cycle, never during this
 * synthetic send_event() call from the task loop.
 *
 * DELIBERATE EXCEPTION to ui_defer()'s standing rule (ui_widgets.h): this
 * check is only meaningful evaluated synchronously, during the very indev
 * dispatch (real or synthetic) that produced this event -- ui_defer()ing it
 * would run the lv_indev_get_act() check one lv_timer_handler() pass later,
 * by which point it's back to NULL regardless of whether this CLICKED came
 * from a real touch or the synthetic pulse, breaking the distinction this
 * whole function exists for. */
static void toggle_confirm_click_cb(lv_event_t *e)
{
    if (lv_indev_get_act() != nullptr) {
        return;
    }
    lv_obj_t *toggle = lv_event_get_target_obj(e);
    if (lv_obj_has_state(toggle, LV_STATE_CHECKED)) {
        lv_obj_clear_state(toggle, LV_STATE_CHECKED);
    } else {
        lv_obj_add_state(toggle, LV_STATE_CHECKED);
    }
    lv_obj_send_event(toggle, LV_EVENT_VALUE_CHANGED, nullptr);
}

/* label_text on the left (make_row()) + a round toggle indicator on the
 * right (2026-09-21, user request: "decoreler le texte et le mettre a
 * gauche de la case qui serait ronde" -- lv_checkbox's own label is
 * fused to its (square) indicator with a fixed left-box/right-text
 * order that can't be reversed, so this is a plain lv_obj instead:
 * LV_OBJ_FLAG_CHECKABLE makes ANY object toggle LV_STATE_CHECKED and
 * fire LV_EVENT_VALUE_CHANGED on a real tap release, the same generic
 * mechanism lv_switch/lv_checkbox build on (lv_obj.c's default event
 * handler) -- not specific to those widget classes. */
lv_obj_t *make_round_toggle_row(lv_obj_t *parent, lv_group_t *group, const char *label_text,
                                bool initial_checked, lv_event_cb_t value_changed_cb)
{
    lv_obj_t *row = make_row(parent, label_text);

    lv_obj_t *toggle = lv_obj_create(row);
    lv_obj_remove_style_all(toggle);
    lv_obj_set_size(toggle, 36, 36);
    /* 2026-09-21 user report: sat flush against the row's/screen's right
     * edge; later request: line its center up with the "Neutral"/"100%"
     * value-label center on the stepper rows below (Color/Intensity), for
     * visual balance across Display's rows. Distance from a row's right
     * edge to that center = next-button width (kMinTouchTarget=56) +
     * controls' column gap (16) + half the value label's width (130/2=65)
     * = 137px; the toggle's own center sits at margin_right + half its
     * width (18), so margin_right = 137 - 18 = 119.
     *
     * +kFocusOutlineSlack (2026-09-22, clipped-outline fix): this row's own
     * pad_right (make_row()) shifts BOTH this toggle and the stepper rows'
     * "controls" container left by the same amount, so it cancels out of
     * this alignment and needed no change here. But make_stepper_row()'s
     * "controls" ALSO gets its own separate pad_right, on top of that,
     * shifting the value label (and next_btn) an extra kFocusOutlineSlack
     * left that this toggle doesn't share -- added back here to keep the
     * two centered on the same line. */
    lv_obj_set_style_margin_right(toggle, 119 + kFocusOutlineSlack, 0);
    lv_obj_set_style_radius(toggle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(toggle, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(toggle, kButtonBorderWidth, 0);
    lv_obj_set_style_border_color(toggle, lv_color_black(), 0);
    lv_obj_set_style_bg_color(toggle, lv_color_black(), LV_STATE_CHECKED);
    lv_obj_add_flag(toggle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(toggle, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_ext_click_area(toggle, kExtClickMargin);
    if (initial_checked) {
        lv_obj_add_state(toggle, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(toggle, value_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(toggle, toggle_confirm_click_cb, LV_EVENT_CLICKED, nullptr);

    /* add_to_group() now styles focus with an outline ring too (2026-09-21
     * user request), so it no longer collides with LV_STATE_CHECKED's bg
     * fill the way its old invert style did -- this can just use it like
     * every other focusable control now. */
    add_to_group(toggle, group);

    return row;
}

/* No lv_slider/lv_roller anywhere in this UI (2026-09-21, user guidance):
 * both render/redraw slower and feel less responsive than on a TFT when
 * every value change forces a full e-paper flush -- discrete -/+ (or
 * prev/next) buttons instead, for every setting that only ever takes a
 * handful of values. */
static lv_obj_t *make_stepper_button(lv_obj_t *parent, const char *label_text)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_style_min_width(btn, kMinTouchTarget, 0);
    lv_obj_set_style_min_height(btn, kMinTouchTarget, 0);
    lv_obj_set_style_radius(btn, kButtonRadius, 0);
    lv_obj_set_style_border_width(btn, kButtonBorderWidth, 0);
    lv_obj_set_ext_click_area(btn, kExtClickMargin);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, label_text);
    /* "+"/"-" read as too small at the default 24px (2026-09-21) -- a
     * bigger font just for this glyph, not the app-wide default. */
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
    lv_obj_center(label);
    return btn;
}

/* label_text row with [-] value [+] on the right (make_row() already
 * places its own label at the far left via SPACE_BETWEEN flex; this
 * container is the row's second/last child, so it lands at the far
 * right). *value_label_out receives the middle label to update from the
 * -/+ callbacks. */
lv_obj_t *make_stepper_row(lv_obj_t *parent, lv_group_t *group, const char *label_text,
                           lv_event_cb_t prev_cb, lv_event_cb_t next_cb, lv_obj_t **value_label_out)
{
    lv_obj_t *row = make_row(parent, label_text);

    lv_obj_t *controls = lv_obj_create(row);
    lv_obj_remove_style_all(controls);
    lv_obj_set_size(controls, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    /* 2026-09-21 user report: "Neutral" wrapped to "Neutr"/"al" -- the
     * value label was too narrow for its longest word at this font size,
     * and the -/+ buttons sat too close to it and to each other. */
    lv_obj_set_style_pad_column(controls, 16, 0);
    /* kFocusOutlineSlack on all 4 sides (2026-09-22 clipped-outline fix):
     * prev_btn/next_btn's DIRECT parent is THIS container (not the row),
     * and it was sized with LV_SIZE_CONTENT to exactly wrap them with zero
     * padding of its own -- their focus outline ring had nowhere to go
     * and got clipped on every side that touched controls' own edge (top/
     * bottom always, and specifically the right on next_btn, the button
     * closest to the screen's edge). make_round_toggle_row()'s margin_right
     * compensates for the shift this adds to the value label's position,
     * to keep it lined up with the toggle above. */
    lv_obj_set_style_pad_all(controls, kFocusOutlineSlack, 0);

    lv_obj_t *prev_btn = make_stepper_button(controls, "-");
    lv_obj_add_event_cb(prev_btn, prev_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(prev_btn, group);

    lv_obj_t *value_label = lv_label_create(controls);
    lv_obj_set_width(value_label, 130);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *next_btn = make_stepper_button(controls, "+");
    lv_obj_add_event_cb(next_btn, next_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(next_btn, group);

    *value_label_out = value_label;
    return row;
}

/* Full-width divider directly below a header row (2026-09-21, user
 * request: screen title top-left, a line below spanning the width). */
lv_obj_t *add_divider_below(lv_obj_t *screen, lv_obj_t *above)
{
    lv_obj_t *divider = lv_obj_create(screen);
    lv_obj_remove_style_all(divider);
    lv_obj_set_style_bg_color(divider, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_size(divider, LV_PCT(100), 2);
    lv_obj_align_to(divider, above, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    return divider;
}

static void back_event_deferred(void *user_data)
{
    switch_screen(static_cast<Screen>(reinterpret_cast<intptr_t>(user_data)));
}

/* 2026-09-23: while a screen has board_ui_nav_suppress_touch_confirm()
 * turned on (Settings > Touch Calibration, for as long as it's active),
 * "< Back" must only be reachable via Left/Right + Power, not a direct
 * touch tap on the button itself either -- see that function's doc
 * comment (ui_nav.h) for the hardware bug this closes. lv_indev_get_act()
 * is non-NULL only while LVGL is actively processing a REAL touch's input
 * cycle (the same check toggle_confirm_click_cb below uses, for the
 * opposite purpose); the synthetic confirm pulse this escape hatch relies
 * on is sent with indev_act NULL, so it's never affected by this guard. */
static void back_event_cb(lv_event_t *e)
{
    if (lv_indev_get_act() != nullptr && board_ui_nav_is_touch_confirm_suppressed()) {
        return;
    }
    ui_defer(back_event_deferred, lv_event_get_user_data(e));
}

/* battery.h's battery_read() (CW2017 gauge, ADR-014) -- refreshed on
 * switch_screen() (every screen entry) AND on a periodic timer
 * (ui_nav.cpp's battery_timer_cb, 10s while charging / 30s otherwise,
 * 2026-09-21 request), not a single fixed-rate poll: redrawing on a fixed
 * clock flushes the whole e-paper screen for no reason when nothing
 * changed -- a much slower interval, adaptive to charging state (the case
 * that actually needs to be seen updating live), keeps that cost rare
 * while still not going stale for an unbounded time on a screen nobody
 * navigates away from (2026-09-21 bug report: it used to stay stuck
 * showing "charging" long after the cable was pulled).
 *
 * Two separate glyphs when charging, not one replacing the other
 * (2026-09-21 bug report: "il doit y en avoir 2 batterie+charge, sinon
 * que batterie" -- LV_SYMBOL_CHARGE alone hid the actual level). Auto-sized
 * label, no clip/fixed-width/right-align (2026-09-21: that combination
 * used to silently clip whichever icon was wider than the budget, e.g.
 * LV_SYMBOL_BATTERY_FULL but not the narrower LV_SYMBOL_CHARGE).
 *
 * Returns whether the panel is charging right now, so battery_timer_cb()
 * can pick its next interval off the same battery_read() call instead of
 * reading it twice. */
bool refresh_battery_label(lv_obj_t *label)
{
    uint8_t soc = 0;
    bool charging = false;
    bool have_battery = battery_read(&soc, &charging);

    if (!label) {
        return have_battery && charging;
    }

    char buf[32];
    if (!have_battery) {
        snprintf(buf, sizeof(buf), "%s --", LV_SYMBOL_BATTERY_EMPTY);
    } else {
        const char *level_icon;
        if (soc >= 80) {
            level_icon = LV_SYMBOL_BATTERY_FULL;
        } else if (soc >= 50) {
            level_icon = LV_SYMBOL_BATTERY_2;
        } else if (soc >= 20) {
            level_icon = LV_SYMBOL_BATTERY_1;
        } else {
            level_icon = LV_SYMBOL_BATTERY_EMPTY;
        }
        if (charging) {
            snprintf(buf, sizeof(buf), "%s %u%%%s", level_icon, static_cast<unsigned>(soc), LV_SYMBOL_CHARGE);
        } else {
            snprintf(buf, sizeof(buf), "%s %u%%", level_icon, static_cast<unsigned>(soc));
        }
    }

    /* lv_label_set_text() invalidates unconditionally, even for identical
     * text -- with battery_timer_cb (ui_nav.cpp) calling this every 10-30s,
     * that alone was a full e-ink flush on a fixed clock with nothing
     * changed (2026-09-24 user report: "je vois encore des refresh auto en
     * periode d'inactivite"). Only touch the label when the text differs. */
    if (strcmp(lv_label_get_text(label), buf) != 0) {
        lv_label_set_text(label, buf);
    }
    return have_battery && charging;
}

lv_obj_t *make_battery_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    refresh_battery_label(label);
    return label;
}

/* Two different margins (2026-09-21, user correction): the title/battery
 * row and the divider under it are purely informational, nobody needs to
 * TOUCH them, so they belong near the true top of the panel like the old
 * FreeInkUI header did -- pushing them down past the dead zone too just
 * left a big empty gap for no reason. Only the "< Back" button, being
 * the one interactive element up here, actually needs to clear
 * docs/touch-calibration-notes.md §10's measured ~87px boundary.
 * kHeaderReservedPct (make_content()'s top space) sized for the Back
 * button's real position, not the info row's.
 *
 * kHeaderInfoHeight (2026-09-21, user correction): the info row uses
 * the WHOLE 0-80 band, text vertically centered in it (flex cross-align
 * CENTER on a fixed-height bar, not just top-aligned in a tiny strip) --
 * the divider sits right at y=80, and Back starts at y=88, just past
 * the measured dead-zone boundary instead of a much bigger margin.
 *
 * Raised by 15px (2026-09-21, user request "remonter la ligne du header")
 * -- Back's own position (kBackTopMargin) is independent of this and
 * stays where the dead-zone measurement put it. */
lv_obj_t *add_back_header(lv_obj_t *screen, const char *title, Screen back_target, lv_group_t *group,
                          lv_obj_t **battery_label_out)
{
    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), kHeaderInfoHeight);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(bar, 16, 0);
    /* Top-only padding shifts the CENTER-aligned content down within the
     * bar's own fixed height, without moving the bar itself (still
     * LV_ALIGN_TOP_MID at y=0) or the divider right under it -- title and
     * battery text sat too close to the very top edge after the header
     * was raised 15px (2026-09-21 request), this settles them back onto
     * a visually centered line. */
    lv_obj_set_style_pad_top(bar, kHeaderContentPadTop, 0);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *title_label = lv_label_create(bar);
    lv_label_set_text(title_label, title);

    *battery_label_out = make_battery_label(bar);

    add_divider_below(screen, bar);

    lv_obj_t *back = make_button(screen, "< Back");
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 16, kBackTopMargin);
    lv_obj_add_event_cb(back, back_event_cb, LV_EVENT_CLICKED,
                         reinterpret_cast<void *>(static_cast<intptr_t>(back_target)));
    add_to_group(back, group);

    return bar;
}

int nearest_preset_index(const uint32_t *presets, int count, uint32_t value)
{
    int best = 0;
    uint32_t best_diff = UINT32_MAX;
    for (int i = 0; i < count; i++) {
        uint32_t diff = value > presets[i] ? value - presets[i] : presets[i] - value;
        if (diff < best_diff) {
            best_diff = diff;
            best = i;
        }
    }
    return best;
}
