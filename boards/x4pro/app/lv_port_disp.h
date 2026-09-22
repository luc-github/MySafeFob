#pragma once

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

#ifdef __cplusplus
}
#endif
