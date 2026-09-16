/**
 * @file buttons.c
 * @brief MySafeFob Factory — boutons physiques X4 Pro (polling + debounce).
 */
#include "buttons.h"

#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"

void buttons_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BTN_LEFT_PIN) | (1ULL << BTN_RIGHT_PIN) |
                        (1ULL << BTN_POWER_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

static button_id_t read_pressed(void)
{
    if (gpio_get_level(BTN_LEFT_PIN) == 0)  return BTN_1;
    if (gpio_get_level(BTN_RIGHT_PIN) == 0) return BTN_2;
    if (gpio_get_level(BTN_POWER_PIN) == 0) return BTN_3;
    return BTN_NONE;
}

button_id_t button_wait_press(int timeout_ms)
{
    int waited = 0;
    while (1) {
        button_id_t b = read_pressed();
        if (b != BTN_NONE) {
            vTaskDelay(pdMS_TO_TICKS(30));          /* debounce */
            if (read_pressed() == b) {
                while (read_pressed() == b) {       /* attend le relachement */
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                return b;
            }
        }
        if (timeout_ms > 0) {
            waited += 10;
            if (waited >= timeout_ms) return BTN_NONE;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
