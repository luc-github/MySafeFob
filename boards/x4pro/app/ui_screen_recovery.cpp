/*
 Project: MySafeFob  ui_screen_recovery.cpp
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
 * @file ui_screen_recovery.cpp
 * @brief MySafeFob App — recovery codes of an account (F-05b, ADR-019):
 *
 *        RECOVERY (from the account page): the codes, numbered, used ones
 *        struck through but kept visible (traceability); tapping an unused
 *        code marks it used, behind a confirmation (irreversible).
 *
 *        EDIT's Recovery tab (2026-10-13, was a separate RECOVERY EDIT
 *        screen; works on EDIT's draft, nothing written until EDIT's Save):
 *        tap a code to remove it (confirmation), "+ Add" types a new code
 *        (FIELD, full keyboard), "Clear all" (confirmation).
 *
 *        Both use one paginated list (no scrolling): rows placed with
 *        touch_safe_y() (X4 Pro touch band), status line in the band's gap,
 *        < > and the page number below. RECOVERY: 6 rows from y 178; the
 *        tab: 4 rows from y 240, inside EDIT's tab container.
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "msf_symbols.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

extern "C" {
#include "accounts.h"
#include "esp_log.h"
}

#include "app_log_workaround.h"

#include <cstdio>
#include <cstring>

static const char *TAG = "recovery";

static constexpr int kRowsPerPage = 6;   /* most rows a list can have */
static constexpr int32_t kRowTop = 178;
static constexpr int32_t kRowH = kMinTouchTarget;
static constexpr int32_t kRowGap = 28;
static constexpr int32_t kPagerY = 720;
static constexpr int32_t kArrowW = 72;
static constexpr int32_t kSideMargin = 24;

/* One paginated code list. */
struct CodeList {
    const account_t *(*source)(void);   /* the record to show */
    void (*on_row)(int index);          /* code index tapped */
    int rows_per_page;                  /* <= kRowsPerPage */
    int page;
    lv_obj_t *rows[kRowsPerPage];
    lv_obj_t *row_labels[kRowsPerPage];
    lv_obj_t *prev, *next, *page_label, *status, *empty_label;
};

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void list_show(CodeList *list)
{
    const account_t *a = list->source();
    int count = a->rcv_count;
    int per_page = list->rows_per_page;
    int pages = count == 0 ? 1 : (count + per_page - 1) / per_page;
    if (list->page >= pages) list->page = pages - 1;
    if (list->page < 0) list->page = 0;
    for (int slot = 0; slot < per_page; slot++) {
        int index = list->page * per_page + slot;
        bool shown = index < count;
        set_hidden(list->rows[slot], !shown);
        if (!shown) continue;
        bool used = (a->rcv_used_mask >> index) & 1u;
        char buf[ACCOUNTS_RCV_CODE_MAX + 32];   /* index (worst case 11 chars) + code + "   used" */
        snprintf(buf, sizeof(buf), "%2d.  %s%s", index + 1, a->rcv_codes[index], used ? "   used" : "");
        lv_label_set_text(list->row_labels[slot], buf);
        lv_obj_set_style_text_decor(list->row_labels[slot], used ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE,
                                    0);
    }
    set_hidden(list->prev, list->page == 0);
    set_hidden(list->next, list->page >= pages - 1);
    set_hidden(list->page_label, pages <= 1);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d / %d", list->page + 1, pages);
    lv_label_set_text(list->page_label, buf);
    set_hidden(list->empty_label, count != 0);
}

struct RowRef {
    CodeList *list;
    int slot;
};

static void row_deferred(void *user_data)
{
    const RowRef *ref = static_cast<const RowRef *>(user_data);
    int index = ref->list->page * ref->list->rows_per_page + ref->slot;
    if (index < ref->list->source()->rcv_count) ref->list->on_row(index);
}

static void row_cb(lv_event_t *e)
{
    ui_defer(row_deferred, lv_event_get_user_data(e));
}

struct PageRef {
    CodeList *list;
    int delta;
};

static void page_deferred(void *user_data)
{
    const PageRef *ref = static_cast<const PageRef *>(user_data);
    ref->list->page += ref->delta;
    lv_label_set_text(ref->list->status, "");
    list_show(ref->list);
    lv_port_disp_request_full_refresh();
}

static void page_cb(lv_event_t *e)
{
    ui_defer(page_deferred, lv_event_get_user_data(e));
}

static lv_obj_t *make_fixed_button(lv_obj_t *parent, lv_group_t *group, const char *text, int32_t w)
{
    lv_obj_t *btn = make_button(parent, text);
    lv_obj_set_style_min_width(btn, 0, 0);
    lv_obj_set_size(btn, w, kRowH);
    lv_obj_set_ext_click_area(btn, 0);
    add_to_group(btn, group);
    return btn;
}

/* Rows, status line, pager (the right part of the pager row stays free).
 * `top` and `pager_y` are screen coordinates (the touch band is placed in
 * screen coordinates); `parent_y` is the parent's own y on the screen. */
static void list_build(CodeList *list, lv_obj_t *parent, int32_t parent_y, lv_group_t *group, const char *empty_text,
                       int32_t top, int rows, int32_t pager_y)
{
    static RowRef row_refs[2][kRowsPerPage];
    static PageRef page_refs[2][2];
    static int used = 0;
    int k = used++;

    list->rows_per_page = rows;
    int32_t y = top;
    for (int slot = 0; slot < rows; slot++) {
        y = touch_safe_y(y, kRowH);
        lv_obj_t *row = make_fixed_button(parent, group, "", 432);   /* 90 % of the screen */
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y - parent_y);
        row_refs[k][slot] = {list, slot};
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, &row_refs[k][slot]);
        lv_obj_t *label = lv_obj_get_child(row, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 12, 0);
        list->rows[slot] = row;
        list->row_labels[slot] = label;
        y += kRowH + kRowGap;
    }

    list->status = lv_label_create(parent);
    lv_label_set_text(list->status, "");
    lv_obj_align(list->status, LV_ALIGN_TOP_MID, 0, kTouchBandTop + 10 - parent_y);

    list->empty_label = lv_label_create(parent);
    lv_label_set_text(list->empty_label, empty_text);
    lv_obj_align(list->empty_label, LV_ALIGN_TOP_MID, 0, top + kRowH - parent_y);

    page_refs[k][0] = {list, -1};
    page_refs[k][1] = {list, +1};
    list->prev = make_fixed_button(parent, group, LV_SYMBOL_LEFT, kArrowW);
    lv_obj_align(list->prev, LV_ALIGN_TOP_LEFT, kSideMargin, pager_y - parent_y);
    lv_obj_add_event_cb(list->prev, page_cb, LV_EVENT_CLICKED, &page_refs[k][0]);
    list->page_label = lv_label_create(parent);
    lv_obj_align(list->page_label, LV_ALIGN_TOP_LEFT, kSideMargin + kArrowW + 16, pager_y + 14 - parent_y);
    list->next = make_fixed_button(parent, group, LV_SYMBOL_RIGHT, kArrowW);
    lv_obj_align(list->next, LV_ALIGN_TOP_LEFT, kSideMargin + kArrowW + 96, pager_y - parent_y);
    lv_obj_add_event_cb(list->next, page_cb, LV_EVENT_CLICKED, &page_refs[k][1]);
}

/* ---- RECOVERY (view, mark used) ---------------------------------------- */

static uint16_t s_view_id;
static account_t s_view;
static CodeList s_view_list;
static int s_pending_index;

void recovery_open(uint16_t id)
{
    s_view_id = id;
    s_view_list.page = 0;
    switch_screen(Screen::RecoveryView);
}

static const account_t *view_source(void)
{
    return &s_view;
}

static void show_unused(void)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%d / %u available", accounts_rcv_unused(&s_view), s_view.rcv_count);
    lv_label_set_text(s_view_list.status, buf);
}

static void mark_confirmed(void *)
{
    ESP_LOGI(TAG, "account %u: code %d marked used", s_view_id, s_pending_index + 1);
    accounts_rcv_mark_used(s_view_id, s_pending_index);
    accounts_get(s_view_id, &s_view);
    list_show(&s_view_list);
    show_unused();
}

static void view_row(int index)
{
    if ((s_view.rcv_used_mask >> index) & 1u) {
        lv_label_set_text(s_view_list.status, "Already used");
        return;
    }
    s_pending_index = index;
    static char message[96];
    snprintf(message, sizeof(message), "Code %d (%s) will be marked\nas used. This cannot be undone.", index + 1,
             s_view.rcv_codes[index]);
    const ui_confirm_t cfg = {"Mark as used?", message, "Mark used", mark_confirmed, nullptr, nullptr, false};
    ui_confirm_show(&cfg);
}

static void view_back_deferred(void *)
{
    account_open(s_view_id);
}

static void view_back_cb(lv_event_t *)
{
    ui_defer(view_back_deferred, nullptr);
}

static void view_loaded_cb(lv_event_t *)
{
    if (!accounts_get(s_view_id, &s_view)) accounts_init_record(&s_view);
    list_show(&s_view_list);
    show_unused();
}

lv_obj_t *build_recovery_view(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;
    lv_obj_t *screen = make_screen();
    add_back_header_cb(screen, MSF_SYMBOL_LIFE_RING " Recovery codes", view_back_cb, nullptr, group, battery_label_out);
    s_view_list.source = view_source;
    s_view_list.on_row = view_row;
    list_build(&s_view_list, screen, 0, group, "No recovery codes", kRowTop, kRowsPerPage, kPagerY);
    lv_obj_add_event_cb(screen, view_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    ui_auto_clear_attach(screen);   /* F-05: codes are secrets */
    return screen;
}

/* ---- EDIT's Recovery tab (draft: add, remove, clear all) --------------- */

static constexpr int32_t kTabRowTop = 240;   /* below EDIT's tabs */
static constexpr int kTabRows = 4;           /* 3 above the touch band, 1 below */
static constexpr int32_t kTabPagerY = 644;
static constexpr int32_t kTabClearY = 728;

static CodeList s_edit_list;

static const account_t *edit_source(void)
{
    return account_edit_draft();
}

static void remove_confirmed(void *)
{
    accounts_rcv_remove(account_edit_draft(), s_pending_index);
    account_edit_mark_dirty();
    list_show(&s_edit_list);
    lv_label_set_text(s_edit_list.status, "Removed (Save to keep)");
}

static void edit_row(int index)
{
    s_pending_index = index;
    static char message[96];
    snprintf(message, sizeof(message), "Code %d (%s) will be removed\nfrom this account.", index + 1,
             account_edit_draft()->rcv_codes[index]);
    const ui_confirm_t cfg = {"Remove code?", message, "Remove", remove_confirmed, nullptr, nullptr, true};
    ui_confirm_show(&cfg);
}

static void clear_all_confirmed(void *)
{
    account_t *a = account_edit_draft();
    memset(a->rcv_codes, 0, sizeof(a->rcv_codes));
    a->rcv_count = 0;
    a->rcv_used_mask = 0;
    account_edit_mark_dirty();
    list_show(&s_edit_list);
}

static void clear_all_deferred(void *)
{
    if (account_edit_draft()->rcv_count == 0) return;
    const ui_confirm_t cfg = {"Remove all codes?", "Every recovery code of this account\nwill be removed.",
                              "Remove all", clear_all_confirmed, nullptr, nullptr, true};
    ui_confirm_show(&cfg);
}

static void clear_all_cb(lv_event_t *)
{
    ui_defer(clear_all_deferred, nullptr);
}

static void add_deferred(void *)
{
    if (account_edit_draft()->rcv_count >= ACCOUNTS_RCV_MAX) {
        lv_label_set_text(s_edit_list.status, "No room for more codes");
        return;
    }
    account_edit_new_rcv_code();
}

static void add_cb(lv_event_t *)
{
    ui_defer(add_deferred, nullptr);
}

void recovery_edit_tab_show(void)
{
    list_show(&s_edit_list);
    lv_label_set_text(s_edit_list.status, "");
}

void recovery_edit_tab_build(lv_obj_t *parent, int32_t parent_y, lv_group_t *group)
{
    s_edit_list.source = edit_source;
    s_edit_list.on_row = edit_row;
    list_build(&s_edit_list, parent, parent_y, group, "No recovery codes", kTabRowTop, kTabRows, kTabPagerY);

    lv_obj_t *add = make_fixed_button(parent, group, LV_SYMBOL_PLUS " Add", 140);
    lv_obj_align(add, LV_ALIGN_TOP_RIGHT, -kSideMargin, kTabPagerY - parent_y);
    lv_obj_add_event_cb(add, add_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *clear = make_fixed_button(parent, group, MSF_SYMBOL_ERASER " Clear all", 180);
    lv_obj_align(clear, LV_ALIGN_TOP_RIGHT, -kSideMargin, kTabClearY - parent_y);
    lv_obj_add_event_cb(clear, clear_all_cb, LV_EVENT_CLICKED, nullptr);
}
