/**
 * @file factory_log.h
 * @brief MySafeFob Factory — gate de debug identique au PiBot (FACTORY_LOGD).
 *   FACTORY_LOG_LEVEL vient d'ENABLE_FACTORY_DEBUG_LOG (Factory/CMakeLists.txt).
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
