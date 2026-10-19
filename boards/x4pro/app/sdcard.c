/*
 Project: MySafeFob  sdcard.c
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
 * @file sdcard.c
 * @brief MySafeFob App — SD card mount/unmount, see sdcard.h.
 */
#include "sdcard.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hw_config.h"
#include "sdmmc_cmd.h"

#include "app_log_workaround.h"

static const char *TAG = "sd";

static sdmmc_card_t *s_card;
static bool s_mounted;

/* Rail GPIO5, active-low: pulse HIGH 80 ms then LOW 120 ms, then held LOW
 * (factory sequence). Left on after unmount, as touch.c's boot sequence
 * leaves it: the sleep path (splash.cpp) turns it off. */
static void power_pulse(void)
{
    gpio_set_level(RAIL_SD_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(80));
    gpio_set_level(RAIL_SD_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
}

/* SDMMC slot 1, 1-bit, 40 MHz: shared by the FAT mount and raw access. */
static void bus_config(sdmmc_host_t *host, sdmmc_slot_config_t *slot)
{
    *host = (sdmmc_host_t)SDMMC_HOST_DEFAULT();
    host->slot = SDMMC_HOST_SLOT_1;
    host->flags = SDMMC_HOST_FLAG_1BIT;
    host->max_freq_khz = SDMMC_FREQ_HIGHSPEED;   /* 40 MHz */

    *slot = (sdmmc_slot_config_t)SDMMC_SLOT_CONFIG_DEFAULT();
    slot->clk = SD_CLK_PIN;
    slot->cmd = SD_CMD_PIN;
    slot->d0 = SD_D0_PIN;
    slot->width = 1;
    slot->cd = GPIO_NUM_NC;
    slot->wp = GPIO_NUM_NC;
    slot->flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
}

esp_err_t sdcard_mount(void)
{
    if (s_mounted) return ESP_OK;
    power_pulse();

    sdmmc_host_t host;
    sdmmc_slot_config_t slot;
    bus_config(&host, &slot);

    esp_vfs_fat_sdmmc_mount_config_t cfg = {
        .format_if_mount_failed = false,   /* never: the card may hold other data */
        .max_files = 2,
        .allocation_unit_size = 16 * 1024,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        s_card = NULL;
        return err;
    }
    s_mounted = true;
    ESP_LOGI(TAG, "mounted on %s", SD_MOUNT_POINT);
    return ESP_OK;
}

esp_err_t sdcard_open_raw(sdmmc_card_t **card_out)
{
    if (s_mounted) return ESP_ERR_INVALID_STATE;
    power_pulse();

    sdmmc_host_t host;
    sdmmc_slot_config_t slot;
    bus_config(&host, &slot);
    static sdmmc_card_t card;   /* lives until the restart that ends USB disk mode */
    esp_err_t err = sdmmc_host_init();
    if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;   /* already initialized */
    if (err == ESP_OK) err = sdmmc_host_init_slot(host.slot, &slot);
    if (err == ESP_OK) err = sdmmc_card_init(&host, &card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "raw open failed: %s", esp_err_to_name(err));
        sdmmc_host_deinit();
        return err;
    }
    ESP_LOGI(TAG, "opened raw: %lu sectors of %lu bytes", (unsigned long)card.csd.capacity,
             (unsigned long)card.csd.sector_size);
    *card_out = &card;
    return ESP_OK;
}

esp_err_t sdcard_unmount(void)
{
    if (!s_mounted) return ESP_OK;
    esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    s_card = NULL;
    s_mounted = false;
    ESP_LOGI(TAG, "unmounted");
    return err;
}
