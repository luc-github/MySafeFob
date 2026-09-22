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
 * @brief MySafeFob App — Settings > Touch Calibration: live x/y readout +
 *        calibration ticks/crosses. The only screen with its own periodic
 *        lv_timer (live while loaded; deliberately NOT done on any other
 *        screen, which stay event-driven).
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "lv_port_indev.h"

extern "C" {
#include "hw_config.h"
}

#include <cstdio>
#include <cstring>

static lv_obj_t *s_touch_diag_label;
static lv_timer_t *s_touch_diag_timer;

static void touch_diag_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    touch_point_t tp = lv_port_indev_get_last_touch();
    char buf[128];
    snprintf(buf, sizeof(buf), "pressed: %s\nx=%d y=%d\nraw_x=%d raw_y=%d",
              tp.pressed ? "yes" : "no", tp.x, tp.y, tp.raw_x, tp.raw_y);

    /* lv_label_set_text() invalidates unconditionally, changed or not --
     * this timer used to do that every 300ms regardless, flushing the
     * whole e-paper screen on a fixed clock even with nobody touching it
     * (found on hardware 2026-09-21: "l'écran clignote de temps en temps
     * alors que personne ne le touche"). Only actually set/redraw when
     * the text would genuinely differ. */
    static char s_last_shown[128] = "";
    if (strcmp(buf, s_last_shown) != 0) {
        strcpy(s_last_shown, buf);
        lv_label_set_text(s_touch_diag_label, buf);
    }
}

static void touch_diag_show_deferred(void *user_data)
{
    (void)user_data;
    if (!s_touch_diag_timer) {
        s_touch_diag_timer = lv_timer_create(touch_diag_timer_cb, 300, nullptr);
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
    if (s_touch_diag_timer) {
        lv_timer_delete(s_touch_diag_timer);
        s_touch_diag_timer = nullptr;
    }
}

static void touch_diag_hide_cb(lv_event_t *e)
{
    (void)e;
    ui_defer(touch_diag_hide_deferred, nullptr);
}

/* Calibration ticks (2026-09-21, user request): docs/touch-calibration-notes.md
 * §9/§10 have an open question -- exactly how wide is the capacitive dead
 * zone near the panel edges -- that was so far only ever answered with
 * rough, off-the-cuff estimates, never a real measurement. A column of
 * marks at known absolute y values (independent of any flex/theme
 * layout -- lv_obj_set_pos() straight on the screen, same technique as
 * the 5-cross test screen that found the I1 palette-offset bug) lets
 * whoever's testing tap next to each one and read off, from the live
 * readout below, the smallest y that still registers a press. Placed at
 * x=380+ to stay clear of the header's Back button/title. */
static void add_calibration_ticks(lv_obj_t *screen)
{
    constexpr int kTickCount = 20;
    constexpr int kTickStep = 40;
    constexpr int kTickFirstY = 10;
    for (int i = 0; i < kTickCount; i++) {
        int y = kTickFirstY + i * kTickStep;

        lv_obj_t *tick = lv_obj_create(screen);
        lv_obj_remove_style_all(tick);
        lv_obj_set_style_bg_color(tick, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
        lv_obj_set_size(tick, 24, 4);
        lv_obj_set_pos(tick, 380, y);

        char buf[8];
        snprintf(buf, sizeof(buf), "%d", y);
        lv_obj_t *label = lv_label_create(screen);
        lv_label_set_text(label, buf);
        lv_obj_set_pos(label, 408, y - 10);
    }
}

/* One cross (two thin crossing bars) at an absolute screen position,
 * lv_obj_set_pos() straight on the screen like add_calibration_ticks()
 * above -- independent of any flex/theme layout. Originally a throwaway
 * diagnostic screen used to find the I1 palette-offset rendering bug
 * (2026-09-21); brought back here (user request) as a permanent visual
 * reference for the 4 corners + center of the panel. */
static void add_cross(lv_obj_t *screen, int cx, int cy)
{
    constexpr int kArmLen = 24;
    constexpr int kArmThickness = 4;

    lv_obj_t *h = lv_obj_create(screen);
    lv_obj_remove_style_all(h);
    lv_obj_set_style_bg_color(h, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_set_size(h, kArmLen, kArmThickness);
    lv_obj_set_pos(h, cx - kArmLen / 2, cy - kArmThickness / 2);

    lv_obj_t *v = lv_obj_create(screen);
    lv_obj_remove_style_all(v);
    lv_obj_set_style_bg_color(v, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
    lv_obj_set_size(v, kArmThickness, kArmLen);
    lv_obj_set_pos(v, cx - kArmThickness / 2, cy - kArmLen / 2);
}

/* 4 corners + center, margined off the panel edges (SCREEN_WIDTH/HEIGHT,
 * hw_config.h) so each cross's arms stay fully visible. */
static void add_calibration_crosses(lv_obj_t *screen)
{
    constexpr int kMargin = 30;
    add_cross(screen, kMargin, kMargin);
    add_cross(screen, SCREEN_WIDTH - kMargin, kMargin);
    add_cross(screen, kMargin, SCREEN_HEIGHT - kMargin);
    add_cross(screen, SCREEN_WIDTH - kMargin, SCREEN_HEIGHT - kMargin);
    add_cross(screen, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
}

lv_obj_t *build_touch_diag(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, "Calibration", Screen::Settings, group, battery_label_out);
    lv_obj_t *content = make_content(screen);

    s_touch_diag_label = lv_label_create(content);
    lv_label_set_text(s_touch_diag_label, "pressed: no");
    lv_obj_set_style_text_align(s_touch_diag_label, LV_TEXT_ALIGN_CENTER, 0);

    add_calibration_ticks(screen);
    add_calibration_crosses(screen);

    lv_obj_add_event_cb(screen, touch_diag_show_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    lv_obj_add_event_cb(screen, touch_diag_hide_cb, LV_EVENT_SCREEN_UNLOADED, nullptr);

    return screen;
}
