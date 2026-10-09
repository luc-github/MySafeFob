/*
 Project: MySafeFob  ui_screen_entry_list.cpp
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
 * @file ui_screen_entry_list.cpp
 * @brief MySafeFob App — generic entry list (UI-SPECS.md §2.3 TOTP_LIST,
 *        §2.6 PWD_LIST, §2.9 RCV_LIST; ROADMAP 8.0 P8): one row per entry,
 *        labels only (never a secret), tap or Left/Right+confirm to select,
 *        "+ Add" at the bottom. No scrolling on e-paper: pages of
 *        kRowsPerPage rows with < > buttons (feedback 2026-10-01).
 *
 *        Rows are placed at fixed y with touch_safe_y() (X4 Pro touch band,
 *        ui_widgets.h): 4 rows above the band, 2 below, and the gap between
 *        holds a status line, which is not a touch target.
 *
 *        Dummy data for now (P8): the entries will come from secret_store.
 *        Selecting a row or "+ Add" only shows a status line until the
 *        target screens exist (TOTP_CODE is P9).
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

extern "C" {
#include "esp_log.h"
}

#include "app_log_workaround.h"

#include <cstdio>

static const char *TAG = "entry_list";

static constexpr int kRowsPerPage = 6;
static constexpr int32_t kRowTop = 178;
static constexpr int32_t kRowH = kMinTouchTarget;   /* 56 */
static constexpr int32_t kRowGap = 28;
static constexpr int32_t kPagerY = 720;
static constexpr int32_t kArrowW = 72;
static constexpr int32_t kSideMargin = 24;

enum ListKind { kListTotp, kListPwd, kListRcv, kListCount };

struct ListState {
    const char *const *labels;   /* dummy entries (P8) */
    int count;
    const char *add_text;
    int page;
    lv_group_t *group;
    lv_obj_t *rows[kRowsPerPage];
    lv_obj_t *row_labels[kRowsPerPage];
    lv_obj_t *prev;
    lv_obj_t *next;
    lv_obj_t *page_label;
    lv_obj_t *status;
    lv_obj_t *empty_label;
};

/* Dummy data: enough TOTP accounts for 3 pages, a few passwords for a
 * single page, no recovery codes for the empty state. */
static const char *const kTotpDummy[] = {"GitHub",  "Google",     "AWS",       "Microsoft", "Dropbox",
                                         "GitLab",  "Proton",     "Bitwarden", "Cloudflare", "Discord",
                                         "Reddit",  "Mastodon",   "Amazon",    "PayPal"};
static const char *const kPwdDummy[] = {"Home Wi-Fi", "Bank", "Email", "NAS admin"};

static ListState s_lists[kListCount] = {
    {kTotpDummy, sizeof(kTotpDummy) / sizeof(kTotpDummy[0]), "+ Add", 0, nullptr, {}, {}, nullptr, nullptr, nullptr, nullptr, nullptr},
    {kPwdDummy, sizeof(kPwdDummy) / sizeof(kPwdDummy[0]), "+ Add", 0, nullptr, {}, {}, nullptr, nullptr, nullptr, nullptr, nullptr},
    {nullptr, 0, "+ Add", 0, nullptr, {}, {}, nullptr, nullptr, nullptr, nullptr, nullptr},
};

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static int page_count(const ListState *st)
{
    return st->count == 0 ? 1 : (st->count + kRowsPerPage - 1) / kRowsPerPage;
}

static void show_page(ListState *st)
{
    int pages = page_count(st);
    if (st->page >= pages) st->page = pages - 1;
    if (st->page < 0) st->page = 0;
    for (int slot = 0; slot < kRowsPerPage; slot++) {
        int index = st->page * kRowsPerPage + slot;
        bool used = index < st->count;
        if (used) lv_label_set_text(st->row_labels[slot], st->labels[index]);
        set_hidden(st->rows[slot], !used);
    }
    set_hidden(st->prev, st->page == 0);
    set_hidden(st->next, st->page >= pages - 1);
    set_hidden(st->page_label, pages <= 1);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d / %d", st->page + 1, pages);
    lv_label_set_text(st->page_label, buf);
    set_hidden(st->empty_label, st->count != 0);
    if (st->count > 0) lv_group_focus_obj(st->rows[0]);
}

static void row_deferred(void *user_data)
{
    lv_obj_t *row = static_cast<lv_obj_t *>(user_data);
    ListState *st = static_cast<ListState *>(lv_obj_get_user_data(row));
    int slot = 0;
    while (slot < kRowsPerPage && st->rows[slot] != row) slot++;
    int index = st->page * kRowsPerPage + slot;
    if (slot == kRowsPerPage || index >= st->count) return;
    ESP_LOGI(TAG, "selected entry %d: %s", index, st->labels[index]);
    char buf[64];
    snprintf(buf, sizeof(buf), "Selected: %s", st->labels[index]);
    lv_label_set_text(st->status, buf);
}

static void row_cb(lv_event_t *e)
{
    ui_defer(row_deferred, lv_event_get_target_obj(e));
}

struct PageStep {
    ListState *st;
    int delta;
};

static void page_deferred(void *user_data)
{
    const PageStep *step = static_cast<const PageStep *>(user_data);
    step->st->page += step->delta;
    lv_label_set_text(step->st->status, "");
    show_page(step->st);
    lv_port_disp_request_full_refresh();   /* every row changes: a clean page */
}

static void page_cb(lv_event_t *e)
{
    ui_defer(page_deferred, lv_event_get_user_data(e));
}

static void add_deferred(void *user_data)
{
    ListState *st = static_cast<ListState *>(user_data);
    ESP_LOGI(TAG, "add requested");
    lv_label_set_text(st->status, "Add: not built yet");
}

static void add_cb(lv_event_t *e)
{
    ui_defer(add_deferred, lv_event_get_user_data(e));
}

static void screen_loaded_cb(lv_event_t *e)
{
    ListState *st = static_cast<ListState *>(lv_event_get_user_data(e));
    lv_label_set_text(st->status, "");
    show_page(st);
}

static lv_obj_t *make_fixed_button(lv_obj_t *screen, lv_group_t *group, const char *text, int32_t w)
{
    lv_obj_t *btn = make_button(screen, text);
    lv_obj_set_style_min_width(btn, 0, 0);
    lv_obj_set_size(btn, w, kRowH);
    lv_obj_set_ext_click_area(btn, 0);   /* gaps < 2 x kExtClickMargin: hit areas must not overlap */
    add_to_group(btn, group);
    return btn;
}

static lv_obj_t *build_list(ListKind kind, const char *title, lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    ListState *st = &s_lists[kind];
    st->group = lv_port_indev_new_group();
    *group_out = st->group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, title, Screen::Home, st->group, battery_label_out);

    int32_t y = kRowTop;
    for (int slot = 0; slot < kRowsPerPage; slot++) {
        y = touch_safe_y(y, kRowH);
        lv_obj_t *row = make_fixed_button(screen, st->group, "", LV_PCT(90));
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
        lv_obj_set_user_data(row, st);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *label = lv_obj_get_child(row, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, LV_PCT(80));
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_t *arrow = lv_label_create(row);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -12, 0);
        st->rows[slot] = row;
        st->row_labels[slot] = label;
        y += kRowH + kRowGap;
    }

    /* Status line in the gap the touch band leaves between rows 4 and 5. */
    st->status = lv_label_create(screen);
    lv_label_set_text(st->status, "");
    lv_obj_align(st->status, LV_ALIGN_TOP_MID, 0, kTouchBandTop + 10);

    st->empty_label = lv_label_create(screen);
    lv_label_set_text(st->empty_label, "No entries yet");
    lv_obj_align(st->empty_label, LV_ALIGN_TOP_MID, 0, kRowTop + kRowH);

    static PageStep steps[kListCount][2];
    steps[kind][0] = {st, -1};
    steps[kind][1] = {st, +1};
    st->prev = make_fixed_button(screen, st->group, LV_SYMBOL_LEFT, kArrowW);
    lv_obj_align(st->prev, LV_ALIGN_TOP_LEFT, kSideMargin, kPagerY);
    lv_obj_add_event_cb(st->prev, page_cb, LV_EVENT_CLICKED, &steps[kind][0]);
    st->page_label = lv_label_create(screen);
    lv_obj_align(st->page_label, LV_ALIGN_TOP_LEFT, kSideMargin + kArrowW + 16, kPagerY + 14);
    st->next = make_fixed_button(screen, st->group, LV_SYMBOL_RIGHT, kArrowW);
    lv_obj_align(st->next, LV_ALIGN_TOP_LEFT, kSideMargin + kArrowW + 96, kPagerY);
    lv_obj_add_event_cb(st->next, page_cb, LV_EVENT_CLICKED, &steps[kind][1]);
    lv_obj_t *add = make_fixed_button(screen, st->group, st->add_text, 140);
    lv_obj_align(add, LV_ALIGN_TOP_RIGHT, -kSideMargin, kPagerY);
    lv_obj_add_event_cb(add, add_cb, LV_EVENT_CLICKED, st);

    st->page = 0;
    show_page(st);
    lv_obj_add_event_cb(screen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, st);
    return screen;
}

lv_obj_t *build_totp_list(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    return build_list(kListTotp, "TOTP Codes", group_out, battery_label_out);
}

lv_obj_t *build_pwd_list(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    return build_list(kListPwd, "Passwords", group_out, battery_label_out);
}

lv_obj_t *build_rcv_list(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    return build_list(kListRcv, "Recovery Codes", group_out, battery_label_out);
}
