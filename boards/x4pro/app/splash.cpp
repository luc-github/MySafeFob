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
    /* NO eink_power_off() here (removed 2026-09-16): board_ready_show()
     * redraws right after, in the SAME session — a POF followed by an
     * almost immediate PON has never been validated on this hardware (in
     * the factory, POF always happens right before a real esp_restart(),
     * never followed by a redraw). Leaving the screen on between the two
     * follows the pattern already proven elsewhere (several
     * eink_display_fb() calls in a row with no power cut — factory menu
     * navigation, 45s ambient refresh). The final POF is left to
     * board_ready_show(), the true resting screen of this sequence. */
    ESP_LOGI(TAG, "splash displayed");
}

extern "C" void board_ready_show(void)
{
    rails_for_splash();

    if (eink_init() != ESP_OK) {
        ESP_LOGE(TAG, "e-ink init FAILED — no ready screen");
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

    /* Same visual language as board_sleep_screen_show(): an inverted
     * block rather than a different font (only one bitmap font embedded). */
    const Rect box{60, 340, static_cast<int16_t>(w - 120), 70};
    target.fill(box, Paint::solid(Color::Black));
    TextStyle inverted;
    inverted.align = TextAlign::Center;
    inverted.color = Color::White;
    target.text(box, "READY", inverted);

    target.line(Point{60, 460}, Point{static_cast<int16_t>(w - 60), 460}, 1,
               Paint::solid(Color::Black));

    TextStyle hint;
    hint.align = TextAlign::Center;
    target.text(Rect{0, 490, w, 40}, "USB console active", hint);
    target.text(Rect{0, 530, w, 40}, "Power (long) = Sleep", hint);

    if (eink_display_fb(s_fb) != ESP_OK) {
        ESP_LOGE(TAG, "e-ink refresh FAILED");
        return;
    }
    eink_power_off();
    ESP_LOGI(TAG, "ready screen displayed, e-ink controller in POF");
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
    ESP_LOGI(TAG, "sleep screen drawn, controller in POF");
}
