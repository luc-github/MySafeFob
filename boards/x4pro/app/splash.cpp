/* 
 Project: MySafeFob  splash.cpp
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
 * @file splash.cpp
 * @brief MySafeFob App — boot screen + sleep screen (board x4pro).
 *
 * board_splash_show() runs before LVGL is initialized (called from
 * app_main(), ahead of board_ui_nav_task) and stays LVGL-free: it blits
 * the pre-rendered splash bitmap straight into eink.c's native
 * framebuffer using the same portrait->landscape transpose convention
 * documented in hw_config.h (fb_x = uy, fb_y = 479 - ux) that
 * FreeInkUIDisplayTarget used to apply internally (ADR-010 amended again
 * — LVGL replaced FreeInkUI, docs/ROADMAP.md).
 *
 * board_sleep_screen_show() runs after board_ui_nav_task has started, but
 * stays LVGL-free too (2026-09-22 request: replace the sleep screen with
 * resources/sleep.png outright) -- it calls ui_nav_suspend_lvgl_for_sleep()
 * first (LVGL is not thread-safe: this stops board_ui_nav_task's own loop
 * from touching it concurrently with the blit below), then blits the sleep
 * bitmap the same way board_splash_show() blits the boot splash.
 */
#include "splash.h"
#include "ui_nav.h"

extern "C" {
#include "eink.h"
#include "hw_config.h"
#include "frontlight.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_timer.h"
}

#include "splash_bitmap.h"
#include "sleep_bitmap.h"

#include <cstdio>
#include <cstring>

/* WORKAROUND (2026-09-18, see touch.c's twin comment): standard ESP_LOG*
 * calls from boards/x4pro/app never reach the serial monitor, 100%
 * reproducible -- a raw printf() from the same call site always works.
 * Scoped to this translation unit only; revert once the real cause is
 * found. */
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

static const char *TAG = "SPLASH";
static uint8_t s_fb[SCREEN_FB_SIZE];

/* Splash bitmap is pre-rendered portrait (SPLASH_W=480, SPLASH_H=800,
 * SPLASH_STRIDE=60 bytes/row, MSB-first, 1=ink/black) by
 * tools/gen_splash.py. Same transpose FreeInkUIDisplayTarget's
 * Orientation::Portrait used to apply per-pixel: fb_x = uy, fb_y = 479 - ux
 * into the native landscape fb (EINK_WB=100 bytes/row). Only ink (black)
 * bits are written — s_fb is memset to white (0xFF) first. */
static void blit_splash_to_fb(uint8_t *fb)
{
    for (int uy = 0; uy < SPLASH_H; uy++) {
        for (int ux = 0; ux < SPLASH_W; ux++) {
            const uint8_t src_byte = splash_bits[uy * SPLASH_STRIDE + (ux >> 3)];
            const bool ink = (src_byte >> (7 - (ux & 7))) & 1;
            if (!ink) {
                continue;
            }
            const int fb_x = uy;
            const int fb_y = SPLASH_W - 1 - ux;
            uint8_t *byte = &fb[fb_y * EINK_WB + (fb_x >> 3)];
            *byte &= static_cast<uint8_t>(~(0x80 >> (fb_x & 7)));
        }
    }
}

/* Same format/transpose as blit_splash_to_fb() above, just a different
 * source bitmap (sleep_bitmap.h, tools/gen_sleep.py from
 * resources/sleep.png) for the deep sleep screen. */
static void blit_sleep_to_fb(uint8_t *fb)
{
    for (int uy = 0; uy < SLEEP_H; uy++) {
        for (int ux = 0; ux < SLEEP_W; ux++) {
            const uint8_t src_byte = sleep_bits[uy * SLEEP_STRIDE + (ux >> 3)];
            const bool ink = (src_byte >> (7 - (ux & 7))) & 1;
            if (!ink) {
                continue;
            }
            const int fb_x = uy;
            const int fb_y = SLEEP_W - 1 - ux;
            uint8_t *byte = &fb[fb_y * EINK_WB + (fb_x >> 3)];
            *byte &= static_cast<uint8_t>(~(0x80 >> (fb_x & 7)));
        }
    }
}

static void rails_for_splash(void)
{
    /* Undo any RTC hold left by rails_hold_for_sleep() from a previous
     * deep sleep (task 8c): rtc_gpio_hold_en() latches the pad and
     * survives the deep-sleep reset — without releasing it first, the
     * gpio_config()/gpio_set_level() calls below would silently have no
     * effect on these three pins. No-op if the pins were never held
     * (cold boot). */
    rtc_gpio_hold_dis(RAIL_PERIPH_PIN);
    rtc_gpio_hold_dis(RAIL_TOUCH_PIN);
    rtc_gpio_hold_dis(RAIL_SD_PIN);

    /* Same policy as the factory at boot: peripherals ON (I2C/SPI
     * pull-ups), touch OFF and SD OFF — the full BSP (8.4) will take over
     * fine-grained management. Active-low: HIGH = OFF for touch and SD. */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << RAIL_PERIPH_PIN) |
                        (1ULL << RAIL_TOUCH_PIN)  |
                        (1ULL << RAIL_SD_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(RAIL_PERIPH_PIN, 1);   /* periph ON, held HIGH */
    gpio_set_level(RAIL_TOUCH_PIN, 1);    /* touch rail OFF (power saving) */
    gpio_set_level(RAIL_SD_PIN, 1);       /* SD OFF */
}

/* Task 8c (ADR-009 pt.4): latch the rail pins through the RTC domain
 * before deep sleep. The digital domain powers down in deep sleep, so a
 * plain gpio_set_level() would leave these pads floating for the whole
 * sleep duration — on an active-LOW rail (touch/SD), a floating pad can
 * silently re-enable the rail and defeat the whole point of turning it
 * off first. rtc_gpio_hold_en() keeps the pad driven at its last level by
 * the RTC domain (which stays powered) until rails_for_splash() releases
 * it again on the next boot. */
static void rails_hold_for_sleep(void)
{
    rtc_gpio_init(RAIL_TOUCH_PIN);
    rtc_gpio_set_direction(RAIL_TOUCH_PIN, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(RAIL_TOUCH_PIN, 1);
    rtc_gpio_hold_en(RAIL_TOUCH_PIN);

    rtc_gpio_init(RAIL_SD_PIN);
    rtc_gpio_set_direction(RAIL_SD_PIN, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(RAIL_SD_PIN, 1);
    rtc_gpio_hold_en(RAIL_SD_PIN);

    rtc_gpio_init(RAIL_PERIPH_PIN);
    rtc_gpio_set_direction(RAIL_PERIPH_PIN, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(RAIL_PERIPH_PIN, 1);
    rtc_gpio_hold_en(RAIL_PERIPH_PIN);
}

extern "C" void board_splash_show(void)
{
    rails_for_splash();

    if (eink_init() != ESP_OK) {
        ESP_LOGE(TAG, "e-ink init FAILED — no splash (USB console active)");
        return;
    }

    memset(s_fb, 0xFF, sizeof(s_fb));

    /* Same bitmap as the factory (resources/splash.png): visual
     * continuity factory -> app at startup (2026-09-15 request). */
    blit_splash_to_fb(s_fb);

    if (eink_display_fb(s_fb) != ESP_OK) {
        ESP_LOGE(TAG, "e-ink refresh FAILED");
        return;
    }
    /* NO eink_power_off() here (removed 2026-09-16): board_ui_nav_task
     * (ui_nav.cpp, task 8.4) redraws its menu right after, in the SAME
     * session — a POF followed by an almost immediate PON has never been
     * validated on this hardware (in the factory, POF always happens
     * right before a real esp_restart(), never followed by a redraw).
     * Leaving the screen on between the two follows the pattern already
     * proven elsewhere (several eink_display_fb() calls in a row with no
     * power cut — factory menu navigation, 45s ambient refresh). The
     * screen then stays powered for the whole interactive session — POF
     * only happens via board_sleep_screen_show() below, right before
     * power_mgr_shutdown(). */
    ESP_LOGI(TAG, "splash displayed");
}

extern "C" void board_sleep_screen_show(void)
{
    /* First thing, before touching eink.c/s_fb below: board_ui_nav_task
     * (a different task when this is reached via main.c's Power-long-press
     * path) must stop pumping lv_timer_handler() before anything else here
     * runs, LVGL is not thread-safe. */
    ui_nav_suspend_lvgl_for_sleep();

    rails_for_splash();

    if (eink_init() != ESP_OK) {
        /* Deep sleep must not be blocked by a display failure — the
         * caller proceeds to power_mgr_shutdown() regardless. */
        ESP_LOGE(TAG, "e-ink init FAILED — sleep screen skipped, sleeping anyway");
        return;
    }

    /* Blitted straight into the framebuffer, same pre-LVGL path as the
     * boot splash (2026-09-22 request: replace the sleep screen outright
     * with resources/sleep.png) -- this function still owns
     * eink_power_off()/rails_hold_for_sleep() right after, unchanged. */
    memset(s_fb, 0xFF, sizeof(s_fb));
    blit_sleep_to_fb(s_fb);
    if (eink_display_fb(s_fb) != ESP_OK) {
        ESP_LOGE(TAG, "e-ink refresh FAILED (sleep screen)");
    }

    eink_power_off();
    rails_hold_for_sleep();
    /* Guarantee the frontlight is off before deep sleep, regardless of
     * whether ui_nav.cpp's own auto-off already fired -- unlike the
     * touch/SD rails above, no rtc_gpio_hold_en() here: frontlight's off
     * level is duty-0/LOW, the same state an un-driven pad defaults to,
     * not an active-HIGH level that needs to be actively held through
     * sleep. Flagged for hardware validation (watch for flicker/drain)
     * rather than built preemptively. */
    frontlight_off();
    ESP_LOGI(TAG, "sleep screen drawn, controller in POF, rails held for sleep");
}
