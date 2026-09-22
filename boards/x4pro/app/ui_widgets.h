/*
 Project: MySafeFob  ui_widgets.h
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
 * @file ui_widgets.h
 * @brief MySafeFob App — internal (component-private), generic LVGL
 *        builders + shared style constants reused by every ui_screen_*.cpp.
 *        No screen-specific logic here; see ui_screens.h for the
 *        per-screen build_xxx() contract these are used from.
 */
#pragma once

#include "lvgl.h"
#include "ui_screens.h"

#ifdef __cplusplus

/* 2026-09-21 hardware test: only the explicitly-sized "Sleep now" button
 * responded reliably to touch; content-sized buttons (their tap area
 * exactly the text's small bounding box) mostly missed. */
constexpr int32_t kMinTouchTarget = 56;

/* Mono theme's own default border (1px, lv_theme_mono.c's BORDER_W_NORMAL)
 * barely shows up on this panel (2026-09-21: "il manque les contours des
 * boutons"). Used explicitly on every button/toggle's NORMAL state, and
 * pinned to the SAME value for PRESSED (see add_to_group()) so a press
 * never causes a visual delta LVGL would otherwise invalidate the whole
 * screen for (the "toggle forces a full e-paper flush" lesson,
 * docs/ROADMAP.md). */
constexpr int32_t kButtonBorderWidth = 3;

/* Extends the CLICKABLE area beyond an object's visible box, without
 * changing its visual size/layout (lv_obj_set_ext_click_area()). Found on
 * hardware 2026-09-21: taps landing just past a tight target's edge were
 * correctly ignored by LVGL -- not a bug, just normal capacitive-touch
 * precision. A few extra pixels of forgiveness costs nothing here. */
constexpr int32_t kExtClickMargin = 16;

/* Rounded corners on every button (2026-09-21, user request) -- mono
 * theme's own default is square. */
constexpr int32_t kButtonRadius = 12;

/* Slack reserved around a row's/controls container's own edges so a
 * focused child's outline ring (4px width + 3px pad = 7px past its own
 * box, see add_to_group()) has room to actually render (2026-09-22 bug:
 * LVGL clips a child's drawing to its DIRECT parent's own box, and every
 * row/controls container here is sized with LV_SIZE_CONTENT to exactly
 * wrap its children, leaving zero room by construction). */
constexpr int32_t kFocusOutlineSlack = 10;

/* Header layout (add_back_header()/build_home()): the info row/divider sit
 * near the true top of the panel (purely informational, nothing to touch
 * there); kBackTopMargin clears docs/touch-calibration-notes.md §10's
 * measured ~87px dead-zone boundary for the one interactive element up
 * there ("< Back" / the Settings icon). kHeaderReservedPct sizes
 * make_content()'s top space to match. */
constexpr int32_t kHeaderInfoHeight = 65;
constexpr int32_t kBackTopMargin = 88;
constexpr int32_t kHeaderReservedPct = 20;
constexpr int32_t kHeaderContentPadTop = 8;

/* Adds `obj` to `group`, styling focus with an outline ring (2026-09-21
 * user request, matches the round toggle's own focus style) and pinning
 * its PRESSED border to the same width as NORMAL (kButtonBorderWidth) so a
 * press never causes a redraw-worthy visual delta. */
void add_to_group(lv_obj_t *obj, lv_group_t *group);

/** @brief Text button, min touch target + rounded corners + extended hit area. */
lv_obj_t *make_button(lv_obj_t *parent, const char *label_text);

/** @brief Blank, non-scrollable, white, SCREEN_WIDTHxSCREEN_HEIGHT screen. */
lv_obj_t *make_screen(void);

/** @brief The fixed content area every screen's body sits in, below the header. */
lv_obj_t *make_content(lv_obj_t *screen);

/** @brief A label + control row (Settings rows), label on the left. */
lv_obj_t *make_row(lv_obj_t *parent, const char *label_text);

/**
 * @brief label_text on the left + a round toggle indicator on the right,
 *        checkable via LV_OBJ_FLAG_CHECKABLE (not lv_switch/lv_checkbox --
 *        see ui_widgets.cpp for why). Fires value_changed_cb on
 *        LV_EVENT_VALUE_CHANGED, on a real tap release or a synthetic
 *        confirm pulse alike.
 */
lv_obj_t *make_round_toggle_row(lv_obj_t *parent, lv_group_t *group, const char *label_text,
                                bool initial_checked, lv_event_cb_t value_changed_cb);

/**
 * @brief label_text row with [-] value [+] on the right. *value_label_out
 *        receives the middle label, for prev_cb/next_cb to update.
 */
lv_obj_t *make_stepper_row(lv_obj_t *parent, lv_group_t *group, const char *label_text,
                           lv_event_cb_t prev_cb, lv_event_cb_t next_cb, lv_obj_t **value_label_out);

/** @brief Full-width divider line directly below `above`. */
lv_obj_t *add_divider_below(lv_obj_t *screen, lv_obj_t *above);

/**
 * @brief Every non-Home screen's header: title (top-left) + battery
 *        (top-right, returned via *battery_label_out) near the true top of
 *        the panel, a divider below spanning the full width, then a
 *        "< Back" button (added to `group`) returning to back_target.
 */
lv_obj_t *add_back_header(lv_obj_t *screen, const char *title, Screen back_target, lv_group_t *group,
                          lv_obj_t **battery_label_out);

/** @brief Creates a battery/charge label and populates it immediately. */
lv_obj_t *make_battery_label(lv_obj_t *parent);

/**
 * @brief Re-reads battery.h's gauge and updates `label`'s text (no-op if
 *        `label` is NULL — still returns whether the pack is charging).
 *        See ui_widgets.cpp for the two-glyph/no-clip rationale.
 */
bool refresh_battery_label(lv_obj_t *label);

/**
 * @brief Index into `presets` (length `count`) whose value is closest to
 *        `value` — shared by every Settings row backed by a small fixed
 *        set of presets (auto-sleep, frontlight color mix, ...).
 */
int nearest_preset_index(const uint32_t *presets, int count, uint32_t value);

/**
 * @brief Runs `fn(user_data)` on the NEXT lv_timer_handler() pass instead of
 *        synchronously, right now, inside the current event's call stack
 *        (thin wrapper over lv_async_call(), falling back to calling `fn`
 *        immediately if LVGL can't allocate the one-shot timer -- state
 *        still gets applied, just without the deferral guarantee).
 *
 *        Standing rule (2026-09-22, after a hard-to-reproduce bug):
 *        EVERY click/value-changed callback in this app's screens defers
 *        its actual work through this, no exceptions decided case-by-case.
 *        The concrete incident that prompted this: Settings > Display's
 *        Frontlight toggle mutates its own lv_group_t's membership
 *        (lv_group_add_obj()/lv_group_remove_obj(), showing/hiding the
 *        Color/Intensity rows) from directly inside the event callback
 *        that same group's indev click triggered -- confirmed on hardware
 *        to occasionally drop the click's effect entirely under a rapid
 *        second tap, most likely LVGL's own indev processing (particularly
 *        with lv_port_indev.c's touch-edge queue replaying several taps
 *        back-to-back via `continue_reading`) not expecting the group
 *        it's mid-dispatch on to be restructured underneath it. No other
 *        control happened to do that kind of structural mutation, so no
 *        other control had hit this specific failure yet -- but the next
 *        one to touch object/group structure from its own click handler
 *        would, and nothing about writing that code would have hinted at
 *        the risk. Deferring universally removes the whole hazard class
 *        instead of relying on each new control's author to recognize it.
 *        Cost: one lv_timer_handler() pass of latency (typically <20ms)
 *        before the callback's effects apply -- negligible against this
 *        e-ink panel's own 700-1500ms flush times.
 *
 *        The one deliberate exception is ui_widgets.cpp's own
 *        toggle_confirm_click_cb(): it reads lv_indev_get_act() to tell a
 *        real touch's own CLICKED event apart from a synthetic confirm
 *        pulse, which is only meaningful synchronously, during the actual
 *        indev dispatch that produced it -- deferring it would make that
 *        check always see "no active indev" and misbehave for real touches
 *        too.
 */
void ui_defer(lv_async_cb_t fn, void *user_data);

#endif /* __cplusplus */
