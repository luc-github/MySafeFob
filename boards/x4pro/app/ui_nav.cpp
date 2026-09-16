/* 
 Project: MySafeFob  ui_nav.cpp
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
 * @file ui_nav.cpp
 * @brief MySafeFob App — interactive menu (task 8.4, minimal slice).
 *
 * Pattern lifted verbatim from references/test_apps/freeinkui-poc/main/
 * main.cpp (POC, fully proven on hardware): Frame/InteractionBuffer route()
 * mutates focus AFTER the draw that used it, so every input cycle draws
 * twice — once (thrown away on screen) to register hit-rects and resolve
 * the input, once more with the now-current focus/active state, and only
 * that second draw is flushed to the panel.
 *
 * Also owns the ADR-012 activity manager: idle timeout tracked across
 * this loop's own button/touch events plus board_activity_notify() calls
 * from main.c (Power button presses, REPL commands = "serial activity").
 */
#include "ui_nav.h"

extern "C" {
#include "eink.h"
#include "hw_config.h"
#include "buttons.h"
#include "touch.h"
#include "power_mgr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

#include <atomic>
#include <cstring>

#include <FreeInkUIDisplayTarget.h>

using namespace freeink::ui;

static const char *TAG = "ui_nav";
static uint8_t s_fb[SCREEN_FB_SIZE];

/* Implemented in splash.cpp (boots/redraws the sleep screen before
 * power_mgr_shutdown() — same trampoline as a manual long Power press,
 * main.c's power_button_task). */
extern "C" void board_sleep_screen_show(void);

/* ADR-012: 45 s of no input on any of button/touch/serial. */
#define IDLE_TIMEOUT_MS   45000

static std::atomic<int64_t> s_last_activity_us{0};

void board_activity_notify(void)
{
    s_last_activity_us.store(esp_timer_get_time(), std::memory_order_relaxed);
}

enum : ActionId { ACTION_ABOUT = 1, ACTION_SLEEP_NOW = 2 };

static void enter_sleep_from_idle_or_menu(const char *reason)
{
    if (!power_mgr_claim_terminal_action()) {
        /* Another path (Power long-press, REPL `sleep`) already won the
         * race — nothing to do, it's handling the transition. */
        return;
    }
    ESP_LOGI(TAG, "entering sleep (%s)", reason);
    board_sleep_screen_show();
    power_mgr_shutdown();   /* never returns on success */
}

static void draw_screen(DisplayTarget &target, Frame<8> &frame, const char *result)
{
    memset(s_fb, 0xFF, sizeof(s_fb));

    const Rect screen = frame.screen();

    TextStyle title;
    title.align = TextAlign::Center;
    title.bold = true;
    target.text(Rect{0, 60, screen.width, 40}, "MySafeFob", title);

    ButtonProps about;
    about.label = "About";
    about.action = ACTION_ABOUT;
    button(frame, Rect{40, 300, static_cast<int16_t>(screen.width - 80), 70}, about);

    ButtonProps sleep_now;
    sleep_now.label = "Sleep now";
    sleep_now.action = ACTION_SLEEP_NOW;
    button(frame, Rect{40, 390, static_cast<int16_t>(screen.width - 80), 70}, sleep_now);

    TextStyle footer;
    footer.align = TextAlign::Center;
    target.text(Rect{0, 490, screen.width, 24}, "Left/Right = focus", footer);
    target.text(Rect{0, 520, screen.width, 24}, "Touch Home = confirm", footer);

    if (result) {
        TextStyle r;
        r.align = TextAlign::Center;
        target.text(Rect{0, 580, screen.width, 32}, result, r);
    }
}

void board_ui_nav_task(void *arg)
{
    (void)arg;

    buttons_init();
    bool touch_ok = touch_init();
    ESP_LOGI(TAG, "touch: %s", touch_ok ? "OK" : "ABSENT");

    s_last_activity_us.store(esp_timer_get_time(), std::memory_order_relaxed);

    DisplayTarget target(s_fb, EINK_W, EINK_H, EINK_WB, Orientation::Portrait);
    const DeviceContext device = target.deviceContext();

    static InteractionBuffer<8> interactions;

    const char *result = nullptr;
    bool first = true;
    bool touch_was_pressed = false;

    while (1) {
        InputSnapshot input{};
        bool activity = false;

        /* Left/Right only — Power (BTN_3) stays exclusively owned by
         * power_mgr / power_button_task (main.c), never acted on here. */
        button_id_t btn = button_wait_press(50);
        if (btn == BTN_1) { input.focusPrev = true; activity = true; }
        else if (btn == BTN_2) { input.focusNext = true; activity = true; }
        else if (btn == BTN_3) { activity = true; }   /* still counts as activity */

        if (touch_ok) {
            touch_point_t tp = touch_read();
            if (tp.pressed && !touch_was_pressed) {
                activity = true;
                if (tp.home) input.confirm = true;
            }
            touch_was_pressed = tp.pressed;
        }

        if (activity) {
            board_activity_notify();
        }

        const bool changed = first || input.focusPrev || input.focusNext || input.confirm;
        if (!changed) {
            int64_t idle_ms = (esp_timer_get_time() - s_last_activity_us.load(std::memory_order_relaxed)) / 1000;
            if (idle_ms >= IDLE_TIMEOUT_MS) {
                enter_sleep_from_idle_or_menu("45s idle, no button/touch/serial input");
                /* Only reaches here if another caller already claimed the
                 * transition — avoid re-triggering every 50ms while that
                 * transition is in flight. */
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            continue;
        }

        /* Pass 1 (thrown away on screen, just to register hit-rects):
         * resolves the current input -> updates focused_/active_. */
        Frame<8> frame(target, device, input, interactions);
        draw_screen(target, frame, result);
        ActionEvent ev = frame.finish();

        if (ev) {
            if (ev.action == ACTION_ABOUT) {
                result = "MySafeFob (MSF) - board x4pro";
            } else if (ev.action == ACTION_SLEEP_NOW) {
                enter_sleep_from_idle_or_menu("Sleep now menu item");
                result = "Sleep transition already in progress";
            }
        }

        /* Pass 2: real draw with the now-current focus/active state — the
         * one that actually gets flushed, eliminating the one-frame lag. */
        Frame<8> frame2(target, device, InputSnapshot{}, interactions);
        draw_screen(target, frame2, result);

        if (first) {
            eink_display_fb(s_fb);
            first = false;
        } else {
            eink_display_fb_fast(s_fb);
        }
    }
}
