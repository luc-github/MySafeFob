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
 * @brief MySafeFob App — boot screen + sleep screen (board x4pro),
 *        via FreeInkUI::DisplayTarget (ADR-010 amended — frozen copy
 *        components/freeinkui/, independent from the factory's).
 *
 * Reuses the proven eink.c driver (same UC8279 sequences as the
 * factory). Replaces the old homemade text rendering (font8x16 + put_pixel) —
 * user feedback 2026-09-16: both screens were unreadable
 * (font too small, dense layout with no visual hierarchy).
 */
#include "splash.h"

extern "C" {
#include "eink.h"
#include "hw_config.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
}

#include "splash_bitmap.h"

#include <cstring>

#include <FreeInkUIDisplayTarget.h>

using namespace freeink::ui;

static const char *TAG = "SPLASH";
static uint8_t s_fb[SCREEN_FB_SIZE];

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
    DisplayTarget target(s_fb, EINK_W, EINK_H, EINK_WB, Orientation::Portrait);
    BitmapRef bmp;
    bmp.data = splash_bits;
    bmp.width = SPLASH_W;
    bmp.height = SPLASH_H;
    bmp.format = BitmapFormat::BW1;
    bmp.progmem = false;
    target.bitmap(Rect{0, 0, target.logicalWidth(), target.logicalHeight()},
                  bmp, BitmapMode::Contain);

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
    rails_for_splash();

    if (eink_init() != ESP_OK) {
        /* Deep sleep must not be blocked by a display failure — the
         * caller proceeds to power_mgr_shutdown() regardless. */
        ESP_LOGE(TAG, "e-ink init FAILED — sleep screen skipped, sleeping anyway");
        return;
    }

    memset(s_fb, 0xFF, sizeof(s_fb));

    DisplayTarget target(s_fb, EINK_W, EINK_H, EINK_WB, Orientation::Portrait);
    const int16_t w = target.logicalWidth();

    TextStyle title;
    title.align = TextAlign::Center;
    target.text(Rect{0, 60, w, 40}, "MySafeFob", title);
    target.line(Point{60, 120}, Point{static_cast<int16_t>(w - 60), 120}, 1,
               Paint::solid(Color::Black));

    /* "ASLEEP" in inverted video — same visual language as the factory
     * menu selection — rather than relying on a different font size (only
     * one bitmap font embedded, same size everywhere). */
    const Rect box{60, 340, static_cast<int16_t>(w - 120), 70};
    target.fill(box, Paint::solid(Color::Black));
    TextStyle inverted;
    inverted.align = TextAlign::Center;
    inverted.color = Color::White;
    target.text(box, "ASLEEP", inverted);

    target.line(Point{60, 460}, Point{static_cast<int16_t>(w - 60), 460}, 1,
               Paint::solid(Color::Black));

    TextStyle hint;
    hint.align = TextAlign::Center;
    target.text(Rect{0, 490, w, 40}, "Power = Wake", hint);

    /* Battery indicator + contact-if-lost: task 8c (CW2017, opt-in).
     * Factory-rescue combo intentionally NOT shown (DECISIONS §16). */

    if (eink_display_fb(s_fb) != ESP_OK) {
        ESP_LOGE(TAG, "e-ink refresh FAILED — sleeping anyway");
        return;
    }
    eink_power_off();
    rails_hold_for_sleep();
    ESP_LOGI(TAG, "sleep screen drawn, controller in POF, rails held for sleep");
}
