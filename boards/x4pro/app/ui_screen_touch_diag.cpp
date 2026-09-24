/*
 Project: MySafeFob  ui_screen_touch_diag.cpp
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
 * @file ui_screen_touch_diag.cpp
 * @brief MySafeFob App — Settings > Touch Calibration: a real guided
 *        9-target calibration sequence (2026-09-23 rebuild).
 *
 * The previous version of this screen (5 static crosshairs + a column of
 * tick marks, both absolutely positioned straight on the screen, plus a
 * live raw/logical readout) was a one-off developer diagnostic used to
 * find the 2026-09-18 axis-swap bug -- never an actual calibration
 * feature, and its absolutely-positioned decorations visually overlapped
 * the header/content layout (2026-09-23 bug report: "la graduation et les
 * 5 croix... le tout superposé"). This rebuild is the guided sequence
 * `docs/UI-SPECS.md` §2.12 originally speced but never built: one target
 * crosshair shown at a time, tap it, advance; at the end, touch.c's
 * touch_calibrate() fits and PERSISTS a per-unit linear correction from
 * the 9 (measured, expected) point pairs, and a pass/fail readout (max
 * residual error) is shown.
 *
 * Escape hatch (2026-09-23, explicit safety requirement: this screen
 * exists to find out whether touch works at all, so it must stay
 * reachable if touch turns out to be completely broken): "< Back" is the
 * FIRST object added to this screen's group (add_back_header(), below),
 * and lv_group_add_obj() auto-focuses the first object added to an empty
 * group (lv_group.c) -- so Left/Right + Power-short-press (or touch-Home)
 * can always back out, independent of the content area's own touch
 * handling. A persistent on-screen hint says so, and a 30s no-progress
 * idle timeout bails back to Settings on its own as a last resort.
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "lv_port_indev.h"
#include "lv_port_disp.h"
#include "ui_nav.h"

extern "C" {
#include "hw_config.h"
#include "touch.h"
#include "esp_log.h"
#include "esp_timer.h"
}

#include "app_log_workaround.h"

#include <cstdio>

static const char *TAG = "ui_touch_cal";

struct CalTarget {
    int16_t x, y;
};

/* 3x3 grid (corners + edge-midpoints + center) -- catches a non-linear/
 * rotational error BETWEEN the corners, not just at them (the same class
 * of bug the 2026-09-18 axis-swap investigation found). Kept well clear of
 * the header/Back button area (make_content()'s top ~160px) and the status
 * text/hint/Restart button stacked above it in the content column. */
static constexpr int kCalTargetCount = 9;
static constexpr int kTapsPerTarget = 3;
/* Margins from the screen edges are bigger than they look like they'd need
 * to be, specifically to stay clear of the physical Home pad's capacitive
 * zone (touch.c: raw_x<70 && raw_y in [660,720]) -- a hardware zone below
 * the visible glass, but 2026-09-23 confirmed on real hardware that a tap
 * near this grid's bottom-right corner can alias into that SAME raw
 * window through touch_lerp()'s fixed mapping. That's not a mistap: the
 * driver correctly reports it as home=1 and reroutes it into the Power/
 * touch-Home "confirm" pulse (lv_port_indev.c) instead of a normal
 * pointer press -- which activates whatever's currently GROUP-FOCUSED
 * (deliberately "< Back", the escape hatch, see this file's header
 * comment), silently bailing out of the run in progress. Pulling every
 * target well clear of the bottom-right corner avoids the overlap instead
 * of fighting the Home-pad detection itself, which is correct as-is. */
/* Rows span the whole usable height (200/460/720) on purpose (2026-09-24
 * hardware finding): the per-unit correction is a global linear fit, so a
 * grid only covering y=520-700 made it EXTRAPOLATE to the header buttons
 * (y~110) -- a tap on Back came out ~50px low and landed on the Settings
 * menu's "Controls" button instead. Interpolating between rows spanning
 * the real range removes that. */
static constexpr int kCalGridTop = 200;
static constexpr int kCalGridBottom = 720;
static constexpr int kCalGridLeft = 80;
static constexpr int kCalGridRight = 380;
static constexpr int kCalGridMidX = (kCalGridLeft + kCalGridRight) / 2;
static constexpr int kCalGridMidY = (kCalGridTop + kCalGridBottom) / 2;

static const CalTarget kCalTargets[kCalTargetCount] = {
    {kCalGridLeft, kCalGridTop},    {kCalGridMidX, kCalGridTop},    {kCalGridRight, kCalGridTop},
    {kCalGridLeft, kCalGridMidY},   {kCalGridMidX, kCalGridMidY},   {kCalGridRight, kCalGridMidY},
    {kCalGridLeft, kCalGridBottom}, {kCalGridMidX, kCalGridBottom}, {kCalGridRight, kCalGridBottom},
};

/* A corrected point further than this from its target, after the fit, is
 * flagged instead of silently reported as a pass. Generous relative to
 * kMinTouchTarget (56px, ui_widgets.h) -- this is measuring MAPPING
 * accuracy, not touch precision. */
static constexpr int32_t kCalPassThresholdPx = 20;
/* No progress at all for this long -> bail back to Settings on its own
 * (the escape hatch's last resort, see file header). */
static constexpr int64_t kCalIdleTimeoutUs = 30LL * 1000000;

static lv_obj_t *s_cal_cross_h;
static lv_obj_t *s_cal_cross_v;
static lv_obj_t *s_cal_status_label;
static lv_timer_t *s_cal_timeout_timer;

/* Result banner (2026-09-23, user request): a plain, unmissable "SUCCESS"/
 * "FAILED" shown for kCalResultBannerMs before the detailed pass/fail text
 * (error in px, saved/not-saved) replaces it -- the detail matters for
 * troubleshooting, but the very first thing to see should be the one-word
 * answer to "did it work". */
static constexpr uint32_t kCalResultBannerMs = 3000;
static lv_timer_t *s_cal_result_timer;
static char s_cal_result_detail[112];

static int s_cal_index;
static int16_t s_cal_measured_x[kCalTargetCount];
static int16_t s_cal_measured_y[kCalTargetCount];
static bool s_cal_done;
static int64_t s_cal_last_progress_us;

/* Saved right before each run resets to identity (reset_calibration_sequence()),
 * so a run that FAILS (rejected as unsafe, poor error, or a persist
 * failure -- anything the "FAILED" banner covers) can put back whatever
 * was actually working before, instead of leaving identity applied
 * (2026-09-24 user request). */
static int32_t s_cal_prev_scale_x1000, s_cal_prev_offset_x;
static int32_t s_cal_prev_scale_y1000, s_cal_prev_offset_y;

/* kTapsPerTarget (defined with the grid above): averaging cancels random
 * aiming noise on a small crosshair. */
static int s_cal_taps_collected;
static int32_t s_cal_sum_x, s_cal_sum_y;

/* Set synchronously from content_pressed_cb() (real indev dispatch, so the
 * point is only valid right then) and consumed shortly after by the
 * ui_defer()'d advance -- see ui_widgets.h's standing rule on why the
 * state-mutating half is deferred but reading the tap position isn't. */
static struct {
    bool valid;
    int16_t x, y;
} s_pending_tap;

/* Minimum time between two COUNTED calibration taps (2026-09-23 hardware
 * bug: a capacitive bounce on a single physical tap occasionally produced
 * two press edges close together, over-counting one target to 4 taps
 * instead of 3 -- which desynced the rest of the sequence's (measured,
 * expected) pairing badly enough to flip the fitted scale's sign, and the
 * resulting correction collapsed every future tap's Y to 0, wiping touch
 * input entirely, including inside the SAME run that computed it).
 * Comfortably below a human's fastest deliberate re-tap, comfortably
 * above any realistic capacitive bounce window (touch.c's own debounce is
 * 30ms). */
static constexpr int64_t kCalTapCooldownUs = 400LL * 1000;
static int64_t s_cal_last_tap_us;

/* A tap only counts for the CURRENT target once at least one flush has
 * actually completed since that target's crosshair was requested to move
 * there (2026-09-23 hardware finding: with kCalTapCooldownUs alone,
 * several targets still received extra taps aimed at the previous,
 * still-displayed position, seconds after the target had already
 * advanced -- badly corrupting the next target's fit, which is what
 * pushed the Y scale outside calibration_fit_is_sane()'s bounds on every
 * run tried so far. A first attempt at fixing this guessed a fixed
 * ~1.7s wait, but hardware testing showed stray taps still arriving
 * 2-5s after a target change -- a flush's real duration varies (fast DU
 * vs full GC, or an unrelated queued refresh landing in between) too much
 * for any fixed guess to cover reliably). Comparing
 * lv_port_disp_get_flush_count() against the value captured when the
 * target last changed is an exact, real signal instead -- if it hasn't
 * moved, the panel provably hasn't redrawn yet, no matter how long the
 * wait was. */
static uint32_t s_cal_target_shown_flush;

/* Enlarged from the original 24px/4px (2026-09-23 hardware finding, see
 * kTapsPerTarget's comment): a thin, small cross is genuinely hard to
 * center a fingertip on at this panel size, inflating measured error with
 * pure aiming noise rather than real mapping distortion. Sized closer to
 * kMinTouchTarget (56px, ui_widgets.h) -- the app's own established
 * comfortable-target size -- rather than an arbitrary decorative mark. */
static constexpr int kCrossArmLen = 56;
static constexpr int kCrossArmThickness = 6;

/* Two thin crossing bars, repositioned via move_cross() as the sequence
 * advances -- one reusable cross instead of the old screen's 5 static
 * ones, since only ONE target is ever shown at a time now. */
static void make_cross(lv_obj_t *screen, lv_obj_t **out_h, lv_obj_t **out_v)
{
    lv_obj_t *h = lv_obj_create(screen);
    lv_obj_remove_style_all(h);
    lv_obj_set_style_bg_color(h, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_set_size(h, kCrossArmLen, kCrossArmThickness);

    lv_obj_t *v = lv_obj_create(screen);
    lv_obj_remove_style_all(v);
    lv_obj_set_style_bg_color(v, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
    lv_obj_set_size(v, kCrossArmThickness, kCrossArmLen);

    *out_h = h;
    *out_v = v;
}

static void move_cross(int cx, int cy)
{
    lv_obj_set_pos(s_cal_cross_h, cx - kCrossArmLen / 2, cy - kCrossArmThickness / 2);
    lv_obj_set_pos(s_cal_cross_v, cx - kCrossArmThickness / 2, cy - kCrossArmLen / 2);
}

static void cancel_result_banner(void)
{
    if (s_cal_result_timer) {
        lv_timer_delete(s_cal_result_timer);
        s_cal_result_timer = nullptr;
    }
}

/* repeat_count(1) (set where this timer is created) makes LVGL delete the
 * timer itself right after this callback returns (lv_timer.c) -- must NOT
 * also call lv_timer_delete() here (double-free), just drop the now-stale
 * pointer so cancel_result_banner() never sees it. */
static void cal_result_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    lv_label_set_text(s_cal_status_label, s_cal_result_detail);
    s_cal_result_timer = nullptr;
}

/* Explicit "1 [ok] 2 [ok] 3" progress (2026-09-23, user request: make it
 * obvious 3 taps are expected per target AND that each one actually got
 * counted) rather than a plain "tap 2 of 3" sentence -- a completed tap's
 * number gets LV_SYMBOL_OK appended, the pending one doesn't yet. */
static void append_tap_progress(char *buf, size_t bufsize)
{
    int pos = 0;
    for (int i = 1; i <= kTapsPerTarget && pos < (int)bufsize; i++) {
        pos += snprintf(buf + pos, bufsize - pos, "%s%d%s",
                        i > 1 ? "  " : "", i, i <= s_cal_taps_collected ? LV_SYMBOL_OK : "");
    }
}

static void update_status_label(void)
{
    char progress[48];
    append_tap_progress(progress, sizeof(progress));

    char buf[128];
    int prev = s_cal_index - 1;
    if (s_cal_taps_collected == 0 && prev >= 0) {
        snprintf(buf, sizeof(buf), "Target %d of %d\n%s\n(prev avg: x=%d y=%d)",
                 s_cal_index + 1, kCalTargetCount, progress, s_cal_measured_x[prev], s_cal_measured_y[prev]);
    } else {
        snprintf(buf, sizeof(buf), "Target %d of %d\n%s", s_cal_index + 1, kCalTargetCount, progress);
    }
    lv_label_set_text(s_cal_status_label, buf);
}

static void reset_calibration_sequence(void)
{
    /* Start every fresh attempt from a clean slate (2026-09-23 hardware
     * bug: after two runs with a large residual error, a THIRD run's taps
     * -- reported through touch_read(), which always applies whatever
     * correction is currently persisted -- came back distorted enough that
     * tapping the on-screen center target registered as a hit on the
     * (unrelated, far away) Restart button instead. Corrections would
     * otherwise compound run over run instead of each one fitting fresh
     * against the known-good touch_lerp() baseline. Restart already meant
     * "start over"; it now also means "and stop trusting the last attempt". */
    touch_get_calibration(&s_cal_prev_scale_x1000, &s_cal_prev_offset_x,
                          &s_cal_prev_scale_y1000, &s_cal_prev_offset_y);
    touch_reset_calibration();
    /* Re-suspend touch-only activation of Back/Restart for this fresh run
     * (touch_diag_show_deferred() already did this on first entry, but
     * Restart re-enters here too, and finish_calibration_sequence() lifts
     * it once a run's outcome is known -- see its own comment). */
    board_ui_nav_suppress_touch_confirm(true);
    cancel_result_banner();
    s_cal_index = 0;
    s_cal_done = false;
    s_cal_taps_collected = 0;
    s_cal_sum_x = 0;
    s_cal_sum_y = 0;
    s_pending_tap.valid = false;
    s_cal_last_progress_us = esp_timer_get_time();
    /* Far enough in the past that the very first tap of a new run is never
     * itself rejected by the cooldown check. */
    s_cal_last_tap_us = s_cal_last_progress_us - kCalTapCooldownUs;
    /* Entering/restarting this screen itself triggers a full refresh
     * (switch_screen()) that hasn't happened yet at this point -- target
     * 1's own crosshair only becomes visible once THAT completes, same
     * flush-count reasoning as every later target change (see
     * s_cal_target_shown_flush's comment). */
    s_cal_target_shown_flush = lv_port_disp_get_flush_count();
    lv_obj_clear_flag(s_cal_cross_h, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_cal_cross_v, LV_OBJ_FLAG_HIDDEN);
    move_cross(kCalTargets[0].x, kCalTargets[0].y);
    update_status_label();
}

static void finish_calibration_sequence(void)
{
    s_cal_done = true;
    lv_obj_add_flag(s_cal_cross_h, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_cal_cross_v, LV_OBJ_FLAG_HIDDEN);

    int16_t expected_x[kCalTargetCount], expected_y[kCalTargetCount];
    for (int i = 0; i < kCalTargetCount; i++) {
        expected_x[i] = kCalTargets[i].x;
        expected_y[i] = kCalTargets[i].y;
    }
    bool persisted = false;
    int32_t max_err = touch_calibrate(s_cal_measured_x, s_cal_measured_y, expected_x, expected_y,
                                      kCalTargetCount, &persisted);
    ESP_LOGI(TAG, "calibration done, max residual error %ld px, persisted=%d",
             (long)max_err, (int)persisted);

    /* "Saved" is only ever claimed once touch_calibrate() has actually
     * read the values back from NVS and confirmed them (2026-09-23 user
     * question: "comment sait-on que la calibration est sauvegardee?") --
     * not just because the fit/write calls didn't throw. If persistence
     * couldn't be verified, say so plainly: the correction is still active
     * for THIS session, but won't survive a reboot. */
    /* max_err == -1: touch.c's calibration_fit_is_sane() rejected the fit
     * outright and never applied or persisted it (2026-09-23 incident: an
     * unsafe fit briefly made every future tap register as Y=0) -- the
     * previous correction (identity, since this run started fresh) is
     * untouched, not just "not saved". */
    bool rejected = max_err < 0;
    bool success = !rejected && persisted && max_err <= kCalPassThresholdPx;

    /* Any outcome other than a clean success restores whatever was
     * actually working before this run started (saved by
     * reset_calibration_sequence(), since every run resets to identity
     * first) -- 2026-09-24 user request: FAILED should never leave the
     * device on a worse (or, previously, an untested identity) correction
     * than it had before trying. */
    if (!success) {
        touch_set_calibration(s_cal_prev_scale_x1000, s_cal_prev_offset_x,
                              s_cal_prev_scale_y1000, s_cal_prev_offset_y);
    }

    if (rejected) {
        snprintf(s_cal_result_detail, sizeof(s_cal_result_detail),
                 "Fit rejected as unsafe.\nPrevious correction restored.\nRestart to retry");
    } else if (!persisted) {
        snprintf(s_cal_result_detail, sizeof(s_cal_result_detail),
                 "New fit wasn't saved!\nPrevious correction restored.\nMax error: %ldpx", (long)max_err);
    } else if (success) {
        snprintf(s_cal_result_detail, sizeof(s_cal_result_detail),
                 "Calibration saved.\nMax error: %ldpx -- PASS", (long)max_err);
    } else {
        snprintf(s_cal_result_detail, sizeof(s_cal_result_detail),
                 "Error %ldpx too high.\nPrevious correction restored.\nRestart to retry", (long)max_err);
    }

    /* One-word answer first (see kCalResultBannerMs's doc comment above);
     * the detail above replaces it once the timer fires. */
    lv_label_set_text(s_cal_status_label, success ? "SUCCESS" : "FAILED");
    cancel_result_banner();
    s_cal_result_timer = lv_timer_create(cal_result_timer_cb, kCalResultBannerMs, nullptr);
    lv_timer_set_repeat_count(s_cal_result_timer, 1);

    /* Lift Back/Restart's physical-button-only restriction now that the
     * outcome (and, if it failed, the restored correction) is final --
     * 2026-09-24 user request: whichever correction is now in effect
     * (the new one on success, the restored old one otherwise) should be
     * immediately testable with a normal touch tap on Back, doubling as a
     * live confirmation that touch itself still works either way. */
    board_ui_nav_suppress_touch_confirm(false);
}

static void advance_calibration_deferred(void *user_data)
{
    (void)user_data;
    if (s_cal_done || !s_pending_tap.valid) {
        return;
    }
    s_pending_tap.valid = false;

    int64_t now = esp_timer_get_time();
    if (now - s_cal_last_tap_us < kCalTapCooldownUs) {
        /* Too soon after the last COUNTED tap -- almost certainly the same
         * physical touch bouncing, not a second deliberate tap. Drop it
         * silently rather than let it corrupt this target's average (see
         * kCalTapCooldownUs's own comment for why this matters). */
        ESP_LOGW(TAG, "tap %lldus after the last one -- ignored as a probable bounce",
                 (long long)(now - s_cal_last_tap_us));
        return;
    }
    if (lv_port_disp_get_flush_count() == s_cal_target_shown_flush) {
        /* No flush has completed yet since THIS target's crosshair was
         * requested to move here -- the panel provably still shows the
         * previous position, so this tap is aimed at that, not the current
         * target (see s_cal_target_shown_flush's own comment for the
         * incident this prevents: such a tap used to get silently counted
         * toward THIS target anyway, pairing a wrong measurement against
         * its expected grid position). */
        ESP_LOGW(TAG, "tap arrived before the panel redrew this target -- ignored");
        return;
    }
    s_cal_last_tap_us = now;


    s_cal_sum_x += s_pending_tap.x;
    s_cal_sum_y += s_pending_tap.y;
    s_cal_taps_collected++;
    s_cal_last_progress_us = esp_timer_get_time();

    if (s_cal_taps_collected < kTapsPerTarget) {
        /* Still collecting this target's kTapsPerTarget taps -- update the
         * "1 [ok] 2 [ok] 3" progress but don't move to the next target or
         * touch the crosshair yet. */
        update_status_label();
        return;
    }

    s_cal_measured_x[s_cal_index] = (int16_t)(s_cal_sum_x / kTapsPerTarget);
    s_cal_measured_y[s_cal_index] = (int16_t)(s_cal_sum_y / kTapsPerTarget);
    s_cal_sum_x = 0;
    s_cal_sum_y = 0;
    s_cal_taps_collected = 0;

    s_cal_index++;
    if (s_cal_index >= kCalTargetCount) {
        finish_calibration_sequence();
    } else {
        s_cal_target_shown_flush = lv_port_disp_get_flush_count();
        move_cross(kCalTargets[s_cal_index].x, kCalTargets[s_cal_index].y);
        update_status_label();
    }
}

/* Fires on LV_EVENT_PRESSED (not CLICKED) so a target registers the moment
 * it's touched, matching "tap the target" rather than requiring a clean
 * release inside it. `content` itself is the clickable target (see
 * build_touch_diag()) -- the crosshair is a non-clickable sibling, so
 * LVGL's hit-testing falls through to content underneath it regardless of
 * where within the content area the tap lands. */
static void content_pressed_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    s_pending_tap.x = static_cast<int16_t>(point.x);
    s_pending_tap.y = static_cast<int16_t>(point.y);
    s_pending_tap.valid = true;
    ui_defer(advance_calibration_deferred, nullptr);
}

static void restart_deferred(void *user_data)
{
    (void)user_data;
    reset_calibration_sequence();
}

/* Same physical-button-only guard as ui_widgets.cpp's back_event_cb (see
 * its own comment) -- Restart is this screen's second escape-hatch-
 * adjacent control, so it gets the identical treatment: a direct touch
 * tap on it is rejected while this screen has suppression turned on
 * (touch_diag_show_deferred(), below), Left/Right + Power is unaffected. */
static void restart_cb(lv_event_t *e)
{
    if (lv_indev_get_act() != nullptr && board_ui_nav_is_touch_confirm_suppressed()) {
        return;
    }
    ui_defer(restart_deferred, nullptr);
}

/* Escape hatch, last resort (see file header): if NOTHING has advanced the
 * sequence for kCalIdleTimeoutUs -- consistent with touch being entirely
 * unresponsive -- leave on its own rather than trusting the user to
 * remember Left/Right + Power. Not ui_defer()'d: this is a plain lv_timer
 * callback, not a click/value-changed event running inside indev's own
 * dispatch, so the hazard ui_defer() exists for doesn't apply here (same
 * category as battery_timer_cb, ui_nav.cpp, which doesn't defer either). */
static void cal_timeout_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_cal_done) {
        return;
    }
    if (esp_timer_get_time() - s_cal_last_progress_us >= kCalIdleTimeoutUs) {
        ESP_LOGW(TAG, "no calibration progress for %lds -- leaving back to Settings",
                 (long)(kCalIdleTimeoutUs / 1000000));
        switch_screen(Screen::Settings);
    }
}

static void touch_diag_show_deferred(void *user_data)
{
    (void)user_data;
    /* Back/Restart become physical-button-only for as long as this screen
     * is up (see their own click handlers' comments, and
     * board_ui_nav_suppress_touch_confirm()'s doc in ui_nav.h) -- this
     * screen's own touch handling is exactly what's under test, so its
     * escape-hatch controls should not depend on it working correctly. */
    board_ui_nav_suppress_touch_confirm(true);
    reset_calibration_sequence();
    if (!s_cal_timeout_timer) {
        s_cal_timeout_timer = lv_timer_create(cal_timeout_timer_cb, 1000, nullptr);
    }
}

static void touch_diag_show_cb(lv_event_t *e)
{
    (void)e;
    ui_defer(touch_diag_show_deferred, nullptr);
}

static void touch_diag_hide_deferred(void *user_data)
{
    (void)user_data;
    board_ui_nav_suppress_touch_confirm(false);
    if (s_cal_timeout_timer) {
        lv_timer_delete(s_cal_timeout_timer);
        s_cal_timeout_timer = nullptr;
    }
    cancel_result_banner();
}

static void touch_diag_hide_cb(lv_event_t *e)
{
    (void)e;
    ui_defer(touch_diag_hide_deferred, nullptr);
}

lv_obj_t *build_touch_diag(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;

    lv_obj_t *screen = make_screen();
    /* "< Back" is added to `group` first, inside add_back_header() -- see
     * the file header comment: this makes it the escape hatch's default
     * focus, before Restart (added further below) joins the same group. */
    add_back_header(screen, "Calibration", Screen::Settings, group, battery_label_out);

    /* Restart sits in the header band, mirroring "< Back" on the right,
     * NOT inside the content/grid area (2026-09-23 hardware bug: after a
     * couple of bad-fit runs, a tap on the on-screen CENTER target was
     * reported -- through the not-yet-fixed correction, see
     * reset_calibration_sequence()'s own comment -- far enough off to land
     * on Restart instead, silently wiping the run in progress). Placing it
     * in the header band makes that impossible by construction: the grid
     * (kCalGridTop=500 and below) is nowhere near y=kBackTopMargin, no
     * matter how wrong a stale correction is. */
    lv_obj_t *restart_btn = make_button(screen, "Restart");
    lv_obj_align(restart_btn, LV_ALIGN_TOP_RIGHT, -16, kBackTopMargin);
    lv_obj_add_event_cb(restart_btn, restart_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(restart_btn, group);

    lv_obj_t *content = make_content(screen);
    /* Push the status/hint text down between grid rows 1 (y=200) and 2
     * (y=460) so the top row's crosshair doesn't sit on top of it. */
    lv_obj_set_style_pad_top(content, 88, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(content, content_pressed_cb, LV_EVENT_PRESSED, nullptr);

    s_cal_status_label = lv_label_create(content);
    lv_obj_set_style_text_align(s_cal_status_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *hint = lv_label_create(content);
    lv_label_set_text(hint, "Touch not responding?\nLeft/Right then Power = Back");
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

    make_cross(screen, &s_cal_cross_h, &s_cal_cross_v);

    lv_obj_add_event_cb(screen, touch_diag_show_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    lv_obj_add_event_cb(screen, touch_diag_hide_cb, LV_EVENT_SCREEN_UNLOADED, nullptr);

    return screen;
}
