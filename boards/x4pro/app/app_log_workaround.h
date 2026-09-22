/*
 Project: MySafeFob  app_log_workaround.h
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
 * @file app_log_workaround.h
 * @brief MySafeFob App — WORKAROUND (2026-09-18): standard ESP_LOG* calls
 *        from boards/x4pro/app never reach the serial monitor, 100%
 *        reproducible -- a raw printf() from the same call site always
 *        works. Root cause not yet understood. Previously this override was
 *        copy-pasted identically into ui_nav.cpp, lv_port_disp.c,
 *        lv_port_indev.c and touch.c; centralized here (2026-09-22) so the
 *        workaround -- and its eventual removal, once the real cause is
 *        found -- only needs touching one file.
 *
 *        Include this AFTER "esp_log.h" in any .c/.cpp file under this
 *        component that needs its ESP_LOG* calls to actually show up.
 */
#pragma once

#include <stdio.h>
#include "esp_timer.h"

#undef ESP_LOGE
#undef ESP_LOGW
#undef ESP_LOGI
#define ESP_LOGE(tag, fmt, ...) do { \
        printf("E (%lld) %s: " fmt "\r\n", (long long)(esp_timer_get_time() / 1000), tag, ##__VA_ARGS__); \
        fflush(stdout); \
    } while (0)
#define ESP_LOGW(tag, fmt, ...) do { \
        printf("W (%lld) %s: " fmt "\r\n", (long long)(esp_timer_get_time() / 1000), tag, ##__VA_ARGS__); \
        fflush(stdout); \
    } while (0)
#define ESP_LOGI(tag, fmt, ...) do { \
        printf("I (%lld) %s: " fmt "\r\n", (long long)(esp_timer_get_time() / 1000), tag, ##__VA_ARGS__); \
        fflush(stdout); \
    } while (0)
