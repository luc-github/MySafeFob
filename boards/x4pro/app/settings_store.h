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

/** @brief Local time zone as a fixed UTC offset in minutes (display only; the clock and TOTP stay UTC). */
int32_t settings_store_get_time_tz_offset_min(void);
void settings_store_set_time_tz_offset_min(int32_t minutes);

#define SETTINGS_TIME_SYNC_HISTORY 3

/**
 * @brief History of the last SETTINGS_TIME_SYNC_HISTORY clock
 *        synchronisations (drift tracking, About screen), newest = index 0.
 *        Each record: UTC epoch, signed offset in seconds the clock had just
 *        before the sync (INT32_MIN = unknown), source (time_service.h's
 *        time_sync_source_t).
 * @return false if there is no record at `index`.
 */
bool settings_store_get_time_sync(int index, uint32_t *epoch, int32_t *delta_s, uint32_t *source);
void settings_store_push_time_sync(uint32_t epoch, int32_t delta_s, uint32_t source);

/**
 * @brief ADR-018: age of the last time sync (seconds) above which HOME's
 *        alert button reports "time sync is old". Default 90 days,
 *        0 = alert disabled.
 */
uint32_t settings_store_get_time_sync_max_age_s(void);

/**
 * @brief F-05 auto-clear: seconds a displayed secret stays on screen
 *        without interaction before the UI returns to a neutral screen.
 *        Default 300 (5 min).
 */
uint32_t settings_store_get_secret_auto_clear_s(void);

/* ---- Generic access by table index (console `setting` command, ADR-018,
 * main/cmd_setting.c). Indexes run 0..settings_store_count()-1, in
 * settings_defs.inc order. Consumers read settings on use, so a value set
 * here applies at its next read (some are only read at boot, e.g. touch
 * calibration). ---- */

typedef enum { SETTINGS_KIND_BOOL, SETTINGS_KIND_U32, SETTINGS_KIND_I32 } settings_kind_t;

typedef struct {
    const char *name;        /* X-macro id, e.g. "IdleTimeoutS" */
    const char *nvs_key;
    settings_kind_t kind;
    uint32_t default_value;  /* raw 32-bit pattern (I32: two's complement) */
} settings_info_t;

int settings_store_count(void);
bool settings_store_describe(int index, settings_info_t *info);
/** @brief Index of the setting named `name` (id or NVS key, case-insensitive), -1 if none. */
int settings_store_find(const char *name);
/** @brief Current value as its raw 32-bit pattern (default if unset). */
uint32_t settings_store_get_raw(int index);
/** @brief Persists a raw value (BOOL: any non-zero = 1). */
esp_err_t settings_store_set_raw(int index, uint32_t value);
/** @brief Erases the NVS key, so the setting reads its default again. */
esp_err_t settings_store_reset(int index);

#ifdef __cplusplus
}
#endif
