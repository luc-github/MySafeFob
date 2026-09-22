/*
 Project: MySafeFob  ui_screen_home.cpp
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
 * @file ui_screen_home.cpp
 * @brief MySafeFob App — Home screen (Settings icon + "Sleep now").
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "lv_port_indev.h"

static void open_settings_deferred(void *user_data)
{
    (void)user_data;
    switch_screen(Screen::Settings);
}

static void open_settings_cb(lv_event_t *e)
{
    (void)e;
    ui_defer(open_settings_deferred, nullptr);
}

static void sleep_now_deferred(void *user_data)
{
    (void)user_data;
    enter_sleep_from_idle_or_menu("menu");
}

static void sleep_now_cb(lv_event_t *e)
{
    (void)e;
    ui_defer(sleep_now_deferred, nullptr);
}

lv_obj_t *build_home(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;

    lv_obj_t *screen = make_screen();

    /* Same header shape as every other screen (battery top-right, divider,
     * a button below it) but with no title/Back -- Home has neither a
     * parent screen nor a name of its own -- and "Settings" (icon,
     * 2026-09-21 user request) in Back's usual slot instead. */
    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), kHeaderInfoHeight);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(bar, 16, 0);
    lv_obj_set_style_pad_top(bar, kHeaderContentPadTop, 0);   /* see add_back_header()'s twin comment */
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    *battery_label_out = make_battery_label(bar);

    add_divider_below(screen, bar);

    /* Icon-only, not the 140px make_button() default meant for text labels
     * like "< Back" (2026-09-21 request: "peut etre plus petit pas besoin
     * qu'il soit si large"). */
    lv_obj_t *settings_btn = make_button(screen, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_min_width(settings_btn, kMinTouchTarget, 0);
    lv_obj_align(settings_btn, LV_ALIGN_TOP_LEFT, 16, kBackTopMargin);
    lv_obj_add_event_cb(settings_btn, open_settings_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(settings_btn, group);

    lv_obj_t *content = make_content(screen);

    lv_obj_t *title = lv_label_create(content);
    lv_label_set_text(title, "MySafeFob");

    lv_obj_t *sleep_btn = make_button(content, "Sleep now");
    lv_obj_set_size(sleep_btn, LV_PCT(70), 70);
    lv_obj_add_event_cb(sleep_btn, sleep_now_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(sleep_btn, group);

    lv_obj_t *hint = lv_label_create(content);
    lv_label_set_text(hint, "Left/Right = focus\nTouch/Power = confirm");
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

    return screen;
}
