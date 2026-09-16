/**
 * @file power_mgr.c
 * @brief MySafeFob power_mgr — STUB Phase 8 (contrat ADR-009).
 *
 * power_mgr_shutdown() est la seule fonction "implementee" : elle fait le
 * deep sleep minimal (wake-up GPIO3) SANS les deinit board (e-ink/rails),
 * celles-ci arrivant avec les drivers de la tache 8c. Permet d'ores et deja
 * de valider le mecanisme de reveil hardware.
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

/* GPIO Power X4 Pro (hw_config du board — duplique ici en attendant le
 * board_config partage de la tache 8c ; NE PAS diverger). */
#define MSF_POWER_PIN   GPIO_NUM_3

/* -----------------------------------------------------------------------
 * Bascule vers factory (Power tenu >= 10 s, ADR-009 amende 2026-09-16).
 *
 * CONTRAT PARTAGE avec boards/x4pro/factory/bootloader_components/
 * custom_bootloader/hooks.c (backup_and_erase_otadata) et avec la factory
 * app (main.c, restore_otadata_from_backup) : mêmes offsets, même layout de
 * secteur de backup. Ici on est en contexte app (pas bootloader ROM), donc
 * on utilise l'API esp_flash_* au lieu de esp_rom_spiflash_*.
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

    /* S3 : pas de SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP (GPIO digital). GPIO3
     * est un RTC IO -> EXT1 (ANY_LOW = un seul pin, suffit ici). */
    return esp_sleep_enable_ext1_wakeup(1ULL << MSF_POWER_PIN,
                                        ESP_EXT1_WAKEUP_ANY_LOW);
}

bool power_mgr_wakeup_from_power(void)
{
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
}

bool power_mgr_battery_critical(void)
{
    return false;   /* CW2017 en 8c */
}

void power_mgr_shutdown(void)
{
    /* TODO 8c : e-ink POF + frontlight off + rails hold before sleep.
     * The caller may draw a sleep screen first (board_sleep_screen_show):
     * the panel is bistable, the image persists at zero power. */

    /* BUGFIX 2026-09-16 (observe sur hardware, "je passe en veille et je me
     * reveille aussitot") : EXT1 (ANY_LOW) reveille des que la condition
     * est vraie. power_button_task() appelle cette fonction PENDANT que
     * Power est encore physiquement enfonce (c'est le geste qui a declenche
     * le long-press) -> sans attendre le relachement, la condition de
     * reveil est deja satisfaite au moment ou le sommeil commence, d'ou le
     * rebond veille<->reveil quasi instantane. On attend explicitement le
     * relachement avant d'armer EXT1 et de dormir. */
    ESP_LOGI(TAG, "attente relachement Power avant veille...");
    while (gpio_get_level(MSF_POWER_PIN) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    /* Re-arm EXT1 right before sleeping — idempotent, and guarantees the
     * wake source even if a future code path reconfigures the pin. */
    esp_sleep_enable_ext1_wakeup(1ULL << MSF_POWER_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
    ESP_LOGI(TAG, "entering deep sleep — wake on Power (GPIO3, EXT1)");
    esp_deep_sleep_start();
    /* jamais atteint */
}

void power_mgr_switch_to_factory(void)
{
    uint8_t buf[FLASH_SECTOR_SIZE];
    uint32_t magic = BACKUP_MAGIC;
    esp_err_t err;

    ESP_LOGI(TAG, "Power >= 10s : bascule vers factory — backup otadata...");

    /* BUGFIX 2026-09-16 (diagnostic) : un log avant CHAQUE appel esp_flash_*
     * permet, si cette fonction se bloque, de savoir exactement laquelle des
     * operations flash est en cause (contexte app/tache, pas bootloader ROM
     * — premiere fois que ce chemin est exerce en conditions reelles). */
    ESP_LOGI(TAG, "lecture otadata secteur 1...");
    err = esp_flash_read(NULL, buf, OTADATA_OFFSET, FLASH_SECTOR_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lecture otadata secteur 1 impossible (%s)", esp_err_to_name(err));
        return;
    }
    uint8_t entry1[OTADATA_ENTRY_SIZE];
    memcpy(entry1, buf, OTADATA_ENTRY_SIZE);

    ESP_LOGI(TAG, "lecture otadata secteur 2...");
    err = esp_flash_read(NULL, buf, OTADATA_OFFSET + FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lecture otadata secteur 2 impossible (%s)", esp_err_to_name(err));
        return;
    }
    uint8_t entry2[OTADATA_ENTRY_SIZE];
    memcpy(entry2, buf, OTADATA_ENTRY_SIZE);

    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, entry1, OTADATA_ENTRY_SIZE);
    memcpy(buf + OTADATA_ENTRY_SIZE, entry2, OTADATA_ENTRY_SIZE);
    memcpy(buf + BACKUP_MAGIC_OFFSET, &magic, sizeof(magic));

    ESP_LOGI(TAG, "erase secteur backup @0x%x...", OTADATA_BACKUP_OFFSET);
    err = esp_flash_erase_region(NULL, OTADATA_BACKUP_OFFSET, FLASH_SECTOR_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase secteur backup impossible (%s)", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "ecriture backup...");
    err = esp_flash_write(NULL, buf, OTADATA_BACKUP_OFFSET, FLASH_SECTOR_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ecriture backup impossible (%s)", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "otadata sauvegarde @0x%x", OTADATA_BACKUP_OFFSET);

    ESP_LOGI(TAG, "erase otadata secteur 1...");
    err = esp_flash_erase_region(NULL, OTADATA_OFFSET, FLASH_SECTOR_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase otadata secteur 1 impossible (%s)", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "erase otadata secteur 2...");
    err = esp_flash_erase_region(NULL, OTADATA_OFFSET + FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase otadata secteur 2 impossible (%s)", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "otadata efface — redemarrage vers factory");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    /* jamais atteint */
}
