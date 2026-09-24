/* 
 Project: MySafeFob  touch.h
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
 * @file touch.h
 * @brief MySafeFob App — X4 Pro GT911 touch driver (polling).
 *
 * GT911 driver, with the sequences validated on the x4pro-probe probe:
 *  - POR reset dance (RST=GPIO4, INT=GPIO10, rail GPIO2 active-low)
 *  - CONFIG UPLOAD MANDATORY on every boot (OTP blank from the factory on
 *    this batch: 0x8047 reads 0x00, the panel doesn't scan without host config)
 *  - mapping validated by the 4-corner test: fb_x = raw_y, fb_y = 479 - raw_x
 *
 * Systematic reads of >= 2 bytes (IDF 5.4 I2C driver quirk:
 * 1-byte reads get NACKed).
 *
 * Identical copy of boards/x4pro/factory/main/touch.h (ADR-010: same
 * hardware, no board divergence expected) — kept independent from the
 * factory's per ADR-010 pt.3 (the factory copy is only updated by an
 * explicit action tested on hardware, never automatically alongside the
 * app's copy).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 2026-09-23: this header used to rely on every C++ caller wrapping its own
 * #include "touch.h" in extern "C" -- broke silently (C++-mangled linkage,
 * a link-time "undefined reference" far from the actual cause) the moment
 * ANY file pulled this header in unwrapped first (lv_port_indev.h does, via
 * its own plain #include "touch.h"): #pragma once then makes every LATER
 * #include a no-op, including one deliberately wrapped in extern "C" by a
 * different .cpp file, silently keeping the FIRST (unwrapped) linkage.
 * Guarding it here instead, like every other header in this component,
 * makes it correct regardless of include order. */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool pressed;       /* true if a finger is down */
    bool home;          /* true if the point is within the Home pad zone
                         * (validated measurement 01:34: raw rx<70, ry 380-580) */
    int16_t x;          /* logical coords, after any calibration transform
                         * (currently: raw passthrough -- unresolved,
                         * docs/touch-calibration-notes.md) */
    int16_t y;          /* logical coords, after touch_rescale_y() */
    int16_t raw_x;      /* GT911 register value, no transform at all --
                         * exposed so the touch diagnostic screen can show
                         * both and calibration doesn't have to guess
                         * which stage a logged number came from */
    int16_t raw_y;
} touch_point_t;

/**
 * @brief Init rails + GT911 POR dance + I2C probe + config upload if needed.
 * @return true if the controller responds and scans.
 */
bool touch_init(void);

/**
 * @brief Reads the current touch state (polling, non-blocking).
 *        NEVER reset the chip between two reads (2026-09-12 bug:
 *        re-dancing on every poll prevented scanning).
 */
touch_point_t touch_read(void);

/**
 * @brief Per-unit linear correction (scale + offset per axis) applied on
 *        top of the fixed calibration above (touch_lerp()/touch_lerp_y()),
 *        persisted in settings_store.h/NVS -- 2026-09-23, Settings >
 *        Touch Calibration's guided 9-target sequence
 *        (ui_screen_touch_diag.cpp) computes and calls this once at the
 *        end of a run. Cached in RAM (loaded once by touch_init()) so
 *        touch_read()'s hot path (~50Hz poll) never touches NVS itself.
 *        Defaults to identity (scale=1000, offset=0) until a calibration
 *        run has been completed at least once.
 * @param scale_x1000/scale_y1000 Per-axis scale, x1000 fixed-point
 *        (1000 = 1.000x).
 * @param offset_x/offset_y Per-axis offset in logical pixels, added AFTER
 *        scaling.
 * @return True once every one of the 4 values has been read straight back
 *         from NVS and matches what was just written -- a real check, not
 *         just "the write call didn't return an error" (settings_store.h's
 *         setters are void, matching NVS's own fire-and-forget convention,
 *         so without this a silent flash-write failure would otherwise be
 *         invisible here). False means the correction is still applied
 *         in RAM for this session, but was NOT reliably saved -- it won't
 *         survive a reboot.
 */
bool touch_set_calibration(int32_t scale_x1000, int32_t offset_x,
                           int32_t scale_y1000, int32_t offset_y);

/**
 * @brief Reads back the currently-applied (in-RAM) correction -- 2026-09-24,
 *        Settings > Touch Calibration saves this before starting a fresh
 *        run (which always resets to identity first, see
 *        touch_reset_calibration()'s own call site there) so a failed run
 *        can restore it afterward instead of leaving identity applied.
 */
void touch_get_calibration(int32_t *scale_x1000, int32_t *offset_x,
                           int32_t *scale_y1000, int32_t *offset_y);

/**
 * @brief Fits touch_set_calibration()'s per-axis linear correction from n
 *        (measured, expected) logical-coordinate point pairs via
 *        least-squares regression, persists it, and returns the worst
 *        residual error (px) across those same points once the new
 *        correction is applied to them -- lets the caller show a
 *        pass/fail readout without re-deriving the fit itself.
 *        `measured_*`/`expected_*` are parallel arrays of length n.
 * @param out_persisted If non-NULL, set to touch_set_calibration()'s own
 *        verified-persistence result (see its doc comment) -- the caller
 *        uses this to tell the user whether the calibration will survive
 *        a reboot, not just that the fit itself succeeded.
 * @return The max residual error in px, or -1 if the fit itself was
 *         rejected as unsafe (calibration_fit_is_sane(), touch.c) and
 *         NEVER applied or persisted -- the previous correction (identity,
 *         if this run started fresh) is left untouched. *out_persisted is
 *         false in that case too. See calibration_fit_is_sane()'s own
 *         comment for the 2026-09-23 incident (a corrupted fit briefly
 *         made every future tap register as Y=0) this guards against.
 */
int32_t touch_calibrate(const int16_t *measured_x, const int16_t *measured_y,
                         const int16_t *expected_x, const int16_t *expected_y, int n,
                         bool *out_persisted);

/**
 * @brief Resets the per-unit correction to identity (scale=1000, offset=0)
 *        and persists that -- Settings > Touch Calibration's own "Reset"
 *        control, in case a bad run made things worse.
 */
void touch_reset_calibration(void);

#ifdef __cplusplus
}
#endif
