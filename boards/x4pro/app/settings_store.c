/*
 Project: MySafeFob  settings_store.c
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
 * @file settings_store.c
 * @brief MySafeFob App — persisted UI preferences (NVS-backed).
 *
 * Table-driven engine (settings_defs.inc) + a mutex around every NVS
 * access: multiple tasks call the accessors below (board_ui_nav_task,
 * main.c's power_button_task and REPL command handlers) and NVS's own
 * thread-safety only covers individual calls, not the get-then-maybe-set
 * sequences a caller might do around them.
 */
#include "settings_store.h"

#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "settings_store";
#define NVS_NAMESPACE "msf_ui"

typedef enum {
    SETTING_TYPE_BOOL,   /* stored as nvs_(get|set)_u8, 0/1 */
    SETTING_TYPE_U32,
} settings_type_t;

/* One id per settings_defs.inc line — internal only, never exposed
 * outside this file (callers use the named accessors in settings_store.h). */
typedef enum {
#define SETTINGS_DEF(id, key, type, def) SETTINGS_ID_##id,
#include "settings_defs.inc"
#undef SETTINGS_DEF
    SETTINGS_ID_COUNT,
} settings_id_t;

typedef struct {
    const char *nvs_key;
    settings_type_t type;
    uint32_t default_value;   /* bool 0/1 or the raw u32 default */
} settings_desc_t;

#define SETTINGS_DEF(id, key, type, def) \
    [SETTINGS_ID_##id] = { key, SETTING_TYPE_##type, (uint32_t)(def) },
static const settings_desc_t kSettingsTable[SETTINGS_ID_COUNT] = {
#include "settings_defs.inc"
};
#undef SETTINGS_DEF

static nvs_handle_t s_handle = 0;
static bool s_open = false;
static SemaphoreHandle_t s_mutex = NULL;

esp_err_t settings_store_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open FAILED (%s)", esp_err_to_name(err));
        return err;
    }
    s_open = true;
    return ESP_OK;
}

/* Generic engine: every typed accessor in this file is a thin wrapper
 * over these two, so a new setting only ever needs one line in
 * settings_defs.inc plus a small named wrapper — never a hand-written
 * NVS read/write pair. */
static uint32_t settings_get_u32(settings_id_t id)
{
    const settings_desc_t *desc = &kSettingsTable[id];
    if (!s_open) {
        return desc->default_value;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t v = desc->default_value;
    esp_err_t err;
    if (desc->type == SETTING_TYPE_BOOL) {
        uint8_t b = (uint8_t)desc->default_value;
        err = nvs_get_u8(s_handle, desc->nvs_key, &b);
        v = b;
    } else {
        err = nvs_get_u32(s_handle, desc->nvs_key, &v);
    }
    xSemaphoreGive(s_mutex);

    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "get '%s' FAILED (%s), defaulting", desc->nvs_key, esp_err_to_name(err));
        return desc->default_value;
    }
    return v;
}

static void settings_set_u32(settings_id_t id, uint32_t value)
{
    const settings_desc_t *desc = &kSettingsTable[id];
    if (!s_open) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = (desc->type == SETTING_TYPE_BOOL)
                        ? nvs_set_u8(s_handle, desc->nvs_key, (uint8_t)value)
                        : nvs_set_u32(s_handle, desc->nvs_key, value);
    if (err == ESP_OK) {
        err = nvs_commit(s_handle);
    }
    xSemaphoreGive(s_mutex);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set '%s' FAILED (%s)", desc->nvs_key, esp_err_to_name(err));
    }
}

bool settings_store_get_power_short_confirm(void)
{
    return settings_get_u32(SETTINGS_ID_PowerShortConfirm) != 0;
}

void settings_store_set_power_short_confirm(bool on)
{
    settings_set_u32(SETTINGS_ID_PowerShortConfirm, on ? 1 : 0);
}

uint32_t settings_store_get_idle_timeout_s(void)
{
    return settings_get_u32(SETTINGS_ID_IdleTimeoutS);
}

void settings_store_set_idle_timeout_s(uint32_t seconds)
{
    settings_set_u32(SETTINGS_ID_IdleTimeoutS, seconds);
}

bool settings_store_get_frontlight_on(void)
{
    return settings_get_u32(SETTINGS_ID_FrontlightOn) != 0;
}

void settings_store_set_frontlight_on(bool on)
{
    settings_set_u32(SETTINGS_ID_FrontlightOn, on ? 1 : 0);
}

uint32_t settings_store_get_frontlight_color(void)
{
    return settings_get_u32(SETTINGS_ID_FrontlightColor);
}

void settings_store_set_frontlight_color(uint32_t color)
{
    settings_set_u32(SETTINGS_ID_FrontlightColor, color);
}

uint32_t settings_store_get_frontlight_intensity(void)
{
    return settings_get_u32(SETTINGS_ID_FrontlightIntensity);
}

void settings_store_set_frontlight_intensity(uint32_t percent)
{
    settings_set_u32(SETTINGS_ID_FrontlightIntensity, percent);
}

