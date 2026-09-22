/**
 * @file lv_port_disp.c
 * @brief MySafeFob App — LVGL display port for the UC8279 e-ink driver
 *        (eink.c/eink.h, unmodified). One full-frame I1 (1bpp) PSRAM
 *        buffer, LV_DISPLAY_RENDER_MODE_FULL: LVGL always redraws and
 *        flushes the whole 480x800 portrait screen in one shot, which is
 *        the natural fit for a panel that has no partial-rectangle write
 *        path of its own (eink_display_fb()/_fast() both take a full
 *        frame). flush_cb transposes that portrait buffer into eink.c's
 *        native landscape layout at write time, per hw_config.h's
 *        documented convention (fb_x = uy, fb_y = 479 - ux) -- the same
 *        transform FreeInkUIDisplayTarget used to do internally for
 *        Orientation::Portrait.
 *
 * Bit polarity note: LVGL's I1 software blender (lv_draw_sw_blend_to_i1.c)
 * sets a bit for a WHITE/light pixel and clears it for BLACK/dark, MSB
 * first -- identical convention to eink.c's native buffer (0xFF = white,
 * 0x00 = black, MSB-first). No polarity inversion needed, only the
 * geometric transpose below.
 */
#include "lv_port_disp.h"

#include "lvgl.h"

#include "eink.h"
#include "hw_config.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_log_workaround.h"

static const char *TAG = "lv_port_disp";

static uint8_t *s_lv_buf;             /* portrait I1 render buffer, PSRAM */
static uint32_t s_lv_stride;          /* bytes per portrait row */
static uint8_t s_native_fb[SCREEN_FB_SIZE];  /* landscape native fb, rebuilt every flush */
static bool s_force_full_refresh = true;

static inline int lv_i1_get_bit(const uint8_t *buf, uint32_t stride, int x, int y)
{
    const uint8_t byte = buf[y * stride + (x >> 3)];
    return (byte >> (7 - (x & 7))) & 1;
}

static inline void native_set_bit(uint8_t *buf, int x, int y, int value)
{
    uint8_t *byte = &buf[y * EINK_WB + (x >> 3)];
    const uint8_t mask = (uint8_t)(0x80 >> (x & 7));
    if (value) {
        *byte |= mask;
    } else {
        *byte &= (uint8_t)~mask;
    }
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;  /* LV_DISPLAY_RENDER_MODE_FULL always flushes the whole screen */

    /* An I1 buffer's first LV_COLOR_INDEXED_PALETTE_SIZE(I1)*4 = 8 bytes
     * are a reserved/assumed palette, not pixel data -- already accounted
     * for in lv_port_disp_init()'s buffer *size* (the crash fix below),
     * but must also be skipped *here*. Found on hardware 2026-09-21: every
     * read without this skip was off by a flat 8 bytes, which (480/8=60,
     * not itself a multiple of 8) drifts differently on every row --
     * looked like objects rendering at the wrong, sometimes duplicated,
     * position. LVGL's own docs: "reserves this space for an assumed
     * palette (which can be skipped in flush callbacks via px_map += 8)". */
    px_map += 8;

    for (int uy = 0; uy < SCREEN_HEIGHT; uy++) {
        for (int ux = 0; ux < SCREEN_WIDTH; ux++) {
            const int bit = lv_i1_get_bit(px_map, s_lv_stride, ux, uy);
            native_set_bit(s_native_fb, uy, SCREEN_WIDTH - 1 - ux, bit);
        }
    }

    /* Timed, but only LOGGED for a FULL refresh (rare: one per screen switch
     * or ghost-budget housekeeping) or an abnormally slow fast DU (should
     * never exceed ~1s; > kSlowFastMs would mean something is actually
     * wrong, worth knowing about). The routine per-tap "fast" case used to
     * be logged unconditionally too (2026-09-22 diagnosis of a dropped-click
     * bug, since fixed in lv_port_indev.c) -- at the tap rate a real user
     * hits during testing, that was itself enough serial output to risk
     * UART back-pressure blocking this task's own printf()/fflush() calls,
     * adding exactly the kind of latency being diagnosed. Now quiet unless
     * there's something worth seeing. */
    static const int64_t kSlowFastMs = 1200;
    bool full = s_force_full_refresh;
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = full ? eink_display_fb(s_native_fb)
                          : eink_display_fb_fast(s_native_fb);
    int64_t elapsed_ms = (esp_timer_get_time() - t0) / 1000;
    s_force_full_refresh = false;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "flush failed: %s", esp_err_to_name(err));
    } else if (full) {
        ESP_LOGI(TAG, "flush FULL done in %lld ms", (long long)elapsed_ms);
    } else if (elapsed_ms > kSlowFastMs) {
        ESP_LOGW(TAG, "flush fast unusually slow: %lld ms", (long long)elapsed_ms);
    }

    lv_display_flush_ready(disp);
}

static void lv_tick_timer_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}

void lv_port_disp_init(void)
{
    memset(s_native_fb, 0xFF, sizeof(s_native_fb));

    static const esp_timer_create_args_t tick_timer_args = {
        .callback = &lv_tick_timer_cb,
        .name = "lv_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000));  /* 1 ms */

    lv_display_t *disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);

    s_lv_stride = lv_draw_buf_width_to_stride(SCREEN_WIDTH, LV_COLOR_FORMAT_I1);
    /* I1 counts as an "indexed" color format (LV_COLOR_FORMAT_IS_INDEXED) --
     * lv_draw_buf_reshape() (called on every refresh, lv_refr.c) sizes the
     * buffer as stride*h PLUS its 2-entry palette
     * (LV_COLOR_INDEXED_PALETTE_SIZE(I1)*4 bytes), even though nothing in
     * our flush path ever reads that palette (bit polarity is fixed, see
     * lv_port_disp.h's doc comment). Undersizing this crashes: reshape
     * returns NULL, LV_ASSERT_NULL hangs the task, and the watchdog panics
     * a few seconds later -- found by hardware testing 2026-09-21. */
    const uint32_t buf_size = s_lv_stride * SCREEN_HEIGHT + LV_COLOR_INDEXED_PALETTE_SIZE(LV_COLOR_FORMAT_I1) * 4;
    s_lv_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!s_lv_buf) {
        /* Used to log and return here, leaving LVGL with no display buffers
         * configured -- board_ui_nav_task would carry on into build_screens()
         * regardless, hit lv_obj_create()/lv_screen_load() with no display
         * to attach to, and crash later with no clear link back to this,
         * the real cause (2026-09-22 audit finding). Fail loudly and
         * immediately instead: PSRAM exhaustion this early is unrecoverable
         * anyway (48KB, the smallest thing LVGL needs here), so there's
         * nothing a caller could usefully do with a soft failure. */
        ESP_LOGE(TAG, "PSRAM alloc FAILED for the %u-byte LVGL frame buffer -- cannot start the UI",
                 (unsigned)buf_size);
        abort();
    }

    lv_display_set_buffers(disp, s_lv_buf, NULL, buf_size, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, flush_cb);

    /* CONFIG_LV_COLOR_DEPTH_1 (sdkconfig.defaults) already auto-selects
     * LV_USE_THEME_MONO over the default/simple themes, but apply it
     * explicitly rather than relying on that auto-wiring. */
    lv_theme_t *theme = lv_theme_mono_init(disp, false, LV_FONT_DEFAULT);
    lv_display_set_theme(disp, theme);
}

void lv_port_disp_request_full_refresh(void)
{
    s_force_full_refresh = true;
}

bool lv_port_disp_ghost_budget_low(void)
{
    return eink_ghost_budget_low();
}
