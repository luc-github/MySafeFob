/* 
 Project: MySafeFob  power_mgr.h
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
 * @file power_mgr.h
 * @brief MySafeFob — software power on/off management (contract, ADR-009).
 *
 * The Power button (GPIO3, active-LOW) is a plain input: the on/off
 * function is achieved via DEEP SLEEP + GPIO wake-up. The device lives
 * "off" between uses; TOTP codes are computed on demand at wake-up
 * (the BM8563 RTC keeps time on battery).
 *
 * Shutdown sequence (power_mgr_shutdown):
 *   1. e-ink power-off (image kept — bistable)
 *   2. frontlight off, optional rails cut (RTC hold)
 *   3. esp_deep_sleep_start() + GPIO3 LOW wake-up (RTC pull-up)
 *
 * At wake-up: S3 reboot, esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1
 *   (GPIO3 = RTC IO -> EXT1 ANY_LOW; digital GPIO wake-up doesn't exist
 *   on S3: no SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP)
 *   => the app jumps straight to the UNLOCK screen.
 *
 *   BUTTON SEMANTICS (ADR-009 amended 2026-09-16): **Power alone**, duration
 *   measurement, no more Power+Right combo — dropped because empirically the
 *   combo prevented the wake-up itself from triggering (tested on
 *   hardware: Power alone wakes systematically, Power+Right together
 *   NEVER wakes, regardless of hold duration — a problem upstream of the
 *   bootloader hook, not a software timing issue).
 *     - Power held < 10s (from the awake app) => normal deep sleep.
 *     - Power held >= 10s (awake OR during the wake window from
 *       sleep) => switch to factory:
 *         - Awake: power_mgr_switch_to_factory() (software, esp_ota).
 *         - Asleep: hooks.c (bootloader) directly measures GPIO3's hold
 *           duration (same 10s threshold), no more dependency on GPIO7
 *           during the wake window.
 *   Left+Power remains impossible (GPIO0 strapping -> download mode).
 *
 * STATUS (2026-09-14): wake/shutdown flow wired for validation —
 * power_mgr_init() at boot logs the wake cause, the REPL `sleep` command
 * draws the board sleep screen then enters deep sleep. Board deinit
 * (e-ink POF from power_mgr itself, rails, frontlight) still TODO 8c.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configures the wake-up source (Power GPIO, active-LOW) and the
 *        RTC pull-up. Call once at boot, before any sleep.
 */
esp_err_t power_mgr_init(void);

/**
 * @brief True if the current boot is a wake-up via the Power button (deep sleep).
 *        The app uses this to skip the splash and go straight to UNLOCK.
 */
bool power_mgr_wakeup_from_power(void);

/**
 * @brief Battery state from the power management side: true if the
 *        critical threshold is reached and an imminent shutdown is
 *        recommended. (Implemented with CW2017 in 8c; returns false by default.)
 */
bool power_mgr_battery_critical(void);

/**
 * @brief Puts the device into deep sleep. NEVER RETURNS
 *        (reboots on wake). Runs the board deinitializations
 *        (e-ink POF, frontlight, rails) before esp_deep_sleep_start().
 */
void power_mgr_shutdown(void);

/**
 * @brief Software switch to the factory partition (Power held >= 10s
 *        from the awake app — ADR-009 amended 2026-09-16). Backs up
 *        otadata @0xB000 (same contract as hooks.c / factory main.c — the
 *        factory restores this backup at its startup, so a Cancel from
 *        the factory cleanly returns to the app), erases otadata, then
 *        esp_restart(). NEVER RETURNS on success; on failure
 *        (flash read/write), returns and logs the error —
 *        the caller stays awake, doesn't sleep by mistake.
 */
void power_mgr_switch_to_factory(void);

/**
 * @brief Claims exclusive ownership of a terminal transition (entering
 *        deep sleep, or switching to factory). Several independent
 *        sources can lead to one of these transitions (a physical Power
 *        press, the REPL `sleep` command, the ADR-012 idle-activity
 *        timeout, a "Sleep now" menu action): only the first caller may
 *        proceed, so two of them can never race and touch the e-ink /
 *        flash state concurrently.
 *
 * @return true if the caller won the claim (must proceed with the
 *         transition); false if another one is already in progress
 *         (caller must not proceed).
 */
bool power_mgr_claim_terminal_action(void);

/**
 * @brief Releases a claim taken via power_mgr_claim_terminal_action().
 *        Only needed after a FAILED power_mgr_switch_to_factory() (it
 *        returns instead of rebooting) — power_mgr_shutdown() never
 *        returns, so its callers never need to call this.
 */
void power_mgr_release_terminal_action(void);

#ifdef __cplusplus
}
#endif
