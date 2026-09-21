/*
 Project: MySafeFob  frontlight.h
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
 * @file frontlight.h
 * @brief MySafeFob App — dual warm/cool LEDC frontlight driver.
 *
 * On/off + a continuous warm<->cool color mix (both channels can be
 * active at once, unlike the original discrete Warm-XOR-Cool design --
 * amended 2026-09-20 after hands-on testing showed a plain toggle felt
 * wrong for what reads as a color-temperature control) at an adjustable
 * overall intensity. Pattern: a small native ESP-IDF LEDC driver, the
 * same shape as references/Luc-Pibot-cnc-pendant-firmware's
 * disp_backlight.c, extended to 2 channels -- not a port of
 * freeink-sdk's FrontlightManager (gamma-corrected blending + deep-sleep
 * GPIO hold, more than this design needs; the crossfade here is a plain
 * linear split, not photometrically corrected).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configures the LEDC timer + the two channels (hw_config.h pins),
 *        both starting at duty 0 (off). Call once at app startup.
 */
esp_err_t frontlight_init(void);

/**
 * @brief Applies the given state. `color_mix_pct` (0-100, clamped): 0 =
 *        full warm, 100 = full cool, 50 = neutral (both channels at half
 *        of `intensity_pct`'s duty). `intensity_pct` (0-100, clamped)
 *        scales overall brightness, applied after the warm/cool split.
 *        `on == false` drives both channels to duty 0 regardless of the
 *        other two arguments.
 */
void frontlight_apply(bool on, uint8_t color_mix_pct, uint8_t intensity_pct);

/**
 * @brief Convenience: both channels to duty 0. Used on the deep-sleep
 *        path (splash.cpp) so the light is never left lit through sleep,
 *        regardless of the persisted on/off preference.
 */
void frontlight_off(void);

#ifdef __cplusplus
}
#endif
