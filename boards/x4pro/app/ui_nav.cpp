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
 * @brief MySafeFob App — navigation orchestrator: owns the Screen array,
 *        the task loop, and the ADR-012 activity/idle-sleep manager. Each
 *        screen's own widgets live in its own ui_screen_*.cpp (see
 *        ui_screens.h for the build_xxx() contract); generic widget
 *        builders live in ui_widgets.cpp (ADR-010 amended again: LVGL
 *        replaces FreeInkUI — see docs/ROADMAP.md, "one file per screen").
 *
 * Also owns the ADR-012 activity manager: idle timeout tracked across
 * this loop's own button/touch events (via lv_port_indev.c's sampler
 * task, which calls board_activity_notify()) plus main.c's own calls
 * (Power button presses, REPL commands = "serial activity").
 */
#include "ui_nav.h"
#include "ui_screens.h"
#include "ui_widgets.h"
#include "splash.h"
#include "settings_store.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

extern "C" {
#include "hw_config.h"
#include "battery.h"
#include "frontlight.h"
#include "time_service.h"
#include "power_mgr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

#include "app_log_workaround.h"

#include "lvgl.h"

#include <atomic>

static const char *TAG = "ui_nav";

/* Deep sleep is a separate, non-LVGL screen (see ui_nav_suspend_lvgl_for_sleep()
 * further down) -- not one of these 6, not part of normal navigation. */
static lv_obj_t *s_screens[static_cast<int>(Screen::kCount)];
/* One battery/charge status label per screen, refreshed on switch_screen()
 * below (2026-09-21 bug report: the label was only ever set once, when
 * each screen was built at startup -- charging state then stuck forever,
 * e.g. still showing "charging" long after the cable was unplugged). */
static lv_obj_t *s_battery_labels[static_cast<int>(Screen::kCount)];
/* One lv_group per screen (2026-09-21 hardware fix, see lv_port_indev.h) --
 * Left/Right only ever steps through the group of the CURRENTLY visible
 * screen, set active in switch_screen() below. */
static lv_group_t *s_groups[static_cast<int>(Screen::kCount)];
static Screen s_screen = Screen::Home;

/* Set right before any LVGL call made from a task OTHER than
 * board_ui_nav_task (main.c's power_button_task, on a Power long-press
 * that wins the ADR-009/012 sleep race) — board_ui_nav_task's own loop
 * checks this every iteration and stops pumping lv_timer_handler() once
 * it's set, so the two tasks never touch LVGL's object tree concurrently
 * during the brief window before deep sleep actually engages. */
static std::atomic<bool> s_lvgl_suspended{false};

static std::atomic<int64_t> s_last_activity_us{0};

void board_activity_notify(void)
{
    s_last_activity_us.store(esp_timer_get_time(), std::memory_order_relaxed);
}

/* Counters, not flags (2026-09-22, same reasoning as lv_port_indev.c's
 * touch-edge queue): a single bool can only ever represent "one pending",
 * so two presses landing while board_ui_nav_task is busy (mid-flush) would
 * coalesce into one action, silently costing the user a step. Bounded
 * naturally -- lv_port_indev.c's input_sampler_task only increments these
 * on a debounced RELEASE edge (its own button_raw_state/button_debounced
 * tracking, same pattern as its touch debounce), so these can only ever be
 * incremented by genuine distinct presses, never by a held button or a
 * polling artifact. */
static std::atomic<int> s_power_confirm_pending{0};
/* Kept separate from the counter above (2026-09-23) precisely so a screen
 * can ask for touch-Home confirms to be dropped without affecting the
 * Power-button path -- see board_ui_nav_suppress_touch_confirm()'s own
 * doc comment (ui_nav.h) for the hardware bug this closes. */
static std::atomic<int> s_touch_home_confirm_pending{0};
static std::atomic<bool> s_suppress_touch_confirm{false};

void board_ui_nav_power_confirm(bool from_touch_home)
{
    if (from_touch_home) {
        s_touch_home_confirm_pending.fetch_add(1, std::memory_order_relaxed);
    } else {
        s_power_confirm_pending.fetch_add(1, std::memory_order_relaxed);
    }
}

void board_ui_nav_suppress_touch_confirm(bool suppress)
{
    s_suppress_touch_confirm.store(suppress, std::memory_order_relaxed);
}

bool board_ui_nav_is_touch_confirm_suppressed(void)
{
    return s_suppress_touch_confirm.load(std::memory_order_relaxed);
}

/* See ui_nav.h's doc comment: LVGL is not thread-safe -- these are only
 * ever set from lv_port_indev.c's input_sampler_task (a different task)
 * and consumed by board_ui_nav_task's own loop, which is the only task
 * that ever touches LVGL. */
static std::atomic<int> s_focus_prev_pending{0};
static std::atomic<int> s_focus_next_pending{0};

void board_ui_nav_focus_prev(void)
{
    s_focus_prev_pending.fetch_add(1, std::memory_order_relaxed);
}

void board_ui_nav_focus_next(void)
{
    s_focus_next_pending.fetch_add(1, std::memory_order_relaxed);
}

void switch_screen(Screen s)
{
    ESP_LOGI(TAG, "screen: %d -> %d", static_cast<int>(s_screen), static_cast<int>(s));
    s_screen = s;
    refresh_battery_label(s_battery_labels[static_cast<int>(s)]);
    lv_port_disp_request_full_refresh();   /* UI-SPECS.md §1.1: full refresh on screen-type change */
    lv_screen_load(s_screens[static_cast<int>(s)]);
}

void enter_sleep_from_idle_or_menu(const char *reason)
{
    if (!power_mgr_claim_terminal_action()) {
        return;   /* another path already won the race */
    }
    ESP_LOGI(TAG, "entering sleep (%s)", reason);
    board_sleep_screen_show();   /* splash.cpp — calls ui_nav_suspend_lvgl_for_sleep() */
    power_mgr_shutdown();        /* never returns on success */
}

/* 2026-09-21 request: 10s while charging (changes fast enough to be worth
 * seeing live), 30s otherwise (discharge is slow -- no need to spend an
 * e-paper refresh more often than that, "on economise en etant sur
 * batterie"). Runs continuously from board_ui_nav_task's init, independent
 * of which screen is open -- refreshes whichever screen's battery label is
 * CURRENTLY visible (s_screen), same one switch_screen() already targets. */
static void battery_timer_cb(lv_timer_t *timer)
{
    /* No periodic label update on Touch Calibration (2026-09-24 user
     * request): an unrelated flush landing mid-run is exactly the kind of
     * surprise that screen's tap-counting is built to be robust against,
     * and there's nothing to gain from a live battery figure during a
     * ~2-minute one-off procedure. The label is still refreshed on entry
     * (switch_screen()). */
    if (s_screen == Screen::TouchDiag) {
        return;
    }
    bool charging = refresh_battery_label(s_battery_labels[static_cast<int>(s_screen)]);
    lv_timer_set_period(timer, charging ? 10000 : 30000);
}

/* -----------------------------------------------------------------------
 * DEEP SLEEP — the sleep screen itself is no longer an LVGL screen
 * (2026-09-22 request: replace it outright with resources/sleep.png,
 * tools/gen_sleep.py -> sleep_bitmap.h). splash.cpp's
 * board_sleep_screen_show() draws that bitmap directly into eink.c's
 * native framebuffer, the same pre-LVGL path board_splash_show() already
 * uses for the boot splash -- simpler than building an LVGL I1 image
 * asset, and this codebase already learned the hard way (the I1
 * palette-offset bug) how easy that format is to get subtly wrong.
 *
 * ui_nav_suspend_lvgl_for_sleep() only keeps the thread-safety half of
 * what used to be ui_nav_show_sleep_screen(): board_sleep_screen_show()
 * can be called from either board_ui_nav_task itself (Home's "Sleep now",
 * the ADR-012 idle timeout) or main.c's power_button_task (a different
 * task, on a Power long-press) -- this flag is what stops
 * board_ui_nav_task's own loop from still pumping lv_timer_handler() while
 * splash.cpp blits and flushes the sleep bitmap straight into the shared
 * framebuffer right after. */
extern "C" void ui_nav_suspend_lvgl_for_sleep(void)
{
    s_lvgl_suspended.store(true, std::memory_order_relaxed);
}

/* -----------------------------------------------------------------------
 * Task entry point (main.c: xTaskCreate(board_ui_nav_task, ...)).
 * ----------------------------------------------------------------------- */
static void build_screen(Screen s, lv_obj_t *(*build_fn)(lv_group_t **, lv_obj_t **))
{
    lv_group_t *group = nullptr;
    lv_obj_t *battery_label = nullptr;
    s_screens[static_cast<int>(s)] = build_fn(&group, &battery_label);
    s_groups[static_cast<int>(s)] = group;
    s_battery_labels[static_cast<int>(s)] = battery_label;
}

static void build_screens(void)
{
    build_screen(Screen::Home, build_home);
    build_screen(Screen::Settings, build_settings);
    build_screen(Screen::SettingsControls, build_settings_controls);
    build_screen(Screen::SettingsDisplay, build_settings_display);
    build_screen(Screen::SettingsAbout, build_settings_about);
    build_screen(Screen::TouchDiag, build_touch_diag);
    build_screen(Screen::Security, build_security);
    build_screen(Screen::Time, build_time);
}

/* REMOVED 2026-09-24 (was: slip eink.c's mandatory ghost-budget full GC
 * into an idle gap instead of letting it land on an active tap, added
 * 2026-09-22). User feedback: any refresh firing without a direct user
 * interaction is unwanted, including one scheduled for an idle moment --
 * matches this app's original, stricter rule (full refresh only on a
 * screen-type change or sleep/wake, UI-SPECS.md §1.1). The hard budget in
 * eink.c (EINK_FAST_BUDGET, unconditional every ~30 fast DUs) is
 * untouched and still bounds ghosting on its own; it can now once again
 * land on an active tap if someone stays on one screen tapping non-stop
 * long enough, same tradeoff this app accepted before 2026-09-22. */

static void check_idle_timeout(void)
{
    uint32_t timeout_s = settings_store_get_idle_timeout_s();
    if (timeout_s == 0) {
        return;   /* disabled, settings_store.h's own dev-default */
    }
    int64_t now = esp_timer_get_time();
    int64_t last = s_last_activity_us.load(std::memory_order_relaxed);
    if (now - last >= static_cast<int64_t>(timeout_s) * 1000000) {
        enter_sleep_from_idle_or_menu("idle timeout");
    }
}

void board_ui_nav_task(void *arg)
{
    (void)arg;

    board_activity_notify();   /* don't start already "idle since boot" */

    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    /* frontlight_init() was never called anywhere in this app (2026-09-22
     * bug report: "au demarrage le frontlight ne marche pas non plus meme
     * si actif") -- every frontlight_apply() call since (here and from the
     * Display screen's toggle/steppers) was setting duty on an LEDC timer/
     * channel pair that had never actually been configured, so it silently
     * had no effect on the physical pins. Must run once, before the first
     * apply_frontlight_from_settings() below. */
    time_service_init();
    frontlight_init();
    apply_frontlight_from_settings();

    build_screens();
    switch_screen(Screen::Home);

    uint8_t initial_soc = 0;
    bool initial_charging = false;
    battery_read(&initial_soc, &initial_charging);
    lv_timer_create(battery_timer_cb, initial_charging ? 10000 : 30000, nullptr);

    while (1) {
        if (!s_lvgl_suspended.load(std::memory_order_relaxed)) {
            lv_timer_handler();

            /* Counters, not flags -- see their declaration's comment. Drain
             * ALL pending steps queued while this loop was busy (e.g. mid-
             * flush), not just one, so two quick presses of the same
             * button always produce two actions once the loop catches up. */
            int power_confirms = s_power_confirm_pending.exchange(0, std::memory_order_relaxed);
            for (int i = 0; i < power_confirms; i++) {
                board_activity_notify();
                /* lv_group_send_data(group, LV_KEY_ENTER) only fires a raw
                 * LV_EVENT_KEY on the focused widget -- turning that into
                 * an actual click is normally lv_indev.c's job, done for a
                 * real KEYPAD/ENCODER indev's own press/release state
                 * machine, which this bypasses entirely (found on hardware
                 * 2026-09-21: Power-short-press "confirm" logged but never
                 * activated the focused button). Firing LV_EVENT_CLICKED
                 * directly is the simple, correct equivalent for a plain
                 * button/menu entry, which is all this confirm pulse is
                 * meant to activate. */
                lv_obj_t *focused = lv_group_get_focused(s_groups[static_cast<int>(s_screen)]);
                if (focused) {
                    lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
                }
            }

            /* Touch-Home confirms: still counted as activity (a real touch
             * happened) even when dropped -- see
             * board_ui_nav_suppress_touch_confirm()'s doc comment (ui_nav.h)
             * for why a suppressed screen still discards these instead of
             * queuing them for later. */
            int touch_home_confirms = s_touch_home_confirm_pending.exchange(0, std::memory_order_relaxed);
            bool suppress_touch_confirm = s_suppress_touch_confirm.load(std::memory_order_relaxed);
            for (int i = 0; i < touch_home_confirms; i++) {
                board_activity_notify();
                if (suppress_touch_confirm) {
                    continue;
                }
                lv_obj_t *focused = lv_group_get_focused(s_groups[static_cast<int>(s_screen)]);
                if (focused) {
                    lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
                }
            }

            /* Moving focus only changes a button's outline ring (add_to_group()) --
             * a small delta the fast DU refresh handles fine, same as any other
             * state change on this UI. No lv_port_disp_request_full_refresh() here
             * (2026-09-22 fix): forcing a full GC flash on every single Left/Right
             * press made navigation feel slow for no visual benefit. */
            int focus_prev_steps = s_focus_prev_pending.exchange(0, std::memory_order_relaxed);
            for (int i = 0; i < focus_prev_steps; i++) {
                lv_group_focus_prev(s_groups[static_cast<int>(s_screen)]);
            }
            int focus_next_steps = s_focus_next_pending.exchange(0, std::memory_order_relaxed);
            for (int i = 0; i < focus_next_steps; i++) {
                lv_group_focus_next(s_groups[static_cast<int>(s_screen)]);
            }

            check_idle_timeout();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
