/*
 Project: MySafeFob  ui_screen_settings_about.cpp
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
 * @file ui_screen_settings_about.cpp
 * @brief MySafeFob App — Settings > About (version, build date, free RAM).
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "lv_port_indev.h"

extern "C" {
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
}

#include <cstdio>

lv_obj_t *build_settings_about(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, "About", Screen::Settings, group, battery_label_out);
    lv_obj_t *content = make_content(screen);

    lv_obj_t *label = lv_label_create(content);
    const esp_app_desc_t *app_desc = esp_app_get_description();
    /* MALLOC_CAP_DEFAULT covers every heap region (internal + the 8MB
     * PSRAM) LVGL/the app could allocate from -- the combined figure a
     * user asking "how much RAM" (2026-09-21 request) actually means,
     * not just one region. */
    size_t ram_total = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    size_t ram_free = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    char buf[220];
    snprintf(buf, sizeof(buf), "MySafeFob\nboard: x4pro\nversion: %s\nbuilt: %s %s\nRAM: %u/%u KB free",
             app_desc->version, app_desc->date, app_desc->time,
             static_cast<unsigned>(ram_free / 1024), static_cast<unsigned>(ram_total / 1024));
    lv_label_set_text(label, buf);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    return screen;
}
