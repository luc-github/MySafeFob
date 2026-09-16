/**
 * @file power_mgr.c
 * @brief MySafeFob power_mgr — STUB Phase 8 (ADR-009 contract).
 *
 * power_mgr_shutdown() is the only "implemented" function: it does the
 * minimal deep sleep (GPIO3 wake-up) WITHOUT board deinit (e-ink/rails),
 * those arriving with the task 8c drivers. Already lets us validate
 * the hardware wake-up mechanism.
 */
#include "power_mgr.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power_mgr";

/* X4 Pro Power GPIO (board hw_config — duplicated here until the
 * shared board_config from task 8c ; DO NOT let it diverge). */
#define MSF_POWER_PIN   GPIO_NUM_3

/* -----------------------------------------------------------------------
 * Switch to factory (Power held >= 10s, ADR-009 amended 2026-09-16).
 *
 * SHARED CONTRACT with boards/x4pro/factory/bootloader_components/
 * custom_bootloader/hooks.c (backup_and_erase_otadata) and with the factory
 * app (main.c, restore_otadata_from_backup): same offsets, same backup
 * sector layout. Here we're in app context (not bootloader ROM), so
 * we use the esp_flash_* API instead of esp_rom_spiflash_*.
 * ----------------------------------------------------------------------- */
#define FLASH_SECTOR_SIZE       0x1000
#define OTADATA_OFFSET          0x10000
#define OTADATA_BACKUP_OFFSET   0xB000
#define OTADATA_ENTRY_SIZE      32
#define BACKUP_MAGIC_OFFSET     0x40
#define BACKUP_MAGIC            0xAA55AA55

esp_err_t power_mgr_init(void)
{
    /* Wake-cause logging — flow validation (normal wake vs factory rescue
     * is decided by the bootloader hook before we ever run). */
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT1:
        ESP_LOGI(TAG, "wake: deep-sleep EXT1 (Power button)");
        break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
        ESP_LOGI(TAG, "wake: cold boot");
        break;
    default:
        ESP_LOGI(TAG, "wake: unexpected cause %d", (int)cause);
        break;
    }

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << MSF_POWER_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;

    /* S3: no SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP (digital GPIO). GPIO3
     * is an RTC IO -> EXT1 (ANY_LOW = a single pin, enough here). */
    return esp_sleep_enable_ext1_wakeup(1ULL << MSF_POWER_PIN,
                                        ESP_EXT1_WAKEUP_ANY_LOW);
}

bool power_mgr_wakeup_from_power(void)
{
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
}

bool power_mgr_battery_critical(void)
{
    return false;   /* CW2017 in 8c */
}

void power_mgr_shutdown(void)
{
    /* TODO 8c: e-ink POF + frontlight off + rails hold before sleep.
     * The caller may draw a sleep screen first (board_sleep_screen_show):
     * the panel is bistable, the image persists at zero power. */

    /* BUGFIX 2026-09-16 (observed on hardware, "I go to sleep and wake
     * right back up"): EXT1 (ANY_LOW) wakes as soon as the condition is
     * true. power_button_task() calls this function WHILE Power is still
     * physically held down (that's the gesture that triggered the
     * long-press) -> without waiting for release, the wake condition is
     * already satisfied the moment sleep begins, hence the near-instant
     * sleep<->wake bounce. We explicitly wait for release before arming
     * EXT1 and sleeping. */
    ESP_LOGI(TAG, "waiting for Power release before sleeping...");
    while (gpio_get_level(MSF_POWER_PIN) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    /* Re-arm EXT1 right before sleeping — idempotent, and guarantees the
     * wake source even if a future code path reconfigures the pin. */
    esp_sleep_enable_ext1_wakeup(1ULL << MSF_POWER_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
    ESP_LOGI(TAG, "entering deep sleep — wake on Power (GPIO3, EXT1)");
    esp_deep_sleep_start();
    /* never reached */
}

void power_mgr_switch_to_factory(void)
{
    uint8_t buf[FLASH_SECTOR_SIZE];
    uint32_t magic = BACKUP_MAGIC;
    esp_err_t err;

    ESP_LOGI(TAG, "Power >= 10s: switching to factory — backing up otadata...");

    /* BUGFIX 2026-09-16 (diagnostic): a log before EACH esp_flash_* call
     * lets us know exactly which flash operation is at fault if this
     * function hangs (app/task context, not bootloader ROM — first time
     * this path is exercised under real conditions). */
    ESP_LOGI(TAG, "reading otadata sector 1...");
    err = esp_flash_read(NULL, buf, OTADATA_OFFSET, FLASH_SECTOR_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not read otadata sector 1 (%s)", esp_err_to_name(err));
        return;
    }
    uint8_t entry1[OTADATA_ENTRY_SIZE];
    memcpy(entry1, buf, OTADATA_ENTRY_SIZE);

    ESP_LOGI(TAG, "reading otadata sector 2...");
    err = esp_flash_read(NULL, buf, OTADATA_OFFSET + FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not read otadata sector 2 (%s)", esp_err_to_name(err));
        return;
    }
    uint8_t entry2[OTADATA_ENTRY_SIZE];
    memcpy(entry2, buf, OTADATA_ENTRY_SIZE);

    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, entry1, OTADATA_ENTRY_SIZE);
    memcpy(buf + OTADATA_ENTRY_SIZE, entry2, OTADATA_ENTRY_SIZE);
    memcpy(buf + BACKUP_MAGIC_OFFSET, &magic, sizeof(magic));

    ESP_LOGI(TAG, "erasing backup sector @0x%x...", OTADATA_BACKUP_OFFSET);
    err = esp_flash_erase_region(NULL, OTADATA_BACKUP_OFFSET, FLASH_SECTOR_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not erase backup sector (%s)", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "writing backup...");
    err = esp_flash_write(NULL, buf, OTADATA_BACKUP_OFFSET, FLASH_SECTOR_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not write backup (%s)", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "otadata backed up @0x%x", OTADATA_BACKUP_OFFSET);

    ESP_LOGI(TAG, "erasing otadata sector 1...");
    err = esp_flash_erase_region(NULL, OTADATA_OFFSET, FLASH_SECTOR_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not erase otadata sector 1 (%s)", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "erasing otadata sector 2...");
    err = esp_flash_erase_region(NULL, OTADATA_OFFSET + FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not erase otadata sector 2 (%s)", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "otadata erased — rebooting to factory");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    /* never reached */
}
