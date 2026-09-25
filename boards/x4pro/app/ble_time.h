/*
 Project: MySafeFob  ble_time.h
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
 * @file ble_time.h
 * @brief MySafeFob App — BLE time sync (ADR-006): the device is a GATT
 *        *client* (NimBLE central). Scan for connectable devices, connect to
 *        the chosen one, read the Current Time Service (0x1805, characteristic
 *        0x2A2B, plus Local Time Information 0x2A0F when present), set the
 *        clock, disconnect. No pairing, no data sent to the peer; the BLE
 *        stack is fully deinitialised when the operation ends. Same
 *        poll-from-the-UI-task model as wifi_time.h.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLE_TIME_IDLE = 0,
    BLE_TIME_SCANNING,
    BLE_TIME_SCAN_DONE,
    BLE_TIME_CONNECTING,
    BLE_TIME_READING,
    BLE_TIME_SYNC_OK,
    BLE_TIME_FAILED,
} ble_time_state_t;

#define BLE_TIME_MAX_DEVICES 5

typedef struct {
    char name[24]; /* empty if the device did not advertise a name */
    int rssi;
    bool has_cts;  /* advertises the Current Time Service (0x1805) */
    char addr[18]; /* "AA:BB:CC:DD:EE:FF" */
} ble_time_device_t;

/** @brief Starts a scan (about 5 s). @return false if an operation is already running. */
bool ble_time_scan_start(void);

/** @brief Connects to device `index` of the last scan and reads its time. @return false if busy/invalid. */
bool ble_time_sync_start(int index);

ble_time_state_t ble_time_get_state(void);
int ble_time_get_device_count(void);
bool ble_time_get_device(int index, ble_time_device_t *out);

/** @brief Human-readable status/failure text for the current state. */
const char *ble_time_get_message(void);

#ifdef __cplusplus
}
#endif
