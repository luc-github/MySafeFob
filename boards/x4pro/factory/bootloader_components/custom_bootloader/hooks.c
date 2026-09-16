/* 
 Project: MySafeFob  hooks.c
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
 * @file hooks.c
 * @brief MySafeFob — custom bootloader hooks (ADR-007).
 *
 * Port of a proven bootloader-hook mechanism from an earlier ESP32
 * project (same author) to ESP32-S3 / IDF 5.5.x, X4 Pro.
 *
 * Recovery trigger (ADR-009 amended 2026-09-16): POWER button (GPIO3)
 * held at wake for >= 10s:
 *   1. Backup otadata to the reserved sector 0xB000 (+ magic 0xAA55AA55)
 *   2. Erase otadata -> the standard bootloader boots the "factory" partition
 *   3. Software reset -> boot factory
 *
 * The factory app restores otadata from the backup at startup (then
 * erases the backup): a power-off from the factory returns to the
 * correct OTA partition.
 *
 * History: the initial trigger used a Power+Right combo (GPIO7).
 * Dropped — tested on hardware, Power+Right together NEVER woke the
 * device, regardless of hold duration (a problem upstream of the
 * hook, not a software timing issue). Replaced by Power alone, duration
 * measurement: this hook runs at wake-up (the button that woke the S3 is
 * already Power), so it's enough to keep measuring how long
 * GPIO3 stays low after wake-up.
 *
 * Validated porting points (ADR-007 §4):
 *   - No buzzer on the X4 Pro: acknowledgment = esp_rom_printf logs.
 *   - Trigger = GPIO3 (Power): GPIO0 is strapping (Left).
 *   - esp_rom_spiflash_* signatures on S3 / IDF 5.5.5: TO BE VERIFIED at
 *     the 1st compile (the original code runs on 5.4.3 on classic ESP32).
 */

#include <string.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_rom_sys.h"
#include "esp_rom_gpio.h"
#include "esp_rom_spiflash.h"
#include "sdkconfig.h"
#include "hal/gpio_ll.h"
#include "hal/rtc_io_ll.h"
#include "hal/wdt_hal.h"
#include "soc/reset_reasons.h"

static const char *TAG = "msf-hook";

/* CONFIG_BOOTLOADER_LOG_LEVEL_NONE strips ESP_LOG* at this build stage:
 * direct prints via esp_rom_printf, gated by FACTORY_LOG_LEVEL
 * (see CMakeLists.txt), independent of sdkconfig. Errors/warnings always on. */
#ifndef FACTORY_LOG_LEVEL
#define FACTORY_LOG_LEVEL 0
#endif
#define HOOK_LOGE(tag, fmt, ...) esp_rom_printf("E (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define HOOK_LOGW(tag, fmt, ...) esp_rom_printf("W (%s) " fmt "\n", tag, ##__VA_ARGS__)
#if FACTORY_LOG_LEVEL
#define HOOK_LOGI(tag, fmt, ...) esp_rom_printf("I (%s) " fmt "\n", tag, ##__VA_ARGS__)
#else
#define HOOK_LOGI(tag, fmt, ...) do {} while (0)
#endif

#define FLASH_SECTOR_SIZE  0x1000

/* -----------------------------------------------------------------------
 * Hardware config (X4 Pro — docs/hardware-specs.md)
 * ----------------------------------------------------------------------- */

#define RECOVERY_BUTTON_PIN   GPIO_NUM_3   /* Power button, active-LOW, pull-up */

/*
 * WARNING — CRITICAL CONSTRAINT: OTADATA BACKUP SECTOR
 * ==========================================================
 * The backup sector must satisfy ALL of these conditions:
 *
 *   1. AFTER the end of the bootloader: bootloader @0x1000, S3 binary ~20-24 KB,
 *      next 4 KB boundary = 0x7000. Backup must be >= 0x7000
 *      (0xB000 does this with margin; S3 bootloader size to be measured).
 *   2. BEFORE the partition table: CONFIG_PARTITION_TABLE_OFFSET=0xC000.
 *      Backup + 0x1000 <= 0xC000  =>  backup <= 0xB000.
 *   3. OUTSIDE any partition: the IDF dangerous-write check aborts
 *      if the address is inside a known partition. 0xB000 is outside partitions
 *      (1st partition = NVS @0xD000). OK.
 *   4. 4 KB aligned (sector boundary).
 *
 * DANGER — CONFIG_SPI_FLASH_DANGEROUS_WRITE:
 *   The factory erases the backup via esp_flash_erase_region(NULL, 0xB000, ...).
 *   With the default ABORTS, any address before the 1st partition (0xD000) is
 *   "dangerous" -> abort(). The factory's sdkconfig.defaults MUST have
 *   CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED=y. (already in place)
 *
 * MSF layout (16 MB, ADR-007):
 *   0x1000       Bootloader
 *   ~0x7000      End of S3 bootloader (to be measured — margin up to 0xB000)
 *   0xB000       <- OTADATA BACKUP (this sector)
 *   0xC000       Partition table
 *   0xD000       NVS (1st partition)
 *
 * Backup sector content:
 *   [0x000] otadata entry 1 (32 bytes used, rest 0xFF)
 *   [0x020] otadata entry 2 (32 bytes used, rest 0xFF)
 *   [0x040] magic: 0xAA55AA55 (valid backup)
 *
 * SHARED CONTRACT: these values MUST be identical in hooks.c and
 * in the factory app's main.c.
 */
#define OTADATA_OFFSET          0x10000
#define OTADATA_SECTOR_1        (OTADATA_OFFSET / FLASH_SECTOR_SIZE)
#define OTADATA_SECTOR_2        ((OTADATA_OFFSET + FLASH_SECTOR_SIZE) / FLASH_SECTOR_SIZE)

#define OTADATA_BACKUP_OFFSET   0xB000
#define OTADATA_BACKUP_SECTOR   (OTADATA_BACKUP_OFFSET / FLASH_SECTOR_SIZE)
#define OTADATA_ENTRY_SIZE      32
#define BACKUP_MAGIC_OFFSET     0x40
#define BACKUP_MAGIC            0xAA55AA55

/* UX BUGFIX 2026-09-16: the old logic required RELEASING the button
 * within a short window after detection, otherwise cancel ("held too
 * long"). No feedback (visual/audio) exists during boot to know when
 * this window starts -> impossible to time a precise release in real
 * use. Inverted: Power held continuously UNTIL the threshold triggers
 * the switch automatically (no need to release at a precise instant);
 * releasing before the threshold = cancel.
 *
 * Final threshold (2026-09-16, ADR-009 amended): 10s, aligned with the
 * software threshold on the awake app side (power_mgr_switch_to_factory) —
 * same gesture ("hold Power 10s") regardless of the starting state (awake or asleep). */
#define CONFIRM_HOLD_US      (10 * 1000000)

/* BUGFIX 2026-09-16: is_button_pressed() already blocks for ~25ms internally
 * (5 reads x 5ms). The confirmation loop was ADDING an artificial 10ms
 * delay on top (POLL_INTERVAL_US) while counting ONLY those 10ms into
 * held_us — the real time elapsed per iteration (~35ms) was thus
 * underestimated by a factor of ~3.5x. Concrete consequence: for
 * held_us to reach CONFIRM_HOLD_US (10s), the button actually had to be
 * held for ~35s. Fix: count the time actually elapsed (the internal
 * duration of is_button_pressed()), with no extra delay. */
#define BUTTON_SAMPLE_US    (5 * 5000)  /* actual duration of is_button_pressed() */

/* -----------------------------------------------------------------------
 * Button (active-LOW, software debounce)
 * ----------------------------------------------------------------------- */

static bool is_button_pressed(gpio_num_t pin)
{
    int pressed_count = 0;
    for (int i = 0; i < 5; i++) {
        if (gpio_ll_get_level(&GPIO, pin) == 0) {
            pressed_count++;
        }
        esp_rom_delay_us(5000);
    }
    return pressed_count >= 3;
}

/* BUGFIX 2026-09-16 (root cause of "no more wake-up or switch to factory
 * at all, even after 12s"): CONFIG_BOOTLOADER_WDT_ENABLE arms the RTC WDT
 * (RWDT) with a single CONFIG_BOOTLOADER_WDT_TIME_MS = 9000ms timeout
 * BEFORE the call to bootloader_after_init() (bootloader_init.c, action
 * WDT_STAGE_ACTION_RESET_RTC). Our confirmation loop DELIBERATELY blocks
 * until CONFIRM_HOLD_US = 10s WITHOUT ever feeding this watchdog -> it
 * fires at 9s, RESETS the chip BEFORE reaching the threshold, which
 * reboots... into the same hook, which re-arms the same WDT for a
 * new 9s-max cycle, etc. As long as the user keeps holding the
 * button, the 10s threshold can therefore NEVER be reached: a silent
 * reset loop (the screen is never refreshed at this stage, no app code
 * is running yet) that looks from the outside like a total hang.
 * Fix: explicitly feed the RWDT on every poll iteration, exactly like
 * bootloader_support/src/flash_encryption/flash_encrypt.c does for its
 * own long operations at the same stage. */
static void feed_bootloader_wdt(void)
{
    wdt_hal_context_t rwdt_ctx = RWDT_HAL_CONTEXT_DEFAULT();
    wdt_hal_write_protect_disable(&rwdt_ctx);
    wdt_hal_feed(&rwdt_ctx);
    wdt_hal_write_protect_enable(&rwdt_ctx);
}

/* -----------------------------------------------------------------------
 * Backup + erase otadata
 * ----------------------------------------------------------------------- */

static void backup_and_erase_otadata(void)
{
    uint8_t buf[FLASH_SECTOR_SIZE];
    uint8_t entry1[OTADATA_ENTRY_SIZE];
    uint8_t entry2[OTADATA_ENTRY_SIZE];
    uint32_t magic = BACKUP_MAGIC;

    if (esp_rom_spiflash_read(OTADATA_OFFSET, (uint32_t *)entry1,
                              OTADATA_ENTRY_SIZE) != ESP_ROM_SPIFLASH_RESULT_OK) {
        HOOK_LOGE(TAG, "could not read otadata sector 1");
        return;
    }
    if (esp_rom_spiflash_read(OTADATA_OFFSET + FLASH_SECTOR_SIZE,
                              (uint32_t *)entry2,
                              OTADATA_ENTRY_SIZE) != ESP_ROM_SPIFLASH_RESULT_OK) {
        HOOK_LOGE(TAG, "could not read otadata sector 2");
        return;
    }

    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, entry1, OTADATA_ENTRY_SIZE);
    memcpy(buf + OTADATA_ENTRY_SIZE, entry2, OTADATA_ENTRY_SIZE);
    memcpy(buf + BACKUP_MAGIC_OFFSET, &magic, sizeof(magic));

    if (esp_rom_spiflash_erase_sector(OTADATA_BACKUP_SECTOR) != ESP_ROM_SPIFLASH_RESULT_OK) {
        HOOK_LOGE(TAG, "could not erase backup sector");
        return;
    }
    if (esp_rom_spiflash_write(OTADATA_BACKUP_OFFSET, (uint32_t *)buf,
                               FLASH_SECTOR_SIZE) != ESP_ROM_SPIFLASH_RESULT_OK) {
        HOOK_LOGE(TAG, "could not write backup");
        return;
    }

    HOOK_LOGI(TAG, "otadata backed up @0x%x", OTADATA_BACKUP_OFFSET);

    if (esp_rom_spiflash_erase_sector(OTADATA_SECTOR_1) != ESP_ROM_SPIFLASH_RESULT_OK ||
        esp_rom_spiflash_erase_sector(OTADATA_SECTOR_2) != ESP_ROM_SPIFLASH_RESULT_OK) {
        HOOK_LOGE(TAG, "could not erase otadata");
        return;
    }

    HOOK_LOGI(TAG, "otadata erased — rebooting to factory");
}

/* -----------------------------------------------------------------------
 * Bootloader hooks
 * ----------------------------------------------------------------------- */

void bootloader_hooks_include(void) {}

void bootloader_before_init(void)
{
    /* Reserved for future use */
}

void bootloader_after_init(void)
{
    /* BUGFIX 2026-09-16 (root cause identified): when RECOVERY_BUTTON_PIN
     * was Right (GPIO7, never used as a wake source), reading it as raw
     * digital here was risk-free. Since the move to Power (GPIO3,
     * ADR-009 amended) — which is precisely the EXT1 pin that woke the
     * chip for EVERY deep sleep wake-up — esp_sleep_enable_ext1_wakeup()
     * routes this pad through the RTC_IO domain for the duration of
     * sleep, and this configuration persists across the wake-up: reading
     * GPIO3 via the digital GPIO matrix (gpio_ll_get_level) without first
     * handing it back to digital on the RTC_IO side returned a frozen
     * value, independent of the button's real state (no more wake-up or
     * switch to factory possible at all, observed on hardware). Fix:
     * rtcio_ll_function_select(...,
     * RTCIO_LL_FUNC_DIGITAL) — the low-level (register) equivalent of
     * rtc_gpio_deinit(), usable here because rtc_io_ll.h is an "inline
     * register" HAL header with no driver/FreeRTOS dependency — hands it
     * back to digital BEFORE any read. On S3, rtcio_num == gpio_num (offset 0).
     * On a "cold" reset (never routed through RTC), this call is a no-op with
     * no side effect. */
    rtcio_ll_function_select(RECOVERY_BUTTON_PIN, RTCIO_LL_FUNC_DIGITAL);

    /* User request 2026-09-16: the switch to factory must remain
     * controllable at the bootloader level, independent of the app's
     * state (crashed/frozen app = always recoverable). Sole exception: a
     * software reset (RESET_REASON_CORE_SW, esp_restart) triggered by a
     * path that has ALREADY set otadata itself
     * (power_mgr_switch_to_factory() on the app side, action_cancel() on
     * the factory side) — re-reading the button here would add a 2nd
     * confirmation delay (up to CONFIRM_HOLD_US) that is useless and
     * confusing, without changing the boot destination. */
    soc_reset_reason_t reset_reason = esp_rom_get_reset_reason(0);
    if (reset_reason == RESET_REASON_CORE_SW) {
        return;
    }

    /* No buzzer on the X4 Pro: no output init, just the button. */
    esp_rom_gpio_pad_select_gpio(RECOVERY_BUTTON_PIN);
    gpio_ll_input_enable(&GPIO, RECOVERY_BUTTON_PIN);
    gpio_ll_pullup_en(&GPIO, RECOVERY_BUTTON_PIN);

    feed_bootloader_wdt();
    esp_rom_delay_us(100000);  /* 100ms: pull-up stabilization */

    if (!is_button_pressed(RECOVERY_BUTTON_PIN)) {
        return;  /* Normal boot, nothing changed */
    }

    HOOK_LOGI(TAG, "POWER (GPIO3) pressed — hold %u ms to confirm...",
              (unsigned)(CONFIRM_HOLD_US / 1000));

    uint32_t held_us = 0;
    uint32_t next_log_us = 1000000;   /* 2026-09-16 diagnostic: progress every ~1s */
    while (is_button_pressed(RECOVERY_BUTTON_PIN) && held_us < CONFIRM_HOLD_US) {
        feed_bootloader_wdt();   /* see feed_bootloader_wdt(): avoids the RWDT reset at 9s */
        held_us += BUTTON_SAMPLE_US;   /* see BUTTON_SAMPLE_US: counts real time */
        if (held_us >= next_log_us) {
            HOOK_LOGI(TAG, "... held %u ms", (unsigned)(held_us / 1000));
            next_log_us += 1000000;
        }
    }

    if (held_us < CONFIRM_HOLD_US) {
        HOOK_LOGW(TAG, "released too soon (%u ms) — recovery cancelled",
                  (unsigned)(held_us / 1000));
        return;
    }

    HOOK_LOGI(TAG, "threshold reached — switching to factory");
    backup_and_erase_otadata();

    HOOK_LOGI(TAG, "software reset -> bootloader -> factory partition");

    esp_rom_delay_us(200000);
    esp_rom_software_reset_system();
    while (1) { }  /* Never reached */
}
