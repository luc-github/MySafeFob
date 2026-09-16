/**
 * @file splash.cpp
 * @brief MySafeFob App — écran de boot + écran de veille (board x4pro),
 *        via FreeInkUI::DisplayTarget (ADR-010 amendé — copie figée
 *        components/freeinkui/, indépendante de celle de la factory).
 *
 * Réutilise le driver eink.c éprouvé (mêmes séquences UC8279 que la
 * factory). Remplace l'ancien rendu texte maison (font8x16 + put_pixel) —
 * retour utilisateur 2026-09-16 : les deux écrans étaient illisibles
 * (police trop petite, mise en page dense sans hiérarchie visuelle).
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
    /* Même politique que la factory au boot : périphériques ON (pull-ups
     * I2C/SPI), touch OFF et SD OFF — le BSP complet (8.4) reprendra la
     * gestion fine. Active-low : HIGH = OFF pour touch et SD. */
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
    gpio_set_level(RAIL_PERIPH_PIN, 1);   /* periph ON, tenu HIGH */
    gpio_set_level(RAIL_TOUCH_PIN, 1);    /* touch rail OFF (économie) */
    gpio_set_level(RAIL_SD_PIN, 1);       /* SD OFF */
}

extern "C" void board_splash_show(void)
{
    rails_for_splash();

    if (eink_init() != ESP_OK) {
        ESP_LOGE(TAG, "e-ink init KO — pas de splash (USB console active)");
        return;
    }

    memset(s_fb, 0xFF, sizeof(s_fb));

    /* Même bitmap que la factory (resources/splash.png) : continuité
     * visuelle factory -> app au démarrage (demande 2026-09-15). */
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
        ESP_LOGE(TAG, "refresh e-ink KO");
        return;
    }
    /* PAS de eink_power_off() ici (retire 2026-09-16) : board_ready_show()
     * redessine juste apres, dans la MEME session — un POF suivi d'un PON
     * quasi immediat n'a jamais ete valide sur ce hardware (dans la
     * factory, le POF arrive toujours juste avant un esp_restart() reel,
     * jamais suivi d'un redessin). Laisser l'ecran allume entre les deux
     * reprend le pattern deja eprouve (plusieurs eink_display_fb() a la
     * suite sans coupure — navigation menu factory, refresh ambiant 45 s).
     * Le POF final revient a board_ready_show(), le veritable ecran de
     * repos de cette sequence. */
    ESP_LOGI(TAG, "splash affiché");
}

extern "C" void board_ready_show(void)
{
    rails_for_splash();

    if (eink_init() != ESP_OK) {
        ESP_LOGE(TAG, "e-ink init KO — pas d'écran prêt");
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

    /* Meme langage visuel que board_sleep_screen_show() : bloc invers
     * plutot qu'une police differente (un seul font bitmap embarque). */
    const Rect box{60, 340, static_cast<int16_t>(w - 120), 70};
    target.fill(box, Paint::solid(Color::Black));
    TextStyle inverted;
    inverted.align = TextAlign::Center;
    inverted.color = Color::White;
    target.text(box, "PRET", inverted);

    target.line(Point{60, 460}, Point{static_cast<int16_t>(w - 60), 460}, 1,
               Paint::solid(Color::Black));

    TextStyle hint;
    hint.align = TextAlign::Center;
    target.text(Rect{0, 490, w, 40}, "Console USB active", hint);
    target.text(Rect{0, 530, w, 40}, "Power (long) = Veille", hint);

    if (eink_display_fb(s_fb) != ESP_OK) {
        ESP_LOGE(TAG, "refresh e-ink KO");
        return;
    }
    eink_power_off();
    ESP_LOGI(TAG, "ecran pret affiche, controleur e-ink en POF");
}

extern "C" void board_sleep_screen_show(void)
{
    rails_for_splash();

    if (eink_init() != ESP_OK) {
        /* Deep sleep must not be blocked by a display failure — the
         * caller proceeds to power_mgr_shutdown() regardless. */
        ESP_LOGE(TAG, "e-ink init KO — sleep screen skipped, sleeping anyway");
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

    /* "EN VEILLE" en video inversee — meme langage visuel que la selection
     * de menu factory — plutot que de compter sur une taille de police
     * differente (un seul font bitmap embarque, meme taille partout). */
    const Rect box{60, 340, static_cast<int16_t>(w - 120), 70};
    target.fill(box, Paint::solid(Color::Black));
    TextStyle inverted;
    inverted.align = TextAlign::Center;
    inverted.color = Color::White;
    target.text(box, "EN VEILLE", inverted);

    target.line(Point{60, 460}, Point{static_cast<int16_t>(w - 60), 460}, 1,
               Paint::solid(Color::Black));

    TextStyle hint;
    hint.align = TextAlign::Center;
    target.text(Rect{0, 490, w, 40}, "Power = Reveil", hint);

    /* Battery indicator + contact-if-lost: task 8c (CW2017, opt-in).
     * Factory-rescue combo intentionally NOT shown (DECISIONS §16). */

    if (eink_display_fb(s_fb) != ESP_OK) {
        ESP_LOGE(TAG, "refresh e-ink KO — sleeping anyway");
        return;
    }
    eink_power_off();
    ESP_LOGI(TAG, "sleep screen drawn, controller in POF");
}
