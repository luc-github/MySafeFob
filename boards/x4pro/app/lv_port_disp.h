#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocates the LVGL draw buffer (PSRAM) and registers the display
 *        driver: LV_COLOR_FORMAT_I1, LV_DISPLAY_RENDER_MODE_FULL (one
 *        full-frame buffer, one flush per redraw — matches the e-paper
 *        "redraw everything, then flush" model). flush_cb transposes the
 *        portrait I1 buffer into eink.c's native landscape framebuffer
 *        layout (hw_config.h's fb_x=uy, fb_y=479-ux convention) and calls
 *        eink_display_fb() (full GC) or eink_display_fb_fast() (DU),
 *        picking one based on lv_port_disp_request_full_refresh()'s flag.
 *        Call once, after eink_init(), before building any screen.
 */
void lv_port_disp_init(void);

/**
 * @brief Forces the NEXT flush to use a full GC refresh instead of the
 *        default fast DU one (UI-SPECS.md §1.1: full refresh only on a
 *        screen-type change). One-shot — cleared right after that flush.
 */
void lv_port_disp_request_full_refresh(void);

/**
 * @brief Number of successful flushes (fast DU or full GC alike) completed
 * so far, since boot. Monotonically increasing, never resets.
 *
 * 2026-09-23: Settings > Touch Calibration uses this instead of guessing a
 * fixed "wait this many ms" delay to know whether the panel has actually
 * redrawn since its target crosshair last moved -- a tap arriving before
 * the count has advanced is aimed at whatever the panel is STILL showing
 * (the flush hasn't happened yet), not the new position, no matter how
 * long the caller waited (a full GC and a DU take very different times,
 * and an unrelated queued refresh -- e.g. ghost-budget housekeeping --
 * can land in between and push it out further still). Confirmed on
 * hardware: a fixed ~1.7s guess still let a couple of stale taps through.
 */
uint32_t lv_port_disp_get_flush_count(void);

#ifdef __cplusplus
}
#endif
