/*
 Project: MySafeFob  wifi_time.h
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
 * @file wifi_time.h
 * @brief MySafeFob App — Wi-Fi time sync (ADR-001): scan for networks, or
 *        join one and set the clock from SNTP. Everything runs in a
 *        short-lived worker task; the radio is fully torn down (stopped,
 *        deinitialised) when the operation ends, and credentials live in
 *        RAM only for the duration of the call (never stored). The UI polls
 *        wifi_time_get_state() from its own task.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_TIME_IDLE = 0,
    WIFI_TIME_SCANNING,
    WIFI_TIME_SCAN_DONE,
    WIFI_TIME_CONNECTING,
    WIFI_TIME_SYNCING,
    WIFI_TIME_SYNC_OK,
    WIFI_TIME_FAILED,
} wifi_time_state_t;

#define WIFI_TIME_MAX_APS 5

typedef struct {
    char ssid[33];
    int rssi;
    bool secure;
} wifi_time_ap_t;

/** @brief Starts a scan. @return false if an operation is already running. */
bool wifi_time_scan_start(void);

/** @brief Joins `ssid` (password NULL/"" for open networks), syncs SNTP, sets the clock + RTC. @return false if busy. */
bool wifi_time_sync_start(const char *ssid, const char *password);

wifi_time_state_t wifi_time_get_state(void);

/** @brief Number of networks found by the last scan (valid in SCAN_DONE). */
int wifi_time_get_ap_count(void);
bool wifi_time_get_ap(int index, wifi_time_ap_t *out);

/** @brief Human-readable status/failure text for the current state. */
const char *wifi_time_get_message(void);

#ifdef __cplusplus
}
#endif
