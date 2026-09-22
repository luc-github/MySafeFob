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
#include <string.h>

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

    esp_err_t err = s_force_full_refresh ? eink_display_fb(s_native_fb)
                                          : eink_display_fb_fast(s_native_fb);
    s_force_full_refresh = false;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "flush failed: %s", esp_err_to_name(err));
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
        ESP_LOGE(TAG, "PSRAM alloc failed for the %u-byte LVGL frame buffer", (unsigned)buf_size);
        return;
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
