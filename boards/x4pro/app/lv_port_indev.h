#pragma once

#include "lvgl.h"
#include "touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inits touch.c (GT911) + buttons.c, registers one LVGL pointer
 *        indev (fed from touch.c's already-remapped/calibrated x/y — see
 *        touch.c for the GT911 coordinate offset logic, untouched here).
 *        Sampling runs in its own task, decoupled from the LVGL/flush
 *        task (same reasoning as the pre-LVGL input_sampler_task this
 *        replaces: a flush blocks for up to a few seconds, and a touch
 *        tap sampled inline on that task can be missed entirely).
 */
void lv_port_indev_init(void);

/**
 * @brief Creates a new, empty lv_group. Each screen owns ONE of its own
 *        (found on hardware 2026-09-21: a single group shared across
 *        every screen meant Left/Right could move focus onto a widget
 *        belonging to a currently-hidden screen — invisible, looked like
 *        the buttons "did nothing" on every screen but the first built).
 */
lv_group_t *lv_port_indev_new_group(void);

/**
 * @brief Latest debounced touch sample (mutex-protected) — for the
 *        TouchDiag screen's raw-coordinate readout, not used for hit
 *        testing (LVGL's pointer indev already does that).
 */
touch_point_t lv_port_indev_get_last_touch(void);

#ifdef __cplusplus
}
#endif
