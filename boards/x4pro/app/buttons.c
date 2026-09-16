/* 
 Project: MySafeFob  buttons.c
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
 * @file buttons.c
 * @brief MySafeFob App — X4 Pro physical buttons (polling + debounce).
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
                while (read_pressed() == b) {       /* wait for release */
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
