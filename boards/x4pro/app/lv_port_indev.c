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
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>

#include "app_log_workaround.h"

static const char *TAG = "lv_port_indev";

static portMUX_TYPE s_touch_lock = portMUX_INITIALIZER_UNLOCKED;
static touch_point_t s_last_touch;   /* protected by s_touch_lock */
static bool s_touch_ok;

/* Discrete press/release EDGE queue (2026-09-22 bug: a button occasionally
 * takes focus but its click never registers -- confirmed on hardware with
 * timestamped logs: the whole press+release cycle sometimes falls in a
 * window where board_ui_nav_task's lv_timer_handler() never happens to
 * observe the transition, e.g. while it's blocked doing an unrelated
 * eink flush (up to ~1.5s for a full GC), or coincides with another timer
 * (the battery label's periodic refresh). pointer_read_cb() used to report
 * only the CURRENT debounced level (s_last_touch) -- if the level flips
 * PRESSED then back to RELEASED between two calls that both happen to land
 * on "released", LVGL never sees the transition at all: no PRESSED, no
 * CLICKED, the tap vanishes even though input_sampler_task logged it
 * correctly. This is exactly the architectural risk flagged (but not
 * fully closed) in docs/ROADMAP.md's 2026-09-18 entry.
 *
 * Fix: input_sampler_task pushes an explicit edge (not just a level) on
 * every press/release transition; pointer_read_cb() drains this queue one
 * edge per read, using LVGL's own `continue_reading` mechanism to be
 * called again immediately (skipping its normal poll period) as long as
 * more edges are queued -- so a burst of taps that piled up while the task
 * was busy flushing gets replayed in full, in order, the moment it's free
 * again, instead of being coalesced down to whatever the level happened to
 * be at the next poll. 16 deep: far more than one real tap's press+release
 * pair, comfortably absorbs several queued taps behind one slow flush --
 * sized for the WORST case (a full GC refresh, ~1.5s, twice as long as a
 * fast DU) rather than the common one. */
typedef struct {
    bool pressed;
    int16_t x, y;
} touch_edge_t;

static QueueHandle_t s_touch_edges;

static void pointer_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    touch_edge_t edge;
    if (s_touch_edges && xQueueReceive(s_touch_edges, &edge, 0) == pdTRUE) {
        data->point.x = edge.x;
        data->point.y = edge.y;
        data->state = edge.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        /* More queued edges (e.g. a whole tap that arrived while this task
         * was busy) -- have LVGL call this again right away instead of
         * waiting for its next scheduled poll, so they're all delivered
         * as their own PRESSED/RELEASED transition, in order. */
        data->continue_reading = (uxQueueMessagesWaiting(s_touch_edges) > 0);
        return;
    }

    /* No pending edge -- report the current debounced level, same as
     * before (still needed while a touch is held with no new transition:
     * indev_proc_press() re-hit-tests the touched object on every read
     * while PRESSED, so it needs a stable, current coordinate each time,
     * not just once at the edge). */
    data->continue_reading = false;
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
 * bounce at press/release was occasionally read as two taps. Reused below
 * (same 30ms) for the physical buttons' own debounce -- same mechanical
 * bounce problem, no reason for a different constant. */
static const int64_t kTouchDebounceUs = 30000;
static const int64_t kButtonDebounceUs = 30000;

static void input_sampler_task(void *arg)
{
    (void)arg;

    bool touch_was_pressed = false;
    bool touch_raw_state = false;
    int64_t touch_raw_since_us = 0;
    /* Last known-good coordinates while a touch is held (2026-09-22 bug:
     * "+ takes focus but the value never changes"). touch_read() resets to
     * {x=-1,y=-1} on EVERY call and only fills in real coordinates when the
     * GT911 reports a fresh buffer THIS poll -- during a held touch, the
     * chip doesn't necessarily re-assert "buffer ready" on every single
     * ~20ms poll, so an intermediate sample can see "nothing new" while the
     * finger is still down. The debounce below correctly keeps `pressed`
     * true across that gap, but was still forwarding that poll's raw
     * (x=-1,y=-1) to LVGL. LVGL re-hit-tests the touched object on EVERY
     * PRESSED sample (lv_indev.c's indev_proc_press()) -- a stray (-1,-1)
     * matches no widget, so LVGL fires PRESS_LOST on the real button and
     * clears its own "active object". The eventual real release then finds
     * no active object left and never sends CLICKED: focus stays visible
     * (it was set before the glitch), but the click is silently dropped.
     * Fix: while debounced-held, substitute the last real coordinate for
     * any poll that came back empty, so LVGL never sees a bogus jump. */
    int16_t last_valid_x = -1, last_valid_y = -1;
    int16_t last_valid_raw_x = 0, last_valid_raw_y = 0;
    bool last_valid_home = false;

    /* Same debounce pattern as touch above (2026-09-22 change): buttons.c
     * used to block here until physical release (button_wait_press()),
     * which stalled THIS loop's touch sampling for as long as a button was
     * held -- buttons_read_raw() never blocks, so the debounce/edge
     * detection moved here instead, interleaved with touch every
     * iteration. Acts on the RELEASE edge, matching button_wait_press()'s
     * original behavior (it only ever returned once the physical release
     * was seen), not the press edge. */
    button_id_t button_raw_state = BTN_NONE;
    button_id_t button_debounced = BTN_NONE;
    int64_t button_raw_since_us = 0;

    while (1) {
        button_id_t button_raw = buttons_read_raw();
        if (button_raw != button_raw_state) {
            button_raw_state = button_raw;
            button_raw_since_us = esp_timer_get_time();
        }
        button_id_t new_button_debounced = button_debounced;
        if (button_raw_state != button_debounced &&
            (esp_timer_get_time() - button_raw_since_us) >= kButtonDebounceUs) {
            new_button_debounced = button_raw_state;
        }
        const bool button_release_edge = button_debounced != BTN_NONE && new_button_debounced == BTN_NONE;
        button_id_t released_btn = button_debounced;
        button_debounced = new_button_debounced;

        if (button_release_edge) {
            if (released_btn == BTN_1) {
                board_activity_notify();
                board_ui_nav_focus_prev();
            } else if (released_btn == BTN_2) {
                board_activity_notify();
                board_ui_nav_focus_next();
            } else if (released_btn == BTN_3) {
                board_activity_notify();   /* Power: activity only, never acted on here */
            }
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

            /* Cache any real sample as soon as the chip reports one -- even
             * before debounce confirms the press -- so the FIRST poll after
             * press_edge fires already has a valid point to fall back on,
             * not just later ones. */
            if (tp.x >= 0) {
                last_valid_x = tp.x;
                last_valid_y = tp.y;
                last_valid_raw_x = tp.raw_x;
                last_valid_raw_y = tp.raw_y;
                last_valid_home = tp.home;
            } else if (debounced) {
                /* No fresh buffer this poll, but still debounced-held --
                 * report the last known-good point instead of -1,-1. */
                tp.x = last_valid_x;
                tp.y = last_valid_y;
                tp.raw_x = last_valid_raw_x;
                tp.raw_y = last_valid_raw_y;
                tp.home = last_valid_home;
            }

            tp.pressed = debounced;

            if (press_edge || release_edge) {
                ESP_LOGI(TAG, "touch %s: x=%d y=%d raw=(%d,%d) home=%d",
                         press_edge ? "press" : "release", tp.x, tp.y,
                         tp.raw_x, tp.raw_y, (int)tp.home);
            }

            portENTER_CRITICAL(&s_touch_lock);
            s_last_touch = tp;
            portEXIT_CRITICAL(&s_touch_lock);

            /* Queue the discrete edge for pointer_read_cb() -- see its own
             * doc comment. Home-pad presses are never reported as a real
             * pointer press/release (pointer_read_cb's long-standing home
             * exclusion, unchanged), so neither edge is queued for those;
             * home's own "confirm" pulse below is unaffected. */
            if (s_touch_edges && !tp.home) {
                touch_edge_t edge;
                bool have_edge = false;
                if (press_edge) {
                    edge = (touch_edge_t){ .pressed = true, .x = tp.x, .y = tp.y };
                    have_edge = true;
                } else if (release_edge) {
                    edge = (touch_edge_t){ .pressed = false, .x = tp.x, .y = tp.y };
                    have_edge = true;
                }
                /* xQueueSend() silently drops the edge if the queue is
                 * full -- that would silently reproduce exactly the kind
                 * of dropped click this queue exists to prevent. Logged
                 * loudly (not just discarded) so a future occurrence is
                 * proof, not a guess: if this line is ever seen, the fix
                 * is to widen the queue further, not just accept it. */
                if (have_edge && xQueueSend(s_touch_edges, &edge, 0) != pdTRUE) {
                    ESP_LOGE(TAG, "touch edge queue FULL -- dropped a %s edge!",
                             edge.pressed ? "press" : "release");
                }
            }

            if (press_edge) {
                board_activity_notify();
                if (tp.home) {
                    board_ui_nav_power_confirm(true);   /* touch-Home = confirm, see pointer_read_cb */
                }
            }
        }

        /* Sets this loop's overall poll cadence (~20ms) -- previously
         * provided implicitly by button_wait_press()'s own internal delay
         * loop, now that both button and touch sampling above are fully
         * non-blocking. */
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void lv_port_indev_init(void)
{
    s_touch_ok = touch_init();
    if (!s_touch_ok) {
        ESP_LOGE(TAG, "GT911 not responding -- touch input disabled");
    }
    buttons_init();

    s_touch_edges = xQueueCreate(16, sizeof(touch_edge_t));

    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, pointer_read_cb);

    xTaskCreate(input_sampler_task, "ui_input", 4096, NULL, 4, NULL);
}

lv_group_t *lv_port_indev_new_group(void)
{
    return lv_group_create();
}
