/* 
 Project: MySafeFob  splash.h
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
 * @file splash.h
 * @brief MySafeFob App — static welcome screen (board x4pro).
 *
 * Displays a static page on the e-ink at app boot, so you know where
 * you are when leaving the factory (user request 2026-09-14).
 * First slice of Phase 8c: the e-ink drivers ported from the factory
 * (proven) will serve as the base for the app's full BSP.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Init rails + e-ink, displays the static page, powers off the
 *        controller (the image persists at zero power).
 *
 * Blocking ~3-4s (full refresh GC). Call once at boot.
 */
void board_splash_show(void);

/**
 * @brief Draw the deep-sleep screen (static, then controller POF).
 *
 * Shown right before power_mgr_shutdown(): the panel is bistable, so the
 * "device asleep" image persists at zero power until the next wake.
 * Deliberately does NOT mention the factory-rescue combo (DECISIONS §16).
 *
 * Blocking ~3-4 s (full refresh GC). Never returns on failure either —
 * the caller proceeds to deep sleep regardless (a missing image must not
 * prevent sleeping).
 */
void board_sleep_screen_show(void);

#ifdef __cplusplus
}
#endif
