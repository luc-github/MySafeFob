/* 
 Project: MySafeFob  main.c
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
 * @file main.c
 * @brief MySafeFob Recovery (factory) — X4 Pro recovery/bootstrap app.
 *
 * Recovery menu (e-ink, no LVGL — homemade 1 bpp gfx):
 *   Left (GPIO0) = up   Right (GPIO7) = down
 *   Home pad (GT911, validated touch zone) = confirm (OK)
 *   Power (GPIO3) = Cancel: reboot to the default partition
 *
 * Flow (ADR-007):
 *   The bootloader hook backed up otadata @0xB000 then erased it before
 *   jumping here. At startup we restore otadata: a power-off from the
 *   factory returns to the correct OTA partition.
 *
 * Actions:
 *   Boot app0  : esp_ota_set_boot_partition + reboot
 *   SD -> app0 : flash /sdcard/msf-fw.bin via OTA to the single app slot
 *                (ADR-011: app1 removed, no network OTA -> no need for
 *                esp_ota A/B rollback, the factory is the safety net if
 *                the update boots badly)
 *
 * NO "SD -> factory" (removed 2026-09-15, user decision): the factory
 * never self-updates from the SD — it would be writing to the very
 * partition it's running from (XIP), with no recovery net if
 * interrupted mid-flight. Treated like the bootloader: updates only via
 * USB/serial (flash_mgr.py --variant x4pro_factory).
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_flash.h"

#include "hw_config.h"
#include "eink.h"
#include "gfx.h"
#include "buttons.h"
#include "touch.h"
#include "sdcard.h"
#include "factory_log.h"
#include "version.h"
#include "splash.h"
#include "battery.h"
#include "battery_icon.h"
#include "rtc.h"
#include "esp_timer.h"

static const char *TAG = "RECOVERY";

/* Otadata backup — MUST match hooks.c EXACTLY (shared contract).
 * Constraints on the 0xB000 sector: after the bootloader (~0x7000 on S3),
 * before the partition table (0xC000), outside any partition (NVS
 * @0xD000), 4 KB aligned. The factory requires
 * CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED=y (already in factory/sdkconfig.defaults). */
#define OTADATA_OFFSET          0x10000
#define OTADATA_SECTOR_SIZE     0x1000
#define OTADATA_ENTRY_SIZE      32
#define OTADATA_BACKUP_OFFSET   0xB000
#define BACKUP_MAGIC_OFFSET     0x40
#define BACKUP_MAGIC            0xAA55AA55

/* -----------------------------------------------------------------------
 * Restore otadata
 * ----------------------------------------------------------------------- */

static bool restore_otadata_from_backup(void)
{
    uint32_t magic = 0;
    esp_err_t err = esp_flash_read(NULL, &magic,
                                   OTADATA_BACKUP_OFFSET + BACKUP_MAGIC_OFFSET,
                                   sizeof(magic));
    if (err != ESP_OK || magic != BACKUP_MAGIC) {
        FACTORY_LOGD(TAG, "no otadata backup (magic=0x%lx)",
                     (unsigned long)magic);
        return false;
    }

    FACTORY_LOGD(TAG, "otadata backup found, restoring...");
    uint8_t entry1[OTADATA_ENTRY_SIZE];
    uint8_t entry2[OTADATA_ENTRY_SIZE];
    esp_flash_read(NULL, entry1, OTADATA_BACKUP_OFFSET, OTADATA_ENTRY_SIZE);
    esp_flash_read(NULL, entry2, OTADATA_BACKUP_OFFSET + OTADATA_ENTRY_SIZE,
                   OTADATA_ENTRY_SIZE);

    bool entry1_empty = true, entry2_empty = true;
    for (int i = 0; i < OTADATA_ENTRY_SIZE; i++) {
        if (entry1[i] != 0xFF) entry1_empty = false;
        if (entry2[i] != 0xFF) entry2_empty = false;
    }
    if (entry1_empty && entry2_empty) {
        ESP_LOGW(TAG, "empty otadata backup, restore skipped");
        goto clear_backup;
    }

    esp_flash_erase_region(NULL, OTADATA_OFFSET, OTADATA_SECTOR_SIZE);
    esp_flash_erase_region(NULL, OTADATA_OFFSET + OTADATA_SECTOR_SIZE,
                           OTADATA_SECTOR_SIZE);
    if (!entry1_empty) {
        esp_flash_write(NULL, entry1, OTADATA_OFFSET, OTADATA_ENTRY_SIZE);
    }
    if (!entry2_empty) {
        esp_flash_write(NULL, entry2, OTADATA_OFFSET + OTADATA_SECTOR_SIZE,
                        OTADATA_ENTRY_SIZE);
    }
    FACTORY_LOGD(TAG, "otadata restored");

clear_backup:
    esp_flash_erase_region(NULL, OTADATA_BACKUP_OFFSET, OTADATA_SECTOR_SIZE);
    FACTORY_LOGD(TAG, "backup sector erased");
    return true;
}

/* -----------------------------------------------------------------------
 * Partition / boot helpers
 * ----------------------------------------------------------------------- */

/* Active label cached ONCE at boot: calling esp_ota_get_boot_partition() on
 * every redraw (a) spammed the esp_ota_ops log and (b) re-read flash on
 * every navigation. The label cannot change within a session: any action
 * that would change it (boot/flash success) reboots before returning here. */
static char s_active_label[17] = "";

static void cache_active_ota_label(void)
{
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    if (boot == NULL) {
        snprintf(s_active_label, sizeof(s_active_label), "unknown");
    } else if (boot->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        snprintf(s_active_label, sizeof(s_active_label), "factory");
    } else {
        /* Bounded copy with no -Wformat-truncation warning (labels <= 16). */
        size_t n = strnlen(boot->label, sizeof(s_active_label) - 1);
        memcpy(s_active_label, boot->label, n);
        s_active_label[n] = '\0';
    }
}

static void boot_partition(const char *label)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label);
    if (!part) {
        ESP_LOGE(TAG, "partition '%s' not found", label);
        return;
    }
    esp_err_t err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set boot partition: %s", esp_err_to_name(err));
        return;
    }
    /* Diagnostic re-read (2026-09-14 session: otadata turned "invalid"
     * mid-session with no identified cause — we verify what flash
     * actually contains after set_boot, before rebooting). */
    const esp_partition_t *check = esp_ota_get_boot_partition();
    ESP_LOGI(TAG, "boot -> '%s' (otadata re-read: %s)", label,
             check ? check->label : "INVALID");
    eink_power_off();
    esp_restart();
}

/* -----------------------------------------------------------------------
 * Menu / SD state
 * ----------------------------------------------------------------------- */

static bool has_fwfile = false;

static void probe_sd_files(void)
{
    has_fwfile = false;
    if (sdcard_mount() != ESP_OK) return;
    FILE *f = fopen(FW_FILENAME, "rb");
    if (f) { has_fwfile = true; fclose(f); }
    sdcard_unmount();
}

/* -----------------------------------------------------------------------
 * Menu (1 bpp gfx — every interaction = full redraw + full refresh)
 * ----------------------------------------------------------------------- */

#define MENU_MAX_ITEMS  6
#define MENU_START_Y    232
#define MENU_ITEM_H     60          /* x2 font: 32px glyph + margin */
#define MENU_PAD_X      30
#define STATUS_Y        (SCREEN_HEIGHT - 56)

typedef enum {
    MENU_ACTION_BOOT_APP0,
    MENU_ACTION_SD_APP0,
} menu_action_t;

typedef struct {
    const char *label;
    menu_action_t action;
} menu_item_t;

static menu_item_t menu_items[MENU_MAX_ITEMS];
static int menu_count = 0;
static int menu_selected = 0;
static char last_status[60] = "";
static bool last_status_ok = false;

static void draw_menu_impl(bool fast)
{
    gfx_clear(COLOR_WHITE);

    gfx_rect(2, 2, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, COLOR_BLACK);
    gfx_rect(3, 3, SCREEN_WIDTH - 6, SCREEN_HEIGHT - 6, COLOR_BLACK);

    char title[48];
    snprintf(title, sizeof(title), "MySafeFob Recovery v" VERSION_FACTORY);
    gfx_draw_string(GFX_CENTER_X(title), 20, title, COLOR_BLACK, COLOR_WHITE);
    gfx_hline(20, 62, SCREEN_WIDTH - 40, COLOR_BLACK);

    /* Two lines (2026-09-14 request). Line 108: battery/charge (CW2017)
     * replaces "Default: app0" — became tautological since ADR-011 (a
     * single app slot, no comparison left to make). Line 144: date/time
     * (BM8563) replaces the touch T:OK/KO status — removed 2026-09-15
     * (redundant: a dead touch is immediately obvious in use). */
    char info[32];
    snprintf(info, sizeof(info), "Active: %s", s_active_label);
    gfx_draw_string(GFX_CENTER_X(info), 72, info, COLOR_BLACK, COLOR_WHITE);

    /* Battery icon (FreeInkUI batteryIndicator) + percentage as text via
     * gfx_draw_string (same font as everything else — not FreeInkUI's,
     * to stay visually consistent). */
    char batt[8];
    uint8_t soc = 0;
    bool charging = false;
    bool batt_ok = battery_read(&soc, &charging);
    if (batt_ok) {
        snprintf(batt, sizeof(batt), "%u%%", (unsigned)soc);
    } else {
        snprintf(batt, sizeof(batt), "--");
    }
    /* Enlarged icon (36x20 -> 64x28, user feedback 2026-09-16: illegible
     * when small) + an explicit "Charging" text next to it — the small
     * lightning bolt drawn by the component stayed hard to see even
     * enlarged, better not to rely on it alone for the charge state. */
    const int batt_icon_w = 64, batt_icon_h = 28, batt_gap = 14;
    const char *charge_label = (batt_ok && charging) ? "Charging" : "";
    int batt_total_w = GFX_TEXT_WIDTH(batt) + batt_gap + batt_icon_w;
    if (charge_label[0]) batt_total_w += batt_gap + GFX_TEXT_WIDTH(charge_label);
    const int batt_x = (SCREEN_WIDTH - batt_total_w) / 2;
    gfx_draw_string(batt_x, 108, batt, COLOR_BLACK, COLOR_WHITE);
    const int batt_icon_x = batt_x + GFX_TEXT_WIDTH(batt) + batt_gap;
    if (batt_ok) {
        battery_icon_draw(batt_icon_x, 108, batt_icon_w, batt_icon_h, soc, charging);
    }
    if (charge_label[0]) {
        gfx_draw_string(batt_icon_x + batt_icon_w + batt_gap, 108, charge_label,
                        COLOR_BLACK, COLOR_WHITE);
    }

    char dt[24];
    rtc_time_t t;
    if (rtc_read(&t)) {
        snprintf(dt, sizeof(dt), "%02d/%02d %02d:%02d", t.day, t.month, t.hour, t.minute);
    } else {
        snprintf(dt, sizeof(dt), "--/-- --:--");
    }
    gfx_draw_string(GFX_CENTER_X(dt), 144, dt, COLOR_BLACK, COLOR_WHITE);

    /* SD on its own line (2026-09-15 request: used to share the line
     * with the touch T:OK/KO status before it was removed, no more
     * reason to keep it stuck to the date). */
    gfx_draw_string(MENU_PAD_X, 180, "SD:", COLOR_BLACK, COLOR_WHITE);
    gfx_draw_string(MENU_PAD_X + 96, 180, has_fwfile ? "FW" : "-",
                    COLOR_BLACK, COLOR_WHITE);
    gfx_hline(20, 222, SCREEN_WIDTH - 40, COLOR_BLACK);

    for (int i = 0; i < menu_count; i++) {
        int y = MENU_START_Y + i * MENU_ITEM_H;
        if (i == menu_selected) {
            /* selection = inverted video */
            gfx_fill_rect(MENU_PAD_X, y, SCREEN_WIDTH - 2 * MENU_PAD_X,
                          MENU_ITEM_H - 8, COLOR_BLACK);
            gfx_draw_string(MENU_PAD_X + 20, y + 14, menu_items[i].label,
                            COLOR_WHITE, COLOR_BLACK);
        } else {
            gfx_fill_rect(MENU_PAD_X, y, SCREEN_WIDTH - 2 * MENU_PAD_X,
                          MENU_ITEM_H - 8, COLOR_WHITE);
            gfx_draw_string(MENU_PAD_X + 20, y + 14, menu_items[i].label,
                            COLOR_BLACK, COLOR_WHITE);
        }
    }

    if (last_status[0] && !last_status_ok) {
        /* Error: inverted banner overlaid on the menu screen, NOT a
         * separate screen (2026-09-16 request: the message must stay on
         * the menu -> a partial refresh is possible, no dedicated full
         * refresh). */
        const int box_x = 8, box_h = GFX_FONT_H + 16;
        const int box_y = STATUS_Y - 20;
        gfx_fill_rect(box_x, box_y, SCREEN_WIDTH - 2 * box_x, box_h, COLOR_BLACK);
        gfx_draw_string(GFX_CENTER_X(last_status), box_y + 8, last_status,
                        COLOR_WHITE, COLOR_BLACK);
    } else {
        gfx_hline(20, STATUS_Y - 8, SCREEN_WIDTH - 40, COLOR_BLACK);
        const char *footer = last_status[0] ? last_status
                                            : "L/R=nav Home=OK Power=Cancel";
        gfx_draw_string(GFX_CENTER_X(footer), STATUS_Y, footer,
                        COLOR_BLACK, COLOR_WHITE);
    }

    /* Navigation = fast DU (no flash); states/status = clean full GC. */
    if (fast) {
        gfx_flush_fast();
    } else {
        gfx_flush();
    }
}

static void draw_menu(void)
{
    draw_menu_impl(false);
}

static void draw_menu_nav(void)
{
    draw_menu_impl(true);
}

static void set_status(const char *msg, bool ok)
{
    strncpy(last_status, msg, sizeof(last_status) - 1);
    last_status[sizeof(last_status) - 1] = '\0';
    last_status_ok = ok;
    /* Fast refresh (2026-09-16 request): the message stays overlaid on
     * the menu (see draw_menu_impl, inverted banner if !ok), no more need
     * for a dedicated full refresh on every status — eink.c's fast
     * refresh budget falls back to full GC on its own when needed. */
    draw_menu_nav();
}

/* -----------------------------------------------------------------------
 * Progress / results (dedicated screens, refreshed every 10%)
 * ----------------------------------------------------------------------- */

static void draw_progress(const char *title, int percent)
{
    gfx_clear(COLOR_WHITE);
    gfx_rect(2, 2, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, COLOR_BLACK);

    gfx_draw_string(GFX_CENTER_X(title), 120, title, COLOR_BLACK, COLOR_WHITE);
    const char *warn = "Do NOT power off!";
    gfx_draw_string(GFX_CENTER_X(warn), 150, warn, COLOR_BLACK, COLOR_WHITE);

    int bar_x = 100, bar_y = 240, bar_w = SCREEN_WIDTH - 200, bar_h = 50;
    gfx_rect(bar_x, bar_y, bar_w, bar_h, COLOR_BLACK);
    int fill_w = ((bar_w - 4) * percent) / 100;
    if (fill_w > 0) {
        gfx_fill_rect(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, COLOR_BLACK);
    }

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    char pct[16];
    snprintf(pct, sizeof(pct), "%d%%", percent);
    gfx_draw_string(GFX_CENTER_X(pct), bar_y + bar_h + 20, pct,
                    COLOR_BLACK, COLOR_WHITE);
    gfx_flush();
}


/* -----------------------------------------------------------------------
 * Actions
 * ----------------------------------------------------------------------- */

static void action_boot(const char *label)
{
    /* Splash instead of a "Booting app0..." text (2026-09-15 request):
     * it's the same image the app will display at its own startup ->
     * a continuous visual transition instead of a menu->splash->app
     * round trip. */
    splash_show();
    boot_partition(label);
    /* if we come back here: failure (very unlikely, app0 is always
     * present in the partition table) — here, a real error message
     * is warranted. */
    char msg[40];
    snprintf(msg, sizeof(msg), "%s not found!", label);
    set_status(msg, false);
}

/**
 * @brief Flashes /sdcard/msf-fw.bin to app0 (esp_ota_*). The factory
 *        NEVER updates itself from the SD: it would be writing to the
 *        very partition it's running from (XIP) — any mid-flight
 *        interruption (battery cut, bug) would leave it invalid with no
 *        recovery net short of a PC. Treated like the bootloader:
 *        updated only via USB/serial (flash_mgr.py --variant
 *        x4pro_factory), never self-serviced in the field. User decision
 *        2026-09-15.
 */
/* Visible SD/flash error (2026-09-15/16 request): inverted banner
 * overlaid on the menu (draw_menu_impl + last_status_ok=false), not a
 * separate screen — no need for one, there's room on the menu, and it
 * avoids a full-refresh round trip to a dedicated screen and back. */
static void fail_result(const char *msg)
{
    probe_sd_files();
    set_status(msg, false);
}

static void action_sd_flash(const char *target_label)
{
    /* BUGFIX 2026-09-16: "Flashing..." used to fire HERE (immediate full
     * refresh), before even knowing whether the SD was mounted or the
     * file present -> up to 3 full-refresh flashes in a row for a simple
     * validation error (a pointless "in progress" flash, then the error,
     * then back to the menu). Moved to right before esp_ota_begin: only
     * one flash in the error case (the message's own). */
    FACTORY_LOGD(TAG, "SD flash -> %s", target_label);

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, target_label);
    if (!part) {
        fail_result("Target partition missing!");
        return;
    }

    if (sdcard_mount() != ESP_OK) {
        fail_result("No SD card!");
        return;
    }

    FILE *fw = fopen(FW_FILENAME, "rb");
    if (!fw) {
        ESP_LOGE(TAG, "%s missing (%s)", FW_FILENAME, strerror(errno));
        sdcard_unmount();
        fail_result("msf-fw.bin missing from SD!");
        return;
    }
    fseek(fw, 0, SEEK_END);
    long fw_size = ftell(fw);
    fseek(fw, 0, SEEK_SET);
    if (fw_size <= 0 || fw_size > (long)part->size) {
        ESP_LOGE(TAG, "invalid size: %ld (max %lu)", fw_size,
                 (unsigned long)part->size);
        fclose(fw);
        sdcard_unmount();
        fail_result("Invalid firmware size!");
        return;
    }

    char title[40];
    snprintf(title, sizeof(title), "Flash %s <- SD", target_label);

    /* All validations passed: this is the first real flash. */
    set_status("Flashing...", true);

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(part, fw_size, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        fclose(fw);
        sdcard_unmount();
        fail_result("OTA begin FAILED!");
        return;
    }

    uint8_t buf[1024];
    long written = 0;
    int last_shown = -10;
    bool ok = true;

    while (written < fw_size) {
        size_t to_read = sizeof(buf);
        if (written + (long)to_read > fw_size) to_read = fw_size - written;
        if (fread(buf, 1, to_read, fw) == 0) {
            ESP_LOGE(TAG, "read error @%ld", written);
            ok = false;
            break;
        }
        err = esp_ota_write(ota_handle, buf, to_read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "write @%ld: %s", written, esp_err_to_name(err));
            ok = false;
            break;
        }
        written += (long)to_read;
        int percent = (int)((written * 100) / fw_size);
        if (percent - last_shown >= 10 || percent == 100) {
            draw_progress(title, percent);   /* full refresh ~2-4s per step */
            last_shown = percent;
        }
    }
    fclose(fw);

    if (ok) {
        if (esp_ota_end(ota_handle) != ESP_OK) ok = false;
    } else if (ota_handle) {
        esp_ota_abort(ota_handle);
    }

    if (ok) {
        /* In-menu banner (no separate screen, 2026-09-16 request), not
         * inverted (ok=true) — enough time to read it before rebooting. */
        set_status("Flash OK! Rebooting...", true);
        remove(FW_OK_FILENAME);
        rename(FW_FILENAME, FW_OK_FILENAME);
        sdcard_unmount();
        vTaskDelay(pdMS_TO_TICKS(2000));
        boot_partition(target_label);
    }

    remove(FW_BAD_FILENAME);
    rename(FW_FILENAME, FW_BAD_FILENAME);
    sdcard_unmount();
    probe_sd_files();
    set_status("Flash FAILED!", false);
}

static void execute_selected_action(void)
{
    switch (menu_items[menu_selected].action) {
        case MENU_ACTION_BOOT_APP0:    action_boot("app0");    break;
        case MENU_ACTION_SD_APP0:      action_sd_flash("app0");   break;
    }
}

static void action_cancel(void)
{
    /* Power = Cancel (user spec 2026-09-14): reboot to the default
     * partition. otadata has already been restored at boot -> a simple
     * reboot returns to the correct OTA partition (factory if none).
     * No set_status()/redraw here (removed 2026-09-15): the splash is
     * already shown by the main loop as soon as the raw Power press is
     * detected, before we even get here. A second flash to a menu+text
     * screen right before power-off would be redundant and would break
     * the splash's visual continuity as the resting image. */
    ESP_LOGI(TAG, "cancel -> reboot to default partition (%s)",
             s_active_label);
    eink_power_off();
    esp_restart();
}

static void dispatch_button(button_id_t btn)
{
    switch (btn) {
        case BTN_1:
            menu_selected = (menu_selected + menu_count - 1) % menu_count;
            draw_menu_nav();
            break;
        case BTN_2:
            menu_selected = (menu_selected + 1) % menu_count;
            draw_menu_nav();
            break;
        case BTN_3:
            action_cancel();
            break;
        default:
            break;
    }
}

/* Home pad hit-test: the zone is detected in touch_read() (raw values
 * validated by measurement 01:34) — no calibration needed here. */

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

void app_main(void)
{
    factory_log_silence_sd_stack();
    FACTORY_LOGD(TAG, "MySafeFob Recovery starting");

    /* 1. Restore otadata FIRST OF ALL (power-off from the factory =
     *    return to the correct OTA partition). */
    restore_otadata_from_backup();
    cache_active_ota_label();
    ESP_LOGI(TAG, "active partition: %s", s_active_label);

    /* 2. E-ink + gfx */
    esp_err_t err = eink_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "eink init: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(200));
        abort();
    }
    gfx_init();

    /* 2b. Splash (ADR-010 amended): covers the input/SD init time below
     * with a fixed image rather than letting the user watch the menu get
     * flashed onto the screen by its own full refresh — the physical GC
     * flash itself remains unavoidable (ADR-010, e-paper section), only
     * WHAT flashes changes. */
    splash_show();

    /* 3. Inputs */
    buttons_init();
    bool touch_ok = touch_init();
    FACTORY_LOGD(TAG, "touch: %s", touch_ok ? "OK" : "ABSENT");

    /* 4. SD (no more app1 probing: partition removed, ADR-011; no
     * "SD -> factory": the factory never self-updates, see the comment
     * on action_sd_flash()) */
    menu_count = 0;
    menu_items[menu_count++] = (menu_item_t){ "Boot app0", MENU_ACTION_BOOT_APP0 };
    menu_items[menu_count++] = (menu_item_t){ "SD -> app0", MENU_ACTION_SD_APP0 };
    menu_selected = 0;
    probe_sd_files();
    draw_menu();

    /* 5. Loop: physical buttons (Power = Cancel) + Home pad (touch) =
     *    confirm. NB: if touch is DOWN, no more confirming is possible —
     *    touch becomes critical for recovery (diagnosed at boot). */
    bool touch_was_pressed = false;
    /* Ambient battery/charge/date refresh (2026-09-15 request):
     * draw_menu_impl() only re-reads the gauge/RTC on each redraw, so
     * never if the user leaves it alone without interacting. 45s
     * (compromise between 30-60s) rather than 1s: an automatic e-ink
     * redraw every second would make the screen flicker continuously and
     * burn through the fast-refresh budget much faster (bringing back
     * the ghosting already fixed). */
    int64_t last_ambient_refresh = esp_timer_get_time();
    while (1) {
        int64_t now = esp_timer_get_time();
        if (now - last_ambient_refresh > 45LL * 1000000) {
            draw_menu_nav();   /* fast refresh, updates batt/charge/date */
            last_ambient_refresh = now;
        }

        /* Immediate feedback on Power (user request 2026-09-15): no
         * buzzer (ADR-007), and button_wait_press() only returns AFTER
         * release -> without this, nothing signals "got it, you can let
         * go". The splash (full refresh) starts as soon as the raw press
         * is detected (even before debounce): the screen starting to
         * flash IS the release signal, no need to wait for the refresh
         * to finish. Only on Power: Left/Right/Home must not flash on
         * every nav press. */
        if (gpio_get_level(BTN_POWER_PIN) == 0) {
            splash_show();
        }
        dispatch_button(button_wait_press(50));

        if (touch_ok) {
            touch_point_t tp = touch_read();
            if (tp.pressed && !touch_was_pressed) {
                /* DIAG 2026-09-15: nothing showed up when the user
                 * touched the screen -> touch_read() had no logging. We
                 * trace every press edge (edge, not 20Hz spam) to know
                 * whether the GT911 reports points at all, and whether
                 * they fall in the Home zone (rx<70, ry 380-580). */
                ESP_LOGI(TAG, "touch: point (x=%d y=%d) home=%s",
                         tp.x, tp.y, tp.home ? "YES" : "no");
            }
            if (tp.pressed && !touch_was_pressed && tp.home) {
                vTaskDelay(pdMS_TO_TICKS(80));   /* minimal feedback */
                execute_selected_action();
            }
            touch_was_pressed = tp.pressed;
        }
    }
}
