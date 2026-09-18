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
 * @brief Injects one confirm pulse into the nav loop's InteractionBuffer,
 *        equivalent to a touch-Home tap. Called by main.c's
 *        power_button_task on a Power release shorter than
 *        MSF_POWER_LONG_MS — gated there behind the "Power short press =
 *        Select" setting (settings_store.h). Power still never becomes a
 *        screen-level actor: this is a single pulse consumed by the next
 *        loop iteration, not GPIO3 joining this loop's own input reads.
 */
void board_ui_nav_power_confirm(void);

#ifdef __cplusplus
}
#endif
