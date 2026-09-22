/*
 Project: MySafeFob  ui_nav.cpp
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
 * @file ui_nav.cpp
 * @brief MySafeFob App — interactive menu + Settings screens, on LVGL
 *        (ADR-010 amended again: LVGL replaces FreeInkUI — see
 *        docs/ROADMAP.md). Each screen is a plain lv_obj_t built once at
 *        init; navigation is `lv_screen_load()` between them.
 *
 * Also owns the ADR-012 activity manager: idle timeout tracked across
 * this loop's own button/touch events (via lv_port_indev.c's sampler
 * task, which calls board_activity_notify()) plus main.c's own calls
 * (Power button presses, REPL commands = "serial activity").
 */
#include "ui_nav.h"
#include "splash.h"
#include "settings_store.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

extern "C" {
#include "hw_config.h"
#include "battery.h"
#include "frontlight.h"
#include "power_mgr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

#include "lvgl.h"

#include <atomic>
#include <cstdio>
#include <cstring>

/* WORKAROUND (2026-09-18, see touch.c's twin comment): standard ESP_LOG*
 * calls from this file never reach the serial monitor, 100% reproducible
 * -- a raw printf() from the same call site always works. Scoped to this
 * translation unit only; revert once the real cause is found. Lost when
 * this file was rewritten for LVGL (2026-09-21) -- every diagnostic log
 * added since then was silently invisible; re-added here. */
#undef ESP_LOGE
#undef ESP_LOGW
#undef ESP_LOGI
#define ESP_LOGE(tag, fmt, ...) do { \
        printf("E (%lld) %s: " fmt "\r\n", (long long)(esp_timer_get_time() / 1000), tag, ##__VA_ARGS__); \
        fflush(stdout); \
    } while (0)
#define ESP_LOGW(tag, fmt, ...) do { \
        printf("W (%lld) %s: " fmt "\r\n", (long long)(esp_timer_get_time() / 1000), tag, ##__VA_ARGS__); \
        fflush(stdout); \
    } while (0)
#define ESP_LOGI(tag, fmt, ...) do { \
        printf("I (%lld) %s: " fmt "\r\n", (long long)(esp_timer_get_time() / 1000), tag, ##__VA_ARGS__); \
        fflush(stdout); \
    } while (0)

static const char *TAG = "ui_nav";

/* -----------------------------------------------------------------------
 * Screens — same 6 as the FreeInkUI version (docs/UI-SPECS.md §1.4). Deep
 * sleep is a separate, non-LVGL screen (see ui_nav_suspend_lvgl_for_sleep()
 * further down) -- not one of these 6, not part of normal navigation.
 * ----------------------------------------------------------------------- */
enum class Screen { Home, Settings, SettingsControls, SettingsAbout, SettingsDisplay, TouchDiag, kCount };

static lv_obj_t *s_screens[static_cast<int>(Screen::kCount)];
/* One battery/charge status label per screen, refreshed on switch_screen()
 * below (2026-09-21 bug report: the label was only ever set once, when
 * each screen was built at startup -- charging state then stuck forever,
 * e.g. still showing "charging" long after the cable was unplugged). */
static lv_obj_t *s_battery_labels[static_cast<int>(Screen::kCount)];
/* One lv_group per screen (2026-09-21 hardware fix, see lv_port_indev.h) --
 * Left/Right only ever steps through the group of the CURRENTLY visible
 * screen, set active in switch_screen() below. */
static lv_group_t *s_groups[static_cast<int>(Screen::kCount)];
static Screen s_screen = Screen::Home;

/* Set right before any LVGL call made from a task OTHER than
 * board_ui_nav_task (main.c's power_button_task, on a Power long-press
 * that wins the ADR-009/012 sleep race) — board_ui_nav_task's own loop
 * checks this every iteration and stops pumping lv_timer_handler() once
 * it's set, so the two tasks never touch LVGL's object tree concurrently
 * during the brief window before deep sleep actually engages. */
static std::atomic<bool> s_lvgl_suspended{false};

static std::atomic<int64_t> s_last_activity_us{0};

void board_activity_notify(void)
{
    s_last_activity_us.store(esp_timer_get_time(), std::memory_order_relaxed);
}

static std::atomic<bool> s_power_confirm_pending{false};

void board_ui_nav_power_confirm(void)
{
    s_power_confirm_pending.store(true, std::memory_order_relaxed);
}

/* See ui_nav.h's doc comment: LVGL is not thread-safe -- these are only
 * ever set from lv_port_indev.c's input_sampler_task (a different task)
 * and consumed by board_ui_nav_task's own loop, which is the only task
 * that ever touches LVGL. */
static std::atomic<bool> s_focus_prev_pending{false};
static std::atomic<bool> s_focus_next_pending{false};

void board_ui_nav_focus_prev(void)
{
    s_focus_prev_pending.store(true, std::memory_order_relaxed);
}

void board_ui_nav_focus_next(void)
{
    s_focus_next_pending.store(true, std::memory_order_relaxed);
}

static bool refresh_battery_label(lv_obj_t *label);

static void switch_screen(Screen s)
{
    ESP_LOGI(TAG, "screen: %d -> %d", static_cast<int>(s_screen), static_cast<int>(s));
    s_screen = s;
    refresh_battery_label(s_battery_labels[static_cast<int>(s)]);
    lv_port_disp_request_full_refresh();   /* UI-SPECS.md §1.1: full refresh on screen-type change */
    lv_screen_load(s_screens[static_cast<int>(s)]);
}

static void back_event_cb(lv_event_t *e)
{
    Screen target = static_cast<Screen>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    switch_screen(target);
}

/* 2026-09-21 hardware test: only the explicitly-sized "Sleep now" button
 * responded reliably to touch; content-sized buttons (their tap area
 * exactly the text's small bounding box) mostly missed. Also, the mono
 * theme's default LV_STATE_FOCUSED style (Left/Right group navigation,
 * distinct from a touch LV_STATE_PRESSED) is subtle enough that focus
 * moving was invisible ("les boutons gauche/droite ne semblent rien
 * faire") -- a full background/text invert instead, same visual
 * language as the sleep screen's "ASLEEP" box. */
static constexpr int32_t kMinTouchTarget = 56;

/* Mono theme's own default border (1px, lv_theme_mono.c's BORDER_W_NORMAL)
 * barely shows up on this panel (2026-09-21: "il manque les contours des
 * boutons"). Used explicitly on every button/toggle's NORMAL state below,
 * and pinned to the SAME value for PRESSED (see add_to_group()) so a
 * press never causes a visual delta LVGL would otherwise invalidate the
 * whole screen for (the "toggle forces a full e-paper flush" lesson,
 * docs/ROADMAP.md). */
static constexpr int32_t kButtonBorderWidth = 3;

/* Outline ring for focus, not a bg/text color invert (2026-09-21, user
 * request: match the round toggle's own focus style, see
 * make_round_toggle_row()'s twin comment). The invert was originally
 * added because the mono theme's own default focus outline read as too
 * subtle to notice ("les boutons gauche/droite ne semblent rien faire") --
 * a thicker, explicit outline ring solves that the same way, without
 * touching bg/text color (which the invert always risked colliding with
 * a widget's own state-dependent styling, as it did on the toggle). */
static void add_to_group(lv_obj_t *obj, lv_group_t *group)
{
    lv_obj_set_style_outline_width(obj, 4, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(obj, lv_color_black(), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(obj, 3, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(obj, kButtonBorderWidth, LV_STATE_PRESSED);
    lv_group_add_obj(group, obj);
}

/* Extends the CLICKABLE area beyond an object's visible box, without
 * changing its visual size/layout -- LVGL's own mechanism for exactly
 * this (lv_obj_set_ext_click_area()). Found on hardware 2026-09-21: two
 * taps in a row on "Settings" missed by 1-3px (logged real coordinates,
 * landing just past the button's bottom edge) and were correctly ignored
 * by LVGL -- not a bug, just normal capacitive-touch/finger precision
 * against a tight target. A few extra pixels of forgiveness costs
 * nothing here. */
static constexpr int32_t kExtClickMargin = 16;

/* Rounded corners on every button (2026-09-21, user request) -- mono
 * theme's own default is square. */
static constexpr int32_t kButtonRadius = 12;

/* Slack reserved around a row's/controls container's own edges so a
 * focused child's outline ring (4px width + 3px pad = 7px past its own
 * box, see add_to_group()) has room to actually render (2026-09-22 bug
 * report: "on ne voit pas la partie qui focus a droite car trop pres de
 * la bordure de l'ecran a droite... meme souci... bordures haut et bas de
 * la checkbox et des boutons +/-"). LVGL clips a child's drawing to its
 * DIRECT parent's own box -- a button/toggle sitting flush against that
 * parent's edge (which every row/controls container here did, being
 * sized with LV_SIZE_CONTENT to exactly wrap its children) leaves zero
 * room for that overflow, cutting the outline off wherever it touched an
 * edge. A comfortable margin past the 7px actually needed. */
static constexpr int32_t kFocusOutlineSlack = 10;

/* NOTE for any new focusable control added later: it must give itself, or
 * its DIRECT parent, this same kFocusOutlineSlack of room on whichever
 * side(s) it can sit flush against that parent's edge, or its focus
 * outline will get clipped exactly the same way (2026-09-22 bug). Every
 * control in this file already follows it: make_row() reserves it for a
 * row's right-hand child (the round toggle, add_back_header()'s "< Back"
 * has plenty of room already since its parent is the whole screen), and
 * make_stepper_row()'s "controls" wrapper reserves it separately for
 * prev_btn/next_btn, whose direct parent is that wrapper, not the row. A
 * plain lv_obj_create(parent) sized with LV_SIZE_CONTENT to exactly wrap
 * its children (the pattern used everywhere here) is the case that always
 * needs this -- it has, by construction, zero room of its own. */

static lv_obj_t *make_button(lv_obj_t *parent, const char *label_text)
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

/* battery.h's battery_read() (CW2017 gauge, ADR-014) -- refreshed on
 * switch_screen() (every screen entry) AND on a periodic timer
 * (battery_timer_cb() below, 10s while charging / 30s otherwise, 2026-09-21
 * request), not a single fixed-rate poll: this app already learned the
 * hard way (Touch calibration's old 300ms poll) that redrawing on a fixed
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
static bool refresh_battery_label(lv_obj_t *label)
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

    lv_label_set_text(label, buf);
    return have_battery && charging;
}

static lv_obj_t *make_battery_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    refresh_battery_label(label);
    return label;
}

/* 2026-09-21 request: 10s while charging (changes fast enough to be worth
 * seeing live), 30s otherwise (discharge is slow -- no need to spend an
 * e-paper refresh more often than that, "on economise en etant sur
 * batterie"). Runs continuously from board_ui_nav_task's init, independent
 * of which screen is open -- refreshes whichever screen's battery label is
 * CURRENTLY visible (s_screen), same one switch_screen() already targets. */
static void battery_timer_cb(lv_timer_t *timer)
{
    bool charging = refresh_battery_label(s_battery_labels[static_cast<int>(s_screen)]);
    lv_timer_set_period(timer, charging ? 10000 : 30000);
}

/* Full-width divider directly below a header row (2026-09-21, user
 * request: screen title top-left, a line below spanning the width). */
static lv_obj_t *add_divider_below(lv_obj_t *screen, lv_obj_t *above)
{
    lv_obj_t *divider = lv_obj_create(screen);
    lv_obj_remove_style_all(divider);
    lv_obj_set_style_bg_color(divider, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_size(divider, LV_PCT(100), 2);
    lv_obj_align_to(divider, above, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    return divider;
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
static constexpr int32_t kHeaderInfoHeight = 65;
static constexpr int32_t kBackTopMargin = 88;
static constexpr int32_t kHeaderReservedPct = 20;
static constexpr int32_t kHeaderContentPadTop = 8;

/* Every non-Home screen: screen title (top-left) + battery (top-right)
 * near the true top of the panel, a divider line below spanning the
 * full width, then a "< Back" button positioned independently far
 * enough down to clear the top dead zone, returning to back_target.
 * `self` is this screen's own id, so its battery label can be found again
 * by switch_screen() and refreshed each time this screen is entered. */
static void add_back_header(lv_obj_t *screen, const char *title, Screen self, Screen back_target, lv_group_t *group)
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

    s_battery_labels[static_cast<int>(self)] = make_battery_label(bar);

    add_divider_below(screen, bar);

    lv_obj_t *back = make_button(screen, "< Back");
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 16, kBackTopMargin);
    lv_obj_add_event_cb(back, back_event_cb, LV_EVENT_CLICKED,
                         reinterpret_cast<void *>(static_cast<intptr_t>(back_target)));
    add_to_group(back, group);
}

/* Same fixed-position line every screen sits below, header or not. */
static lv_obj_t *make_content(lv_obj_t *screen)
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

/* A label + control on one row (Settings rows). pad_right/top/bottom (not
 * pad_left -- the label on the left isn't focusable, no outline to clip)
 * give kFocusOutlineSlack's worth of room for a focused RIGHT-hand child's
 * outline ring, for whichever control sits directly in this row (the
 * round toggle -- make_stepper_row()'s own "controls" wrapper needs and
 * gets its own separate slack, see its twin comment). */
static lv_obj_t *make_row(lv_obj_t *parent, const char *label_text)
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
 * que power soit actif pour short press", i.e. Power seemed to only ever
 * "work" once focus moved off this row onto a real button). Toggling
 * manually here on CLICKED closes that gap -- guarded by lv_indev_get_act()
 * so a REAL touch tap (which already flips the state via LVGL's own
 * RELEASED handler, then also fires CLICKED right after) doesn't get
 * double-toggled back to where it started: lv_indev_get_act() is only
 * non-null while LVGL is actively processing that real touch's input
 * cycle, never during this synthetic send_event() call from the task
 * loop. */
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
static lv_obj_t *make_round_toggle_row(lv_obj_t *parent, lv_group_t *group, const char *label_text,
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

/* No lv_slider/lv_roller anywhere in this file (2026-09-21, user
 * guidance): both render/redraw slower and feel less responsive than on
 * a TFT when every value change forces a full e-paper flush -- discrete
 * -/+ (or prev/next) buttons instead, for every setting that only ever
 * takes a handful of values. */
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
static lv_obj_t *make_stepper_row(lv_obj_t *parent, lv_group_t *group, const char *label_text,
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

/* A plain lv_obj_create(NULL) screen keeps the base theme's default
 * padding and stays scrollable -- on a paginated e-ink UI nothing should
 * ever scroll, and default padding it not accounted for anywhere else
 * here could unbalance flex centering (suspected after a 2026-09-21
 * hardware test: Home's "Sleep now" button, flex-centered, sat with its
 * right edge almost against the panel border and roughly a third of its
 * own width of margin on the left instead of even margins). */
static lv_obj_t *make_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_size(screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    return screen;
}

static void enter_sleep_from_idle_or_menu(const char *reason)
{
    if (!power_mgr_claim_terminal_action()) {
        return;   /* another path already won the race */
    }
    ESP_LOGI(TAG, "entering sleep (%s)", reason);
    board_sleep_screen_show();   /* splash.cpp — calls ui_nav_suspend_lvgl_for_sleep() */
    power_mgr_shutdown();        /* never returns on success */
}

/* -----------------------------------------------------------------------
 * HOME
 * ----------------------------------------------------------------------- */
static void open_settings_cb(lv_event_t *e)
{
    (void)e;
    switch_screen(Screen::Settings);
}

static void sleep_now_cb(lv_event_t *e)
{
    (void)e;
    enter_sleep_from_idle_or_menu("menu");
}

static lv_obj_t *build_home(void)
{
    lv_group_t *group = lv_port_indev_new_group();
    s_groups[static_cast<int>(Screen::Home)] = group;

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
    s_battery_labels[static_cast<int>(Screen::Home)] = make_battery_label(bar);

    add_divider_below(screen, bar);

    /* Icon-only, not the 140px make_button() default meant for text labels
     * like "< Back" (2026-09-21 request: "peut etre plus petit pas besoin
     * qu'il soit si large"). */
    lv_obj_t *settings_btn = make_button(screen, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_min_width(settings_btn, kMinTouchTarget, 0);
    lv_obj_align(settings_btn, LV_ALIGN_TOP_LEFT, 16, kBackTopMargin);
    lv_obj_add_event_cb(settings_btn, open_settings_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(settings_btn, group);

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

/* -----------------------------------------------------------------------
 * SETTINGS (menu of sub-screens)
 * ----------------------------------------------------------------------- */
static void open_screen_cb(lv_event_t *e)
{
    Screen target = static_cast<Screen>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    switch_screen(target);
}

/* 2x kExtClickMargin (2026-09-22 bug report: "j'appuis sur Display, une
 * fois sur deux c'est About qui s'ouvre" -- already half-diagnosed by the
 * very next log block below, added the day before for a near-identical
 * "Touch Diagnostic opened About instead" report). Root cause: each menu
 * button's touch hit area extends kExtClickMargin=16px past its own
 * visual box on every side (lv_obj_set_ext_click_area(), make_button()) --
 * with only make_content()'s default 16px row gap between two adjacent,
 * full-width buttons, their extended hit areas overlapped by 16px right
 * in the middle of that gap, so a tap landing there could resolve to
 * EITHER button. Doubling the gap to 32px (2x the 16px each side
 * contributes) is exactly the point past which they can no longer
 * overlap at all. Menu buttons only -- other screens' rows have their
 * interactive control on one side, not a full-width button repeated
 * top-to-bottom, so this specific overlap doesn't arise there. */
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

static lv_obj_t *build_settings(void)
{
    lv_group_t *group = lv_port_indev_new_group();
    s_groups[static_cast<int>(Screen::Settings)] = group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, "Settings", Screen::Settings, Screen::Home, group);
    lv_obj_t *content = make_content(screen);
    lv_obj_set_style_pad_row(content, kMenuButtonRowGap, 0);

    lv_obj_t *b1 = add_menu_button(content, "Controls", Screen::SettingsControls, group);
    lv_obj_t *b2 = add_menu_button(content, "Display", Screen::SettingsDisplay, group);
    lv_obj_t *b3 = add_menu_button(content, "About", Screen::SettingsAbout, group);
    lv_obj_t *b4 = add_menu_button(content, "Touch Calibration", Screen::TouchDiag, group);

    /* Diagnostic-only (2026-09-21: "Touch Diagnostic" tap opened "About"
     * instead; root cause found and fixed 2026-09-22, see
     * kMenuButtonRowGap's comment) -- kept to confirm the real gap between
     * buttons at runtime, since it still logs their actual y/y2. */
    lv_obj_update_layout(screen);
    lv_obj_t *btns[] = {b1, b2, b3, b4};
    const char *names[] = {"Controls", "Display", "About", "TouchDiag"};
    for (int i = 0; i < 4; i++) {
        ESP_LOGI(TAG, "settings menu %s=(%d,%d,%dx%d) y2=%d", names[i],
                 (int)lv_obj_get_x(btns[i]), (int)lv_obj_get_y(btns[i]),
                 (int)lv_obj_get_width(btns[i]), (int)lv_obj_get_height(btns[i]),
                 (int)lv_obj_get_y2(btns[i]));
    }

    return screen;
}

/* -----------------------------------------------------------------------
 * SETTINGS > CONTROLS & CALIBRATION
 * ----------------------------------------------------------------------- */
static constexpr uint32_t kAutoSleepPresets[] = {0, 30, 60, 120};
static const char *const kAutoSleepLabels[] = {"Off", "30s", "1min", "2min"};
static constexpr int kAutoSleepPresetCount = 4;

static int nearest_preset_index(const uint32_t *presets, int count, uint32_t value)
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

static void power_confirm_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    settings_store_set_power_short_confirm(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static lv_obj_t *s_idle_value_label;
static int s_idle_index;

static void set_idle_index(int index)
{
    s_idle_index = (index + kAutoSleepPresetCount) % kAutoSleepPresetCount;
    settings_store_set_idle_timeout_s(kAutoSleepPresets[s_idle_index]);
    lv_label_set_text(s_idle_value_label, kAutoSleepLabels[s_idle_index]);
}

static void idle_prev_cb(lv_event_t *e) { (void)e; set_idle_index(s_idle_index - 1); }
static void idle_next_cb(lv_event_t *e) { (void)e; set_idle_index(s_idle_index + 1); }

static lv_obj_t *build_settings_controls(void)
{
    lv_group_t *group = lv_port_indev_new_group();
    s_groups[static_cast<int>(Screen::SettingsControls)] = group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, "Controls", Screen::SettingsControls, Screen::Settings, group);
    lv_obj_t *content = make_content(screen);

    make_round_toggle_row(content, group, "Power short press",
                          settings_store_get_power_short_confirm(), power_confirm_switch_cb);

    make_stepper_row(content, group, "Auto-sleep", idle_prev_cb, idle_next_cb, &s_idle_value_label);
    s_idle_index = nearest_preset_index(kAutoSleepPresets, kAutoSleepPresetCount,
                                        settings_store_get_idle_timeout_s());
    lv_label_set_text(s_idle_value_label, kAutoSleepLabels[s_idle_index]);

    return screen;
}

/* -----------------------------------------------------------------------
 * SETTINGS > DISPLAY (frontlight)
 * ----------------------------------------------------------------------- */
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

static void apply_frontlight_from_settings(void)
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

static void frontlight_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
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

static void set_color_index(int index)
{
    s_color_index = (index + kColorMixPresetCount) % kColorMixPresetCount;
    settings_store_set_frontlight_color(kColorMixPresets[s_color_index]);
    apply_frontlight_from_settings();
    lv_label_set_text(s_color_value_label, kColorMixLabels[s_color_index]);
}

static void color_prev_cb(lv_event_t *e) { (void)e; set_color_index(s_color_index - 1); }
static void color_next_cb(lv_event_t *e) { (void)e; set_color_index(s_color_index + 1); }

static void set_intensity_value(int value)
{
    s_intensity_value = value < 0 ? 0 : (value > 100 ? 100 : value);
    settings_store_set_frontlight_intensity(static_cast<uint32_t>(s_intensity_value));
    apply_frontlight_from_settings();
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", s_intensity_value);
    lv_label_set_text(s_intensity_value_label, buf);
}

static void intensity_prev_cb(lv_event_t *e) { (void)e; set_intensity_value(s_intensity_value - kIntensityStep); }
static void intensity_next_cb(lv_event_t *e) { (void)e; set_intensity_value(s_intensity_value + kIntensityStep); }

static lv_obj_t *build_settings_display(void)
{
    lv_group_t *group = lv_port_indev_new_group();
    s_groups[static_cast<int>(Screen::SettingsDisplay)] = group;
    s_display_group = group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, "Display", Screen::SettingsDisplay, Screen::Settings, group);
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

/* -----------------------------------------------------------------------
 * SETTINGS > ABOUT
 * ----------------------------------------------------------------------- */
static lv_obj_t *build_settings_about(void)
{
    lv_group_t *group = lv_port_indev_new_group();
    s_groups[static_cast<int>(Screen::SettingsAbout)] = group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, "About", Screen::SettingsAbout, Screen::Settings, group);
    lv_obj_t *content = make_content(screen);

    lv_obj_t *label = lv_label_create(content);
    const esp_app_desc_t *app_desc = esp_app_get_description();
    /* MALLOC_CAP_DEFAULT covers every heap region (internal + the 8MB
     * PSRAM) LVGL/the app could allocate from -- the combined figure a
     * user asking "how much RAM" (2026-09-21 request) actually means,
     * not just one region. */
    size_t ram_total = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    size_t ram_free = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    char buf[220];
    snprintf(buf, sizeof(buf), "MySafeFob\nboard: x4pro\nversion: %s\nbuilt: %s %s\nRAM: %u/%u KB free",
             app_desc->version, app_desc->date, app_desc->time,
             static_cast<unsigned>(ram_free / 1024), static_cast<unsigned>(ram_total / 1024));
    lv_label_set_text(label, buf);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    return screen;
}

/* -----------------------------------------------------------------------
 * SETTINGS > TOUCH DIAGNOSTIC — live readout, only while this screen is
 * loaded (a periodic lv_timer here is the point of this one screen; it's
 * deliberately NOT done on any other screen, which stay event-driven).
 * ----------------------------------------------------------------------- */
static lv_obj_t *s_touch_diag_label;
static lv_timer_t *s_touch_diag_timer;

static void touch_diag_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    touch_point_t tp = lv_port_indev_get_last_touch();
    char buf[128];
    snprintf(buf, sizeof(buf), "pressed: %s\nx=%d y=%d\nraw_x=%d raw_y=%d",
              tp.pressed ? "yes" : "no", tp.x, tp.y, tp.raw_x, tp.raw_y);

    /* lv_label_set_text() invalidates unconditionally, changed or not --
     * this timer used to do that every 300ms regardless, flushing the
     * whole e-paper screen on a fixed clock even with nobody touching it
     * (found on hardware 2026-09-21: "l'écran clignote de temps en temps
     * alors que personne ne le touche"). Only actually set/redraw when
     * the text would genuinely differ. */
    static char s_last_shown[128] = "";
    if (strcmp(buf, s_last_shown) != 0) {
        strcpy(s_last_shown, buf);
        lv_label_set_text(s_touch_diag_label, buf);
    }
}

static void touch_diag_show_cb(lv_event_t *e)
{
    (void)e;
    if (!s_touch_diag_timer) {
        s_touch_diag_timer = lv_timer_create(touch_diag_timer_cb, 300, nullptr);
    }
}

static void touch_diag_hide_cb(lv_event_t *e)
{
    (void)e;
    if (s_touch_diag_timer) {
        lv_timer_delete(s_touch_diag_timer);
        s_touch_diag_timer = nullptr;
    }
}

/* Calibration ticks (2026-09-21, user request): docs/touch-calibration-notes.md
 * §9/§10 have an open question -- exactly how wide is the capacitive dead
 * zone near the panel edges -- that was so far only ever answered with
 * rough, off-the-cuff estimates, never a real measurement. A column of
 * marks at known absolute y values (independent of any flex/theme
 * layout -- lv_obj_set_pos() straight on the screen, same technique as
 * the 5-cross test screen that found the I1 palette-offset bug) lets
 * whoever's testing tap next to each one and read off, from the live
 * readout below, the smallest y that still registers a press. Placed at
 * x=380+ to stay clear of the header's Back button/title. */
static void add_calibration_ticks(lv_obj_t *screen)
{
    constexpr int kTickCount = 20;
    constexpr int kTickStep = 40;
    constexpr int kTickFirstY = 10;
    for (int i = 0; i < kTickCount; i++) {
        int y = kTickFirstY + i * kTickStep;

        lv_obj_t *tick = lv_obj_create(screen);
        lv_obj_remove_style_all(tick);
        lv_obj_set_style_bg_color(tick, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
        lv_obj_set_size(tick, 24, 4);
        lv_obj_set_pos(tick, 380, y);

        char buf[8];
        snprintf(buf, sizeof(buf), "%d", y);
        lv_obj_t *label = lv_label_create(screen);
        lv_label_set_text(label, buf);
        lv_obj_set_pos(label, 408, y - 10);
    }
}

/* One cross (two thin crossing bars) at an absolute screen position,
 * lv_obj_set_pos() straight on the screen like add_calibration_ticks()
 * above -- independent of any flex/theme layout. Originally a throwaway
 * diagnostic screen used to find the I1 palette-offset rendering bug
 * (2026-09-21); brought back here (user request) as a permanent visual
 * reference for the 4 corners + center of the panel. */
static void add_cross(lv_obj_t *screen, int cx, int cy)
{
    constexpr int kArmLen = 24;
    constexpr int kArmThickness = 4;

    lv_obj_t *h = lv_obj_create(screen);
    lv_obj_remove_style_all(h);
    lv_obj_set_style_bg_color(h, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_set_size(h, kArmLen, kArmThickness);
    lv_obj_set_pos(h, cx - kArmLen / 2, cy - kArmThickness / 2);

    lv_obj_t *v = lv_obj_create(screen);
    lv_obj_remove_style_all(v);
    lv_obj_set_style_bg_color(v, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
    lv_obj_set_size(v, kArmThickness, kArmLen);
    lv_obj_set_pos(v, cx - kArmThickness / 2, cy - kArmLen / 2);
}

/* 4 corners + center, margined off the panel edges (SCREEN_WIDTH/HEIGHT,
 * hw_config.h) so each cross's arms stay fully visible. */
static void add_calibration_crosses(lv_obj_t *screen)
{
    constexpr int kMargin = 30;
    add_cross(screen, kMargin, kMargin);
    add_cross(screen, SCREEN_WIDTH - kMargin, kMargin);
    add_cross(screen, kMargin, SCREEN_HEIGHT - kMargin);
    add_cross(screen, SCREEN_WIDTH - kMargin, SCREEN_HEIGHT - kMargin);
    add_cross(screen, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
}

static lv_obj_t *build_touch_diag(void)
{
    lv_group_t *group = lv_port_indev_new_group();
    s_groups[static_cast<int>(Screen::TouchDiag)] = group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, "Calibration", Screen::TouchDiag, Screen::Settings, group);
    lv_obj_t *content = make_content(screen);

    s_touch_diag_label = lv_label_create(content);
    lv_label_set_text(s_touch_diag_label, "pressed: no");
    lv_obj_set_style_text_align(s_touch_diag_label, LV_TEXT_ALIGN_CENTER, 0);

    add_calibration_ticks(screen);
    add_calibration_crosses(screen);

    lv_obj_add_event_cb(screen, touch_diag_show_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    lv_obj_add_event_cb(screen, touch_diag_hide_cb, LV_EVENT_SCREEN_UNLOADED, nullptr);

    return screen;
}

/* -----------------------------------------------------------------------
 * DEEP SLEEP — the sleep screen itself is no longer an LVGL screen
 * (2026-09-22 request: replace it outright with resources/sleep.png,
 * tools/gen_sleep.py -> sleep_bitmap.h). splash.cpp's
 * board_sleep_screen_show() draws that bitmap directly into eink.c's
 * native framebuffer, the same pre-LVGL path board_splash_show() already
 * uses for the boot splash -- simpler than building an LVGL I1 image
 * asset, and this codebase already learned the hard way (the I1
 * palette-offset bug) how easy that format is to get subtly wrong.
 *
 * ui_nav_suspend_lvgl_for_sleep() only keeps the thread-safety half of
 * what used to be ui_nav_show_sleep_screen(): board_sleep_screen_show()
 * can be called from either board_ui_nav_task itself (Home's "Sleep now",
 * the ADR-012 idle timeout) or main.c's power_button_task (a different
 * task, on a Power long-press) -- this flag is what stops
 * board_ui_nav_task's own loop from still pumping lv_timer_handler() while
 * splash.cpp blits and flushes the sleep bitmap straight into the shared
 * framebuffer right after. */
extern "C" void ui_nav_suspend_lvgl_for_sleep(void)
{
    s_lvgl_suspended.store(true, std::memory_order_relaxed);
}

/* -----------------------------------------------------------------------
 * Task entry point (main.c: xTaskCreate(board_ui_nav_task, ...)).
 * ----------------------------------------------------------------------- */
static void build_screens(void)
{
    s_screens[static_cast<int>(Screen::Home)] = build_home();
    s_screens[static_cast<int>(Screen::Settings)] = build_settings();
    s_screens[static_cast<int>(Screen::SettingsControls)] = build_settings_controls();
    s_screens[static_cast<int>(Screen::SettingsDisplay)] = build_settings_display();
    s_screens[static_cast<int>(Screen::SettingsAbout)] = build_settings_about();
    s_screens[static_cast<int>(Screen::TouchDiag)] = build_touch_diag();
}

static void check_idle_timeout(void)
{
    uint32_t timeout_s = settings_store_get_idle_timeout_s();
    if (timeout_s == 0) {
        return;   /* disabled, settings_store.h's own dev-default */
    }
    int64_t now = esp_timer_get_time();
    int64_t last = s_last_activity_us.load(std::memory_order_relaxed);
    if (now - last >= static_cast<int64_t>(timeout_s) * 1000000) {
        enter_sleep_from_idle_or_menu("idle timeout");
    }
}

void board_ui_nav_task(void *arg)
{
    (void)arg;

    board_activity_notify();   /* don't start already "idle since boot" */

    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    /* frontlight_init() was never called anywhere in this app (2026-09-22
     * bug report: "au demarrage le frontlight ne marche pas non plus meme
     * si actif") -- every frontlight_apply() call since (here and from the
     * Display screen's toggle/steppers) was setting duty on an LEDC timer/
     * channel pair that had never actually been configured, so it silently
     * had no effect on the physical pins. Must run once, before the first
     * apply_frontlight_from_settings() below. */
    frontlight_init();
    apply_frontlight_from_settings();

    build_screens();
    switch_screen(Screen::Home);

    uint8_t initial_soc = 0;
    bool initial_charging = false;
    battery_read(&initial_soc, &initial_charging);
    lv_timer_create(battery_timer_cb, initial_charging ? 10000 : 30000, nullptr);

    while (1) {
        if (!s_lvgl_suspended.load(std::memory_order_relaxed)) {
            lv_timer_handler();

            if (s_power_confirm_pending.exchange(false, std::memory_order_relaxed)) {
                board_activity_notify();
                /* lv_group_send_data(group, LV_KEY_ENTER) only fires a raw
                 * LV_EVENT_KEY on the focused widget -- turning that into
                 * an actual click is normally lv_indev.c's job, done for a
                 * real KEYPAD/ENCODER indev's own press/release state
                 * machine, which this bypasses entirely (found on hardware
                 * 2026-09-21: Power-short-press "confirm" logged but never
                 * activated the focused button). Firing LV_EVENT_CLICKED
                 * directly is the simple, correct equivalent for a plain
                 * button/menu entry, which is all this confirm pulse is
                 * meant to activate. */
                lv_obj_t *focused = lv_group_get_focused(s_groups[static_cast<int>(s_screen)]);
                if (focused) {
                    lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
                }
            }

            if (s_focus_prev_pending.exchange(false, std::memory_order_relaxed)) {
                lv_port_disp_request_full_refresh();
                lv_group_focus_prev(s_groups[static_cast<int>(s_screen)]);
            }
            if (s_focus_next_pending.exchange(false, std::memory_order_relaxed)) {
                lv_port_disp_request_full_refresh();
                lv_group_focus_next(s_groups[static_cast<int>(s_screen)]);
            }

            check_idle_timeout();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
