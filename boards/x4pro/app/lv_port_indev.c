/**
 * @file lv_port_indev.c
 * @brief MySafeFob App — LVGL input port: touch.c's GT911 driver (its
 *        axis-swap + piecewise-linear coordinate remap is untouched,
 *        consumed here only via touch_read()'s already-calibrated x/y)
 *        feeds one LVGL pointer indev, read by LVGL itself from
 *        board_ui_nav_task's own lv_timer_handler() call -- safe, that's
 *        the one task allowed to touch LVGL.
 *
 *        Left/Right (buttons.c), sampled in THIS file's own task, must
 *        NOT call any lv_* API directly: LVGL is not thread-safe, and
 *        this task is a different FreeRTOS task from the one running
 *        lv_timer_handler(). Found the hard way on hardware 2026-09-21 --
 *        calling lv_group_focus_prev/next() from here crashed
 *        (lv_inv_area, two tasks touching LVGL's invalidated-area list at
 *        once). Left/Right only ever raise a flag via
 *        board_ui_nav_focus_prev/next() (ui_nav.h); board_ui_nav_task's
 *        own loop is what actually calls into LVGL for it.
 */
#include "lv_port_indev.h"

#include "buttons.h"
#include "ui_nav.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>

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

static const char *TAG = "lv_port_indev";

static portMUX_TYPE s_touch_lock = portMUX_INITIALIZER_UNLOCKED;
static touch_point_t s_last_touch;   /* protected by s_touch_lock */
static bool s_touch_ok;

static void pointer_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    portENTER_CRITICAL(&s_touch_lock);
    touch_point_t tp = s_last_touch;
    portEXIT_CRITICAL(&s_touch_lock);

    data->point.x = tp.x;
    data->point.y = tp.y;
    /* The "home pad" is a dedicated capacitive zone on the digitizer
     * BELOW the visible e-ink glass (2026-09-21, user-confirmed hardware
     * detail) -- touch.c's calibration still reports an x/y that happens
     * to fall inside the normal 0-479/0-799 screen range for it, which
     * would make LVGL click whatever widget happens to be drawn near
     * that y on the CURRENT screen, unrelated to the actual (off-screen)
     * button being pressed. Never report it as a real pointer press --
     * input_sampler_task treats its press edge as a separate "confirm"
     * pulse instead (same one Power-short-press uses; this is exactly
     * the old FreeInkUI "touch-Home = confirm" gesture, decoupled from
     * position, that this indev had lost track of). */
    data->state = (tp.pressed && !tp.home) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

touch_point_t lv_port_indev_get_last_touch(void)
{
    portENTER_CRITICAL(&s_touch_lock);
    touch_point_t tp = s_last_touch;
    portEXIT_CRITICAL(&s_touch_lock);
    return tp;
}

/* Sampled in its own task, decoupled from the LVGL/flush task (same
 * reasoning as the pre-LVGL input_sampler_task's 2026-09-18 fix in
 * ui_nav.cpp's history: a flush blocks for up to a few seconds, and a
 * quick touch tap sampled inline on that same task can start and end
 * entirely inside that window, never seen at all). Software debounce
 * (30 ms) also carried over from that same fix — a capacitive panel's
 * bounce at press/release was occasionally read as two taps. */
static const int64_t kTouchDebounceUs = 30000;

static void input_sampler_task(void *arg)
{
    (void)arg;

    bool touch_was_pressed = false;
    bool touch_raw_state = false;
    int64_t touch_raw_since_us = 0;

    while (1) {
        button_id_t btn = button_wait_press(20);
        if (btn == BTN_1) {
            board_activity_notify();
            board_ui_nav_focus_prev();
        } else if (btn == BTN_2) {
            board_activity_notify();
            board_ui_nav_focus_next();
        } else if (btn == BTN_3) {
            board_activity_notify();   /* Power: activity only, never acted on here */
        }

        if (s_touch_ok) {
            touch_point_t tp = touch_read();

            if (tp.pressed != touch_raw_state) {
                touch_raw_state = tp.pressed;
                touch_raw_since_us = esp_timer_get_time();
            }
            bool debounced = touch_was_pressed;
            if (touch_raw_state != touch_was_pressed &&
                (esp_timer_get_time() - touch_raw_since_us) >= kTouchDebounceUs) {
                debounced = touch_raw_state;
            }
            const bool press_edge = debounced && !touch_was_pressed;
            const bool release_edge = !debounced && touch_was_pressed;
            touch_was_pressed = debounced;
            tp.pressed = debounced;

            if (press_edge || release_edge) {
                ESP_LOGI(TAG, "touch %s: x=%d y=%d raw=(%d,%d) home=%d",
                         press_edge ? "press" : "release", tp.x, tp.y,
                         tp.raw_x, tp.raw_y, (int)tp.home);
            }

            portENTER_CRITICAL(&s_touch_lock);
            s_last_touch = tp;
            portEXIT_CRITICAL(&s_touch_lock);

            if (press_edge) {
                board_activity_notify();
                if (tp.home) {
                    board_ui_nav_power_confirm();   /* touch-Home = confirm, see pointer_read_cb */
                }
            }
        }
    }
}

void lv_port_indev_init(void)
{
    s_touch_ok = touch_init();
    if (!s_touch_ok) {
        ESP_LOGE(TAG, "GT911 not responding -- touch input disabled");
    }
    buttons_init();

    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, pointer_read_cb);

    xTaskCreate(input_sampler_task, "ui_input", 4096, NULL, 4, NULL);
}

lv_group_t *lv_port_indev_new_group(void)
{
    return lv_group_create();
}
