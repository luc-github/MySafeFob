/**
 * @file sdcard.c
 * @brief MySafeFob Factory — SD X4 Pro (SDMMC 1-bit, slot 1).
 *   Extrait de sd_test.c du probe (validation SD 16 Go FAT32, 2026-09-13).
 */
#include "sdcard.h"
#include "hw_config.h"

#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"

static const char *TAG = "sd";

static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

static void sd_power_pulse(void)
{
    /* GPIO5 actif-LOW : pulse HIGH 80 ms puis LOW 120 ms, puis tenu LOW. */
    gpio_config_t io = { .pin_bit_mask = 1ULL << RAIL_SD_PIN,
                         .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io);
    gpio_set_level(RAIL_SD_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(80));
    gpio_set_level(RAIL_SD_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
}

esp_err_t sdcard_mount(void)
{
    if (s_mounted) return ESP_OK;

    sd_power_pulse();

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;   /* 40 MHz */

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = SD_CLK_PIN;
    slot.cmd = SD_CMD_PIN;
    slot.d0 = SD_D0_PIN;
    slot.width = 1;
    slot.cd = GPIO_NUM_NC;
    slot.wp = GPIO_NUM_NC;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot,
                                            &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
        return err;
    }
    s_mounted = true;
    ESP_LOGI(TAG, "SD montee sur %s", SD_MOUNT_POINT);
    return ESP_OK;
}

esp_err_t sdcard_unmount(void)
{
    if (!s_mounted) return ESP_OK;
    esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    s_card = NULL;
    s_mounted = false;
    return err;
}
