/*
 Project: MySafeFob  settings_store.h
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
 * @file settings_store.h
 * @brief MySafeFob App — persisted UI preferences (NVS-backed).
 *
 * First NVS-backed persistence in this codebase (everything else so far
 * only calls the bare nvs_flash_init() in main.c). Table-driven
 * internally (settings_defs.inc, one line per setting — see that file's
 * doc comment for the pattern's origin) so adding a setting never needs
 * a hand-written NVS read/write pair; mutex-protected (settings_store.c)
 * since multiple tasks — ui_nav, power_button_task, the REPL — can call
 * these. Deliberately scoped to non-secret UI preferences only:
 * PIN/keystore-adjacent settings belong to secret_store once it exists,
 * not here.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opens the NVS namespace. Call once, after nvs_flash_init(), and
 *        before any task that might read a setting (main.c's app_main()
 *        starts board_ui_nav_task right after this call for exactly that
 *        reason).
 */
esp_err_t settings_store_init(void);

/**
 * @brief "Power short press = Select" (UI-SPECS.md §2.12): a quick
 *        press-and-release of Power (main.c, power_button_task) acts as
 *        an extra confirm pulse, equivalent to touch-Home. Defaults to
 *        true (on) — a reliable confirm path independent of the touch
 *        panel while touch calibration is still being validated
 *        (docs/touch-calibration-notes.md).
 */
bool settings_store_get_power_short_confirm(void);
void settings_store_set_power_short_confirm(bool on);

/**
 * @brief ADR-012 idle-activity timeout (seconds) before the device
 *        auto-sleeps on its own (no button/touch/serial input) —
 *        UI-SPECS.md §2.13's "Auto-sleep after inactivity" field
 *        (SETTINGS_SECURITY, not yet wired to a screen — this is the
 *        underlying persisted value, ready for when it is).
 *        **0 = disabled** (never auto-sleeps from inactivity alone).
 *        Default: **0**, deliberately, for development — ADR-012's
 *        originally-validated 45s default is what a shipped device
 *        should use, but constantly falling asleep mid-test is not what
 *        this codebase is being exercised for right now.
 */
uint32_t settings_store_get_idle_timeout_s(void);
void settings_store_set_idle_timeout_s(uint32_t seconds);

/**
 * @brief Frontlight preferences (UI-SPECS.md §2.14, SETTINGS_DISPLAY).
 *        Color is a 0-100 warm<->cool mix percentage (0=warm, 100=cool,
 *        50=neutral — see frontlight_apply()), amended 2026-09-20 from an
 *        earlier discrete warm-XOR-cool design. Intensity is a 0-100
 *        overall-brightness percentage. No separate frontlight auto-off
 *        (removed 2026-09-20, user request: a light-only timer alongside
 *        the device's own idle-sleep timeout was two settings for one
 *        job — the light already turns off when the device sleeps,
 *        frontlight_off() in splash.cpp) — settings_store_get/
 *        set_idle_timeout_s() above is now the single inactivity timer
 *        for both.
 */
bool settings_store_get_frontlight_on(void);
void settings_store_set_frontlight_on(bool on);

uint32_t settings_store_get_frontlight_color(void);
void settings_store_set_frontlight_color(uint32_t color);

uint32_t settings_store_get_frontlight_intensity(void);
void settings_store_set_frontlight_intensity(uint32_t percent);

/**
 * @brief touch.c's per-unit calibration correction (Settings > Touch
 *        Calibration's guided sequence, ui_screen_touch_diag.cpp). Scale
 *        is x1000 fixed-point (1000 = 1.000x, always >= 0); offset is a
 *        signed pixel delta -- stored/read via nvs_(get|set)_u32's raw
 *        32-bit value, which round-trips a two's-complement int32_t
 *        bit-for-bit, so no bias encoding is needed here.
 */
uint32_t settings_store_get_touch_cal_scale_x(void);
void settings_store_set_touch_cal_scale_x(uint32_t scale_x1000);
int32_t settings_store_get_touch_cal_offset_x(void);
void settings_store_set_touch_cal_offset_x(int32_t offset_px);
uint32_t settings_store_get_touch_cal_scale_y(void);
void settings_store_set_touch_cal_scale_y(uint32_t scale_y1000);
int32_t settings_store_get_touch_cal_offset_y(void);
void settings_store_set_touch_cal_offset_y(int32_t offset_px);

#ifdef __cplusplus
}
#endif
