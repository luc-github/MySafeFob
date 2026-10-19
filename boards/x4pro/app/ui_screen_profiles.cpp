/*
 Project: MySafeFob  ui_screen_profiles.cpp
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
 * @file ui_screen_profiles.cpp
 * @brief MySafeFob App — Settings > PROFILES (2026-10-17 user request):
 *        managing the profiles accounts belong to (accounts.h).
 *
 *        PROFILES: one row per profile, "<name> (N)", the active one
 *        ticked; pages of kRowsPerPage rows (no scrolling on e-paper), as
 *        NAMES. A row opens PROFILE; "+ New" asks a name (the keyboard
 *        screen below) and opens the profile's first account, since a
 *        profile exists through its accounts.
 *        PROFILE: its account count, New, Rename, and Delete = the profile
 *        AND all its accounts, behind a critical confirmation.
 *        With a single profile (or no account at all) the list would hold
 *        one row: profiles_open() goes straight to PROFILE instead, whose
 *        Back then returns to Settings. Deleting that only profile empties
 *        the store; the device goes on with an empty Default profile.
 *        PROFILE_RENAME: full keyboard, prefilled; renaming to an existing
 *        profile merges the two (refused if a name would appear twice),
 *        an empty name makes it the default profile.
 *        A profile can also be created from an account's Profile field.
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "ui_keyboard.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

extern "C" {
#include "accounts.h"
#include "esp_log.h"
}

#include "app_log_workaround.h"

#include <cstdio>
#include <cstring>
#include <strings.h>

static const char *TAG = "profiles";

static char s_selected[ACCOUNTS_PROFILE_MAX + 1];   /* the profile PROFILE / PROFILE_RENAME work on */
static bool s_new_mode;                              /* PROFILE_RENAME names a new profile instead */

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static bool single_profile(void)
{
    return accounts_profile_count() <= 1;
}

void profiles_open(void)
{
    if (!single_profile()) {
        switch_screen(Screen::Profiles);
        return;
    }
    if (!accounts_profile_after(nullptr, s_selected)) s_selected[0] = '\0';   /* no account: Default */
    switch_screen(Screen::Profile);
}

/* ======================================================================== */
/* PROFILES                                                                 */
/* ======================================================================== */

static constexpr int kRowsPerPage = 6;
static constexpr int32_t kRowTop = 178;
static constexpr int32_t kRowH = kMinTouchTarget;
static constexpr int32_t kRowGap = 28;
static constexpr int32_t kPagerY = 720;
static constexpr int32_t kArrowW = 72;
static constexpr int32_t kSideMargin = 24;

static lv_group_t *s_list_group;
static int s_page;
static char s_shown[kRowsPerPage][ACCOUNTS_PROFILE_MAX + 1];
static lv_obj_t *s_rows[kRowsPerPage];
static lv_obj_t *s_row_labels[kRowsPerPage];
static lv_obj_t *s_prev, *s_next, *s_page_label, *s_list_status;

static void show_list(void)
{
    int count = accounts_profile_count();
    int pages = count == 0 ? 1 : (count + kRowsPerPage - 1) / kRowsPerPage;
    if (s_page >= pages) s_page = pages - 1;
    if (s_page < 0) s_page = 0;
    int n = accounts_profile_list(s_page * kRowsPerPage, kRowsPerPage, s_shown);
    for (int slot = 0; slot < kRowsPerPage; slot++) {
        if (slot < n) {
            char buf[ACCOUNTS_PROFILE_MAX + 24];
            bool active = strcasecmp(s_shown[slot], accounts_profile()) == 0;
            snprintf(buf, sizeof(buf), "%s%s (%d)", active ? LV_SYMBOL_OK " " : "", accounts_profile_label(s_shown[slot]),
                     accounts_profile_size(s_shown[slot]));
            lv_label_set_text(s_row_labels[slot], buf);
        }
        set_hidden(s_rows[slot], slot >= n);
    }
    set_hidden(s_prev, s_page == 0);
    set_hidden(s_next, s_page >= pages - 1);
    set_hidden(s_page_label, pages <= 1);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d / %d", s_page + 1, pages);
    lv_label_set_text(s_page_label, buf);
    lv_label_set_text(s_list_status, "");
    if (n > 0) lv_group_focus_obj(s_rows[0]);
}

static void list_row_deferred(void *user_data)
{
    int slot = static_cast<int>(reinterpret_cast<intptr_t>(user_data));
    if (slot >= kRowsPerPage || lv_obj_has_flag(s_rows[slot], LV_OBJ_FLAG_HIDDEN)) return;
    strlcpy(s_selected, s_shown[slot], sizeof(s_selected));
    switch_screen(Screen::Profile);
}

static void list_row_cb(lv_event_t *e)
{
    ui_defer(list_row_deferred, lv_event_get_user_data(e));
}

static void list_page_deferred(void *user_data)
{
    s_page += static_cast<int>(reinterpret_cast<intptr_t>(user_data));
    show_list();
    lv_port_disp_request_full_refresh();   /* every row changes: a clean page */
}

static void list_page_cb(lv_event_t *e)
{
    ui_defer(list_page_deferred, lv_event_get_user_data(e));
}

static void list_loaded_cb(lv_event_t *)
{
    show_list();
}

static void new_deferred(void *)
{
    s_new_mode = true;
    switch_screen(Screen::ProfileRename);
}

static void new_cb(lv_event_t *)
{
    ui_defer(new_deferred, nullptr);
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

lv_obj_t *build_profiles(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    s_list_group = lv_port_indev_new_group();
    *group_out = s_list_group;
    lv_obj_t *screen = make_screen();
    add_back_header(screen, LV_SYMBOL_DIRECTORY " Profiles", Screen::Settings, s_list_group, battery_label_out);

    int32_t y = kRowTop;
    for (int slot = 0; slot < kRowsPerPage; slot++) {
        y = touch_safe_y(y, kRowH);
        lv_obj_t *row = make_fixed_button(screen, s_list_group, "", LV_PCT(90));
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
        lv_obj_add_event_cb(row, list_row_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(slot)));
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
    s_list_status = lv_label_create(screen);
    lv_label_set_text(s_list_status, "");
    lv_obj_align(s_list_status, LV_ALIGN_TOP_MID, 0, kTouchBandTop + 10);

    s_prev = make_fixed_button(screen, s_list_group, LV_SYMBOL_LEFT, kArrowW);
    lv_obj_align(s_prev, LV_ALIGN_TOP_LEFT, kSideMargin, kPagerY);
    lv_obj_add_event_cb(s_prev, list_page_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(-1)));
    /* Pager on the left, "+ New" on the right: same row as NAMES. */
    s_page_label = lv_label_create(screen);
    lv_obj_align(s_page_label, LV_ALIGN_TOP_LEFT, kSideMargin + kArrowW + 16, kPagerY + 14);
    s_next = make_fixed_button(screen, s_list_group, LV_SYMBOL_RIGHT, kArrowW);
    lv_obj_align(s_next, LV_ALIGN_TOP_LEFT, kSideMargin + kArrowW + 96, kPagerY);
    lv_obj_add_event_cb(s_next, list_page_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(1)));
    lv_obj_t *add = make_fixed_button(screen, s_list_group, LV_SYMBOL_PLUS " New", 140);
    lv_obj_align(add, LV_ALIGN_TOP_RIGHT, -kSideMargin, kPagerY);
    lv_obj_add_event_cb(add, new_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_event_cb(screen, list_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    return screen;
}

/* ======================================================================== */
/* PROFILE                                                                  */
/* ======================================================================== */

static lv_obj_t *s_title, *s_info, *s_status, *s_rename_btn, *s_delete_btn;

/* Disabled = no frame, as HOME's empty letters (the mono theme shows no other difference). */
static void set_enabled(lv_obj_t *btn, bool on)
{
    if (on) lv_obj_remove_state(btn, LV_STATE_DISABLED);
    else lv_obj_add_state(btn, LV_STATE_DISABLED);
}

static void show_profile(void)
{
    int n = accounts_profile_size(s_selected);
    char buf[64];
    lv_label_set_text(s_title, accounts_profile_label(s_selected));
    if (n == 0) snprintf(buf, sizeof(buf), "No accounts yet");
    else snprintf(buf, sizeof(buf), "%d %s%s", n, n == 1 ? "account" : "accounts",
                  strcasecmp(s_selected, accounts_profile()) == 0 ? "\nShown on the home screen" : "");
    lv_label_set_text(s_info, buf);
    set_enabled(s_rename_btn, n > 0);   /* an empty profile has nothing to rename or delete */
    set_enabled(s_delete_btn, n > 0);
}

static void profile_loaded_deferred(void *)
{
    /* Gone meanwhile (last account removed), while other profiles remain. */
    if (accounts_profile_size(s_selected) == 0 && accounts_total() > 0) {
        profiles_open();
        return;
    }
    show_profile();
}

static void profile_back_deferred(void *)
{
    switch_screen(single_profile() ? Screen::Settings : Screen::Profiles);
}

static void profile_back_cb(lv_event_t *)
{
    ui_defer(profile_back_deferred, nullptr);
}

static void profile_loaded_cb(lv_event_t *)
{
    lv_label_set_text(s_status, "");
    ui_defer(profile_loaded_deferred, nullptr);
}

static void rename_deferred(void *)
{
    s_new_mode = false;
    switch_screen(Screen::ProfileRename);
}

static void rename_cb(lv_event_t *)
{
    ui_defer(rename_deferred, nullptr);
}

static void delete_confirmed(void *)
{
    int removed = accounts_profile_delete(s_selected);
    if (removed < 0) {
        lv_label_set_text(s_status, "Write failed, nothing was deleted");
        return;
    }
    ESP_LOGI(TAG, "profile deleted with %d accounts", removed);
    if (accounts_total() > 0) profiles_open();
    else switch_screen(Screen::Home);   /* store empty: Default, nothing to show */
}

static void delete_deferred(void *)
{
    int n = accounts_profile_size(s_selected);
    if (n == 0) return;
    char msg[ACCOUNTS_PROFILE_MAX + 128];
    if (n == accounts_total()) {   /* the only profile: everything goes */
        snprintf(msg, sizeof(msg), "All %d %s will be deleted:\nthe device starts again with an\n"
                 "empty Default profile.\nThis cannot be undone.", n, n == 1 ? "account" : "accounts");
    } else {
        snprintf(msg, sizeof(msg), "Profile \"%s\" and its %d %s\nwill be deleted.\nThis cannot be undone.",
                 accounts_profile_label(s_selected), n, n == 1 ? "account" : "accounts");
    }
    const ui_confirm_t cfg = {"Delete profile?", msg, "Delete", delete_confirmed, nullptr, nullptr, true};
    ui_confirm_show(&cfg);
}

static void delete_cb(lv_event_t *)
{
    ui_defer(delete_deferred, nullptr);
}

lv_obj_t *build_profile(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;
    lv_obj_t *screen = make_screen();
    lv_obj_t *bar = add_back_header_cb(screen, "", profile_back_cb, nullptr, group, battery_label_out);
    s_title = lv_obj_get_child(bar, 0);

    s_info = lv_label_create(screen);
    lv_obj_set_width(s_info, LV_PCT(90));
    lv_obj_set_style_text_align(s_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_info, LV_ALIGN_TOP_MID, 0, 190);

    /* New and Rename above the touch band, Delete alone below it. */
    lv_obj_t *add = make_button(screen, LV_SYMBOL_PLUS "  New profile");
    lv_obj_set_width(add, LV_PCT(90));
    lv_obj_align(add, LV_ALIGN_TOP_MID, 0, 290);
    lv_obj_add_event_cb(add, new_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(add, group);

    s_rename_btn = make_button(screen, LV_SYMBOL_EDIT "  Rename");
    lv_obj_set_width(s_rename_btn, LV_PCT(90));
    lv_obj_align(s_rename_btn, LV_ALIGN_TOP_MID, 0, 290 + kMinTouchTarget + 2 * kExtClickMargin);
    lv_obj_add_event_cb(s_rename_btn, rename_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(s_rename_btn, group);

    s_delete_btn = make_button(screen, LV_SYMBOL_TRASH "  Delete with its accounts");
    lv_obj_set_width(s_delete_btn, LV_PCT(90));
    lv_obj_align(s_delete_btn, LV_ALIGN_TOP_MID, 0, kTouchBandBottom + 12);
    lv_obj_add_event_cb(s_delete_btn, delete_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(s_delete_btn, group);
    lv_obj_set_style_border_width(s_rename_btn, 0, LV_STATE_DISABLED);
    lv_obj_set_style_border_width(s_delete_btn, 0, LV_STATE_DISABLED);

    s_status = lv_label_create(screen);
    lv_label_set_text(s_status, "");
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, kTouchBandTop + 10);

    lv_obj_add_event_cb(screen, profile_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    return screen;
}

/* ======================================================================== */
/* PROFILE_RENAME                                                           */
/* ======================================================================== */

static constexpr int32_t kFieldY = 176;
static constexpr int32_t kFieldW = 432;

static char s_text[ACCOUNTS_PROFILE_MAX + 1];
static int s_text_len;
static lv_obj_t *s_field_label, *s_field_status, *s_rename_title;

static void show_field(void)
{
    char buf[ACCOUNTS_PROFILE_MAX + 4];
    snprintf(buf, sizeof(buf), "%s_", s_text);
    lv_label_set_text(s_field_label, buf);
}

static void kb_char(char c, void *)
{
    if (s_text_len >= ACCOUNTS_PROFILE_MAX) return;
    s_text[s_text_len++] = c;
    s_text[s_text_len] = '\0';
    lv_label_set_text(s_field_status, "");
    show_field();
}

static void kb_backspace(void *)
{
    if (s_text_len > 0) s_text[--s_text_len] = '\0';
    show_field();
}

/* New: a name no profile has yet, then its first account. */
static void new_enter(void)
{
    if (s_text_len == 0 || strcasecmp(s_text, ACCOUNTS_PROFILE_DEFAULT_LABEL) == 0) {
        lv_label_set_text(s_field_status, "Type a name (Default already exists)");
        return;
    }
    if (accounts_profile_size(s_text) > 0) {
        lv_label_set_text(s_field_status, "This profile already exists");
        return;
    }
    if (accounts_total() >= MSF_STORE_MAX_ACCOUNTS) {
        lv_label_set_text(s_field_status, accounts_result_text(ACCOUNTS_ERR_FULL));
        return;
    }
    ESP_LOGI(TAG, "new profile: first account");
    account_edit_open_new_in_profile(s_text);
}

static void kb_enter(void *)
{
    if (s_new_mode) {
        new_enter();
        return;
    }
    if (strcasecmp(s_text, ACCOUNTS_PROFILE_DEFAULT_LABEL) == 0) s_text[0] = '\0';   /* the empty profile */
    if (strcmp(s_text, s_selected) == 0) {   /* unchanged */
        switch_screen(Screen::Profile);
        return;
    }
    char clash[ACCOUNTS_NAME_MAX + 1] = "";
    accounts_result_t r = accounts_profile_rename(s_selected, s_text, clash);
    if (r == ACCOUNTS_ERR_NAME_EXISTS) {
        char buf[ACCOUNTS_NAME_MAX + ACCOUNTS_PROFILE_MAX + 32];
        snprintf(buf, sizeof(buf), "\"%s\" is already in %s", clash, accounts_profile_label(s_text));
        lv_label_set_text(s_field_status, buf);
        return;
    }
    if (r != ACCOUNTS_OK) {
        lv_label_set_text(s_field_status, accounts_result_text(r));
        return;
    }
    strlcpy(s_selected, s_text, sizeof(s_selected));
    switch_screen(Screen::Profile);
}

static void rename_loaded_cb(lv_event_t *)
{
    strlcpy(s_text, s_new_mode ? "" : s_selected, sizeof(s_text));
    s_text_len = static_cast<int>(strlen(s_text));
    lv_label_set_text(s_rename_title, s_new_mode ? LV_SYMBOL_PLUS " New profile" : LV_SYMBOL_EDIT " Rename profile");
    lv_label_set_text(s_field_status, s_new_mode ? "Its first account opens next" : "Empty = Default profile");
    show_field();
}

/* Back: to the list for New, to the profile for Rename. */
static void rename_back_deferred(void *)
{
    if (s_new_mode) profiles_open();
    else switch_screen(Screen::Profile);
}

static void rename_back_cb(lv_event_t *)
{
    ui_defer(rename_back_deferred, nullptr);
}

lv_obj_t *build_profile_rename(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;
    lv_obj_t *screen = make_screen();
    lv_obj_t *bar = add_back_header_cb(screen, LV_SYMBOL_EDIT " Rename profile", rename_back_cb, nullptr, group,
                                       battery_label_out);
    s_rename_title = lv_obj_get_child(bar, 0);

    lv_obj_t *box = lv_obj_create(screen);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, kFieldW, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(box, kMinTouchTarget, 0);
    lv_obj_set_style_border_width(box, kButtonBorderWidth, 0);
    lv_obj_set_style_border_color(box, lv_color_black(), 0);
    lv_obj_set_style_radius(box, kButtonRadius, 0);
    lv_obj_set_style_pad_all(box, 10, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, kFieldY);
    s_field_label = lv_label_create(box);
    lv_obj_set_width(s_field_label, LV_PCT(100));
    lv_label_set_long_mode(s_field_label, LV_LABEL_LONG_WRAP);

    s_field_status = lv_label_create(screen);
    lv_label_set_text(s_field_status, "");
    lv_obj_align(s_field_status, LV_ALIGN_TOP_MID, 0, 412);

    static const ui_keyboard_cb_t kCb = {kb_char, kb_backspace, kb_enter, nullptr};
    lv_obj_t *kb = ui_keyboard_create(screen, group, &kCb, UI_KEYBOARD_FULL);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_obj_add_event_cb(screen, rename_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    return screen;
}
