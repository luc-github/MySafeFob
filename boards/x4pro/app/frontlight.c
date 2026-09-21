/*
 Project: MySafeFob  frontlight.c
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
#include "frontlight.h"
#include "hw_config.h"

#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "frontlight";

#define FRONTLIGHT_DUTY_MAX ((1 << LEDC_TIMER_10_BIT) - 1)   /* 1023 */

esp_err_t frontlight_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = FRONTLIGHT_LEDC_MODE,
        .timer_num = FRONTLIGHT_LEDC_TIMER,
        .duty_resolution = FRONTLIGHT_LEDC_RES,
        .freq_hz = FRONTLIGHT_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config FAILED (%s)", esp_err_to_name(err));
        return err;
    }

    const struct {
        ledc_channel_t channel;
        gpio_num_t gpio;
    } channels[] = {
        {FRONTLIGHT_COOL_CHANNEL, FRONTLIGHT_COOL_PIN},
        {FRONTLIGHT_WARM_CHANNEL, FRONTLIGHT_WARM_PIN},
    };
    for (size_t i = 0; i < sizeof(channels) / sizeof(channels[0]); i++) {
        ledc_channel_config_t ch_cfg = {
            .gpio_num = channels[i].gpio,
            .speed_mode = FRONTLIGHT_LEDC_MODE,
            .channel = channels[i].channel,
            .timer_sel = FRONTLIGHT_LEDC_TIMER,
            .duty = 0,
            .hpoint = 0,
        };
        err = ledc_channel_config(&ch_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ledc_channel_config FAILED (%s)", esp_err_to_name(err));
            return err;
        }
    }

    return ESP_OK;
}

static void set_channel_duty(ledc_channel_t channel, uint32_t duty)
{
    ledc_set_duty(FRONTLIGHT_LEDC_MODE, channel, duty);
    ledc_update_duty(FRONTLIGHT_LEDC_MODE, channel);
}

void frontlight_apply(bool on, uint8_t color_mix_pct, uint8_t intensity_pct)
{
    if (intensity_pct > 100) {
        intensity_pct = 100;
    }
    if (color_mix_pct > 100) {
        color_mix_pct = 100;
    }

    if (!on) {
        set_channel_duty(FRONTLIGHT_COOL_CHANNEL, 0);
        set_channel_duty(FRONTLIGHT_WARM_CHANNEL, 0);
        return;
    }

    /* Plain linear crossfade -- warm_pct + cool_pct always sum to 100,
     * each then scaled by the overall intensity. Not photometrically
     * corrected (the two LEDs may not be equally bright at the same
     * duty), see the file header. */
    const uint32_t cool_pct = color_mix_pct;
    const uint32_t warm_pct = 100 - color_mix_pct;
    const uint32_t cool_duty = cool_pct * intensity_pct * FRONTLIGHT_DUTY_MAX / (100u * 100u);
    const uint32_t warm_duty = warm_pct * intensity_pct * FRONTLIGHT_DUTY_MAX / (100u * 100u);

    set_channel_duty(FRONTLIGHT_COOL_CHANNEL, cool_duty);
    set_channel_duty(FRONTLIGHT_WARM_CHANNEL, warm_duty);
}

void frontlight_off(void)
{
    set_channel_duty(FRONTLIGHT_COOL_CHANNEL, 0);
    set_channel_duty(FRONTLIGHT_WARM_CHANNEL, 0);
}
