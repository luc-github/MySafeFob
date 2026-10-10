/*
 Project: MySafeFob  ui_screen_names.cpp
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
 * @file ui_screen_names.cpp
 * @brief MySafeFob App — NAMES (ADR-019, UI-SPECS.md §2.2 amendment): the
 *        account names of the letter chosen on HOME, sorted, one row each
 *        (name only, never a secret); tap or Left/Right+confirm opens the
 *        ACCOUNT page. No scrolling on e-paper: pages of kRowsPerPage rows
 *        with < > buttons (feedback 2026-10-01), "+ Add" at the bottom.
 *
 *        Rows are placed at fixed y with touch_safe_y() (X4 Pro touch band,
 *        ui_widgets.h): 4 rows above the band, 2 below, and the gap between
 *        holds a status line, which is not a touch target.
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

extern "C" {
#include "accounts.h"
#include "esp_log.h"
}

#include "app_log_workaround.h"

#include <cstdio>

static const char *TAG = "names";

static constexpr int kRowsPerPage = 6;
static constexpr int32_t kRowTop = 178;
static constexpr int32_t kRowH = kMinTouchTarget;   /* 56 */
static constexpr int32_t kRowGap = 28;
static constexpr int32_t kPagerY = 720;
static constexpr int32_t kArrowW = 72;
static constexpr int32_t kSideMargin = 24;

static char s_letter = 'A';
static int s_page;
static int s_count;
static account_summary_t s_shown[kRowsPerPage];
static lv_group_t *s_group;
static lv_obj_t *s_title;
static lv_obj_t *s_rows[kRowsPerPage];
static lv_obj_t *s_row_labels[kRowsPerPage];
static lv_obj_t *s_prev;
static lv_obj_t *s_next;
static lv_obj_t *s_page_label;
static lv_obj_t *s_status;

void names_open(char letter)
{
    if (letter != s_letter) s_page = 0;
    s_letter = letter;
    switch_screen(Screen::Names);
}

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void show_page(void)
{
    s_count = accounts_count(s_letter);
    int pages = s_count == 0 ? 1 : (s_count + kRowsPerPage - 1) / kRowsPerPage;
    if (s_page >= pages) s_page = pages - 1;
    if (s_page < 0) s_page = 0;
    int n = accounts_list(s_letter, s_page * kRowsPerPage, kRowsPerPage, s_shown);
    for (int slot = 0; slot < kRowsPerPage; slot++) {
        if (slot < n) lv_label_set_text(s_row_labels[slot], s_shown[slot].name);
        set_hidden(s_rows[slot], slot >= n);
    }
    set_hidden(s_prev, s_page == 0);
    set_hidden(s_next, s_page >= pages - 1);
    set_hidden(s_page_label, pages <= 1);
    char buf[32];   /* two full-range ints: GCC checks the worst case */
    snprintf(buf, sizeof(buf), "%d / %d", s_page + 1, pages);
    lv_label_set_text(s_page_label, buf);
    char title[8];
    snprintf(title, sizeof(title), "%c", s_letter);
    lv_label_set_text(s_title, title);
    if (n > 0) lv_group_focus_obj(s_rows[0]);
}

static void row_deferred(void *user_data)
{
    int slot = static_cast<int>(reinterpret_cast<intptr_t>(user_data));
    if (slot >= kRowsPerPage || lv_obj_has_flag(s_rows[slot], LV_OBJ_FLAG_HIDDEN)) return;
    ESP_LOGI(TAG, "open account %u (%s)", s_shown[slot].id, s_shown[slot].name);
    account_open(s_shown[slot].id);
}

static void row_cb(lv_event_t *e)
{
    ui_defer(row_deferred, lv_event_get_user_data(e));
}

static void page_deferred(void *user_data)
{
    s_page += static_cast<int>(reinterpret_cast<intptr_t>(user_data));
    lv_label_set_text(s_status, "");
    show_page();
    lv_port_disp_request_full_refresh();   /* every row changes: a clean page */
}

static void page_cb(lv_event_t *e)
{
    ui_defer(page_deferred, lv_event_get_user_data(e));
}

static void add_deferred(void *)
{
    ESP_LOGI(TAG, "add requested (letter %c)", s_letter);
    account_edit_open_new(s_letter);
}

static void add_cb(lv_event_t *)
{
    ui_defer(add_deferred, nullptr);
}

/* Re-read on every visit: an account may have been removed meanwhile. A
 * letter left empty sends the user back to HOME. */
static void screen_loaded_deferred(void *)
{
    lv_label_set_text(s_status, "");
    show_page();
    if (s_count == 0) switch_screen(Screen::Home);
}

static void screen_loaded_cb(lv_event_t *)
{
    ui_defer(screen_loaded_deferred, nullptr);
}

static lv_obj_t *make_fixed_button(lv_obj_t *screen, const char *text, int32_t w)
{
    lv_obj_t *btn = make_button(screen, text);
    lv_obj_set_style_min_width(btn, 0, 0);
    lv_obj_set_size(btn, w, kRowH);
    lv_obj_set_ext_click_area(btn, 0);   /* gaps < 2 x kExtClickMargin: hit areas must not overlap */
    add_to_group(btn, s_group);
    return btn;
}

lv_obj_t *build_names(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    s_group = lv_port_indev_new_group();
    *group_out = s_group;

    lv_obj_t *screen = make_screen();
    lv_obj_t *bar = add_back_header(screen, "A", Screen::Home, s_group, battery_label_out);
    s_title = lv_obj_get_child(bar, 0);

    int32_t y = kRowTop;
    for (int slot = 0; slot < kRowsPerPage; slot++) {
        y = touch_safe_y(y, kRowH);
        lv_obj_t *row = make_fixed_button(screen, "", LV_PCT(90));
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(slot)));
        lv_obj_t *label = lv_obj_get_child(row, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, LV_PCT(80));
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_t *arrow = lv_label_create(row);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -12, 0);
        s_rows[slot] = row;
        s_row_labels[slot] = label;
        y += kRowH + kRowGap;
    }

    /* Status line in the gap the touch band leaves between rows 4 and 5. */
    s_status = lv_label_create(screen);
    lv_label_set_text(s_status, "");
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, kTouchBandTop + 10);

    s_prev = make_fixed_button(screen, LV_SYMBOL_LEFT, kArrowW);
    lv_obj_align(s_prev, LV_ALIGN_TOP_LEFT, kSideMargin, kPagerY);
    lv_obj_add_event_cb(s_prev, page_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(-1)));
    s_page_label = lv_label_create(screen);
    lv_obj_align(s_page_label, LV_ALIGN_TOP_LEFT, kSideMargin + kArrowW + 16, kPagerY + 14);
    s_next = make_fixed_button(screen, LV_SYMBOL_RIGHT, kArrowW);
    lv_obj_align(s_next, LV_ALIGN_TOP_LEFT, kSideMargin + kArrowW + 96, kPagerY);
    lv_obj_add_event_cb(s_next, page_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(1)));
    lv_obj_t *add = make_fixed_button(screen, "+ Add", 140);
    lv_obj_align(add, LV_ALIGN_TOP_RIGHT, -kSideMargin, kPagerY);
    lv_obj_add_event_cb(add, add_cb, LV_EVENT_CLICKED, nullptr);

    show_page();
    lv_obj_add_event_cb(screen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    return screen;
}
