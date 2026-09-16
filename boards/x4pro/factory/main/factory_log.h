/* 
 Project: MySafeFob  factory_log.h
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
 * @file factory_log.h
 * @brief MySafeFob Factory — debug gate (FACTORY_LOGD).
 *   FACTORY_LOG_LEVEL comes from ENABLE_FACTORY_DEBUG_LOG (Factory/CMakeLists.txt).
 */
#pragma once

#include "esp_log.h"

#ifndef FACTORY_LOG_LEVEL
#define FACTORY_LOG_LEVEL 0
#endif

#if FACTORY_LOG_LEVEL
#define FACTORY_LOGD(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#else
#define FACTORY_LOGD(tag, fmt, ...) do {} while (0)
#endif

static inline void factory_log_silence_sd_stack(void)
{
    esp_log_level_set("sdmmc", ESP_LOG_NONE);
    esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_NONE);
    esp_log_level_set("sdmmc_periph", ESP_LOG_NONE);
    esp_log_level_set("sdmmc_req", ESP_LOG_NONE);
    esp_log_level_set("sdmmc_common", ESP_LOG_NONE);
    esp_log_level_set("fatfs", ESP_LOG_NONE);
    esp_log_level_set("sd_diskio", ESP_LOG_NONE);
}
