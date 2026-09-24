/* 
 Project: MySafeFob  ui_nav.h
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
 * @file ui_nav.h
 * @brief MySafeFob App — interactive menu (task 8.4, minimal slice).
 *
 * Left/Right (buttons.c) move focus, touch-Home (touch.c) confirms —
 * exact pattern proven in references/test_apps/freeinkui-poc/main/main.cpp
 * (Frame/InteractionBuffer double-draw-per-input-cycle: draw once to
 * register hit-rects + route the input, redraw with the now-updated
 * focus/active state, only THAT one gets flushed to the panel).
 *
 * Replaces the static board_ready_show() screen after the boot splash.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The interactive menu's main loop. Never returns (deep sleep is
 *        entered from inside it, either on "Sleep now" or on the
 *        ADR-012 idle timeout — both never return either on success).
 *        Intended to run as its own FreeRTOS task (xTaskCreate).
 */
void board_ui_nav_task(void *arg);

/**
 * @brief Resets the ADR-012 idle-activity timer. Call on any input this
 *        loop doesn't itself see — REPL commands (serial activity) and
 *        Power button presses (main.c's power_button_task owns GPIO3,
 *        not this loop).
 */
void board_activity_notify(void);

/**
 * @brief Queues one confirm pulse (fires LV_EVENT_CLICKED on the
 *        currently focused widget, see board_ui_nav_task's loop) — the
 *        same pulse for two unrelated physical gestures that both mean
 *        "confirm, not a positional tap":
 *         - main.c's power_button_task, on a Power release shorter than
 *           MSF_POWER_LONG_MS, gated behind the "Power short press =
 *           Select" setting (settings_store.h) -- pass false;
 *         - lv_port_indev.c's input_sampler_task, on a touch-Home press
 *           edge (the GT911 digitizer's dedicated capacitive zone below
 *           the visible e-ink glass, per touch.c — its x/y calibrates
 *           into the normal on-screen range, which would make LVGL treat
 *           it as a tap on whatever's drawn there; this pulse is used
 *           instead of feeding that touch to the pointer indev at all) --
 *           pass true.
 *        Neither Power nor touch-Home otherwise joins this loop's own
 *        screen-level input handling. A COUNTER, not a single flag
 *        (2026-09-22, same reasoning as the touch-edge queue fix in
 *        lv_port_indev.c): two calls landing while board_ui_nav_task is
 *        busy (e.g. mid-flush) both still fire their own pulse once it
 *        catches up, instead of the second one silently overwriting/
 *        losing the first. Kept as two separate counters internally
 *        (button vs. touch-Home) precisely so board_ui_nav_suppress_touch_confirm()
 *        below can drop only one of the two origins.
 * @param from_touch_home True for the touch-Home path, false for the
 *        Power-button path -- see board_ui_nav_suppress_touch_confirm().
 */
void board_ui_nav_power_confirm(bool from_touch_home);

/**
 * @brief While true, confirm pulses queued with from_touch_home=true are
 *        silently dropped instead of activating the focused widget --
 *        Power-button confirms (from_touch_home=false) are NEVER affected,
 *        the physical button stays a reliable way to confirm/escape no
 *        matter what a screen's own touch handling is doing.
 *
 *        2026-09-23: Settings > Touch Calibration turns this on for as
 *        long as it's the active screen. Its own guided sequence reads raw
 *        taps anywhere on a content area that spans most of the panel --
 *        confirmed on hardware that a tap near the touch-Home pad's own
 *        capacitive zone (touch.c: raw_x<70 && raw_y in [660,720]) can
 *        alias into it through touch_lerp()'s fixed mapping, which used to
 *        silently activate whatever was focused (deliberately "< Back",
 *        the screen's own escape hatch) and wipe the run in progress. This
 *        drops that specific signal cleanly at the source, by ORIGIN, in
 *        place of only ever trying to keep every calibration target
 *        geometrically far enough from the risky corner to *probably*
 *        avoid triggering it.
 *
 *        Also consulted directly (via board_ui_nav_is_touch_confirm_suppressed()
 *        below) by "< Back"'s own click handler (ui_widgets.cpp) and
 *        Touch Calibration's "Restart" button, to reject a DIRECT touch
 *        tap landing on either of them too while a screen wants this
 *        escape hatch to be physical-button-only -- not just the
 *        touch-Home confirm path this flag was first added for. Either
 *        button staying reachable via Left/Right + Power is unaffected;
 *        only a touch-originated activation (lv_indev_get_act() != NULL)
 *        is rejected while suppressed.
 */
void board_ui_nav_suppress_touch_confirm(bool suppress);

/**
 * @brief Read-only query for board_ui_nav_suppress_touch_confirm()'s
 *        current state -- see its own doc comment for who calls this and
 *        why.
 */
bool board_ui_nav_is_touch_confirm_suppressed(void);

/**
 * @brief Queues one Left/Right group-focus step, consumed by
 *        board_ui_nav_task's own loop. LVGL is not thread-safe -- calling
 *        lv_group_focus_prev/next() directly from lv_port_indev.c's
 *        input_sampler_task (a different FreeRTOS task) crashed on
 *        hardware 2026-09-21 (two tasks touching LVGL's invalidated-area
 *        list at once). Same pattern as board_ui_nav_power_confirm(): the
 *        sampler task only ever raises a counter, board_ui_nav_task is the
 *        sole task that ever calls into LVGL -- and it's a counter, not a
 *        flag, for the same reason: two quick presses of the same button
 *        while the loop is busy (mid-flush) both still move focus once it
 *        catches up, rather than the second press coalescing into the
 *        first and silently costing the user one step.
 */
void board_ui_nav_focus_prev(void);
void board_ui_nav_focus_next(void);

/**
 * @brief Stops board_ui_nav_task's own loop from pumping lv_timer_handler()
 *        any further, so it can't touch LVGL concurrently with whatever
 *        happens right after this call (LVGL is not thread-safe). The
 *        deep sleep screen itself is drawn without LVGL at all: splash.cpp's
 *        board_sleep_screen_show() (which calls this first) blits
 *        resources/sleep.png (tools/gen_sleep.py -> sleep_bitmap.h)
 *        straight into eink.c's native framebuffer, the same pre-LVGL path
 *        board_splash_show() already uses for the boot splash, then owns
 *        eink_power_off() and the rail-holding sequence right after.
 */
void ui_nav_suspend_lvgl_for_sleep(void);

#ifdef __cplusplus
}
#endif
