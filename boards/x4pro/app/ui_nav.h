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
 * @brief Injects one confirm pulse (fires LV_EVENT_CLICKED on the
 *        currently focused widget, see board_ui_nav_task's loop) — the
 *        same pulse for two unrelated physical gestures that both mean
 *        "confirm, not a positional tap":
 *         - main.c's power_button_task, on a Power release shorter than
 *           MSF_POWER_LONG_MS, gated behind the "Power short press =
 *           Select" setting (settings_store.h);
 *         - lv_port_indev.c's input_sampler_task, on a touch-Home press
 *           edge (the GT911 digitizer's dedicated capacitive zone below
 *           the visible e-ink glass, per touch.c — its x/y calibrates
 *           into the normal on-screen range, which would make LVGL treat
 *           it as a tap on whatever's drawn there; this pulse is used
 *           instead of feeding that touch to the pointer indev at all).
 *        Neither Power nor touch-Home otherwise joins this loop's own
 *        screen-level input handling — this is a single pulse consumed
 *        by the next loop iteration.
 */
void board_ui_nav_power_confirm(void);

/**
 * @brief Queues a Left/Right group-focus step, consumed by
 *        board_ui_nav_task's own loop. LVGL is not thread-safe -- calling
 *        lv_group_focus_prev/next() directly from lv_port_indev.c's
 *        input_sampler_task (a different FreeRTOS task) crashed on
 *        hardware 2026-09-21 (two tasks touching LVGL's invalidated-area
 *        list at once). Same pattern as board_ui_nav_power_confirm(): the
 *        sampler task only ever raises a flag, board_ui_nav_task is the
 *        sole task that ever calls into LVGL.
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
