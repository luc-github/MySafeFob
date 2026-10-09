/*
 Project: MySafeFob  ui_screen_account.cpp
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
 * @file ui_screen_account.cpp
 * @brief MySafeFob App — ACCOUNT (ADR-019, UI-SPECS.md §2.2 amendment): one
 *        account record. The TOTP code first (computed when the page opens
 *        and on Refresh, with the seconds left at that instant: no
 *        countdown redraw, ADR-009), then the login (password masked,
 *        Reveal), then the recovery codes count. Parts the record does not
 *        have are hidden. Remove goes through the P6 confirmation.
 *
 *        F-05 auto-clear: a secret is on screen here, so after
 *        SecretAutoClearS seconds without any input the page returns to
 *        HOME (e-paper keeps the image even while the device sleeps).
 *
 *        Targets sit at fixed y above the X4 Pro touch band, Edit/Remove
 *        below it (touch_safe_y(), ui_widgets.h). Edit and the recovery
 *        code list are not built yet (status line only).
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

extern "C" {
#include "accounts.h"
#include "settings_store.h"
#include "time_service.h"
#include "esp_log.h"
#include "esp_timer.h"
}

#include "app_log_workaround.h"

#include <cstdio>
#include <cstring>

static const char *TAG = "account";

static constexpr int32_t kLabelX = 24;
static constexpr int32_t kValueX = 150;
static constexpr int32_t kTotpY = 176;
static constexpr int32_t kLoginY = 256;
static constexpr int32_t kPasswordY = 300;
static constexpr int32_t kRecoveryY = 384;
static constexpr int32_t kNotesY = 448;
static constexpr int32_t kActionsY = 600;
static constexpr uint32_t kAutoClearCheckMs = 5000;

static uint16_t s_id;
static account_t s_account;
static bool s_revealed;
static lv_obj_t *s_title;
static lv_obj_t *s_totp_box, *s_code_label, *s_left_label;
static lv_obj_t *s_login_box, *s_user_label, *s_password_label, *s_reveal_label, *s_notes_label;
static lv_obj_t *s_recovery_box, *s_recovery_label;
static lv_obj_t *s_status;
static lv_obj_t *s_refresh_btn;
static lv_timer_t *s_clear_timer;

void account_open(uint16_t id)
{
    s_id = id;
    switch_screen(Screen::Account);
}

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void show_code(void)
{
    if (!time_service_is_valid()) {
        lv_label_set_text(s_code_label, "--- ---");
        lv_label_set_text(s_left_label, "clock not set");
        return;
    }
    char code[9];
    int left = 0;
    if (!accounts_totp(s_id, time_service_get_utc(), code, &left)) {
        lv_label_set_text(s_code_label, "error");
        lv_label_set_text(s_left_label, "");
        return;
    }
    /* "123 456" / "1234 5678": grouped for reading then typing. */
    char shown[12];
    int half = static_cast<int>(strlen(code)) / 2;
    snprintf(shown, sizeof(shown), "%.*s %s", half, code, code + half);
    lv_label_set_text(s_code_label, shown);
    char buf[16];
    snprintf(buf, sizeof(buf), "(%d s)", left);
    lv_label_set_text(s_left_label, buf);
}

static void show_password(void)
{
    if (s_revealed) {
        lv_label_set_text(s_password_label, s_account.password);
    } else {
        char masked[24];
        size_t n = strlen(s_account.password);
        if (n > sizeof(masked) - 1) n = sizeof(masked) - 1;
        memset(masked, '*', n);
        masked[n] = '\0';
        lv_label_set_text(s_password_label, masked);
    }
    lv_label_set_text(s_reveal_label, s_revealed ? "Hide" : "Reveal");
}

static void show_account(void)
{
    if (!accounts_get(s_id, &s_account)) {
        memset(&s_account, 0, sizeof(s_account));
        lv_label_set_text(s_title, "?");
    } else {
        lv_label_set_text(s_title, s_account.name);
    }
    s_revealed = false;
    set_hidden(s_totp_box, !s_account.has_totp);
    if (s_account.has_totp) show_code();
    set_hidden(s_login_box, !s_account.has_login);
    if (s_account.has_login) {
        lv_label_set_text(s_user_label, s_account.username);
        lv_label_set_text(s_notes_label, s_account.notes);
        show_password();
    }
    set_hidden(s_recovery_box, !s_account.has_recovery);
    if (s_account.has_recovery) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%u / %u unused", s_account.rcv_unused, s_account.rcv_total);
        lv_label_set_text(s_recovery_label, buf);
    }
    lv_label_set_text(s_status, "");
}

/* ---- Actions ----------------------------------------------------------- */

static void refresh_deferred(void *)
{
    show_code();
}

static void refresh_cb(lv_event_t *)
{
    ui_defer(refresh_deferred, nullptr);
}

static void reveal_deferred(void *)
{
    s_revealed = !s_revealed;
    show_password();
}

static void reveal_cb(lv_event_t *)
{
    ui_defer(reveal_deferred, nullptr);
}

static void status_deferred(void *user_data)
{
    lv_label_set_text(s_status, static_cast<const char *>(user_data));
}

static void recovery_cb(lv_event_t *)
{
    ui_defer(status_deferred, const_cast<char *>("Recovery codes: not built yet"));
}

static void edit_cb(lv_event_t *)
{
    ui_defer(status_deferred, const_cast<char *>("Edit: not built yet"));
}

static void remove_confirmed(void *)
{
    char letter = accounts_index_letter(s_account.name);
    ESP_LOGI(TAG, "remove account %u (%s)", s_id, s_account.name);
    accounts_remove(s_id);
    names_open(letter);   /* back to HOME by itself if the letter is now empty */
}

static void remove_deferred(void *)
{
    static char message[ACCOUNTS_NAME_MAX + 96];
    snprintf(message, sizeof(message), "\"%s\" and all its data\n(TOTP, login, recovery codes)\nwill be removed.\n"
             "This cannot be undone.", s_account.name);
    const ui_confirm_t cfg = {"Remove account?", message, "Remove", remove_confirmed, nullptr, nullptr};
    ui_confirm_show(&cfg);
}

static void remove_cb(lv_event_t *)
{
    ui_defer(remove_deferred, nullptr);
}

/* ---- F-05 auto-clear ----------------------------------------------------- */

static void clear_timer_cb(lv_timer_t *)
{
    uint32_t limit_s = settings_store_get_secret_auto_clear_s();
    if (limit_s == 0 || ui_confirm_is_open()) return;
    if (esp_timer_get_time() - ui_nav_last_activity_us() >= static_cast<int64_t>(limit_s) * 1000000) {
        ESP_LOGI(TAG, "auto-clear after %lu s without input", static_cast<unsigned long>(limit_s));
        switch_screen(Screen::Home);
    }
}

static void screen_loaded_cb(lv_event_t *)
{
    show_account();
    if (!s_clear_timer) s_clear_timer = lv_timer_create(clear_timer_cb, kAutoClearCheckMs, nullptr);
}

static void screen_unloaded_cb(lv_event_t *)
{
    if (s_clear_timer) {
        lv_timer_delete(s_clear_timer);
        s_clear_timer = nullptr;
    }
}

/* ---- Build --------------------------------------------------------------- */

static lv_obj_t *make_box(lv_obj_t *screen, int32_t y)
{
    lv_obj_t *box = lv_obj_create(screen);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(box, LV_ALIGN_TOP_LEFT, 0, y);
    lv_obj_add_flag(box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    /* The boxes overlap (full width, children at absolute y): a clickable
     * box on top would swallow the taps meant for another box's buttons. */
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

static lv_obj_t *make_text(lv_obj_t *parent, const char *text, int32_t x, int32_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, x, y);
    return label;
}

static lv_obj_t *make_action(lv_obj_t *parent, lv_group_t *group, const char *text, int32_t w, lv_align_t align,
                             int32_t x, int32_t y, lv_event_cb_t cb)
{
    lv_obj_t *btn = make_button(parent, text);
    lv_obj_set_style_min_width(btn, 0, 0);
    lv_obj_set_size(btn, w, kMinTouchTarget);
    lv_obj_set_ext_click_area(btn, 0);
    lv_obj_align(btn, align, x, touch_safe_y(y, kMinTouchTarget));
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(btn, group);
    return btn;
}

lv_obj_t *build_account(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;

    lv_obj_t *screen = make_screen();
    lv_obj_t *bar = add_back_header(screen, "", Screen::Names, group, battery_label_out);
    s_title = lv_obj_get_child(bar, 0);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_title, 300);

    /* TOTP: label, big code, seconds left, Refresh. */
    s_totp_box = make_box(screen, 0);
    make_text(s_totp_box, "TOTP", kLabelX, kTotpY + 14);
    s_code_label = make_text(s_totp_box, "", kValueX - 30, kTotpY + 8);
    lv_obj_set_style_text_font(s_code_label, &lv_font_montserrat_32, 0);
    s_left_label = make_text(s_totp_box, "", kValueX - 30, kTotpY + 50);
    s_refresh_btn = make_action(s_totp_box, group, LV_SYMBOL_REFRESH, 72, LV_ALIGN_TOP_RIGHT, -kLabelX, kTotpY,
                                refresh_cb);

    /* Login: username, password (masked) + Reveal, notes. */
    s_login_box = make_box(screen, 0);
    make_text(s_login_box, "Login", kLabelX, kLoginY);
    s_user_label = make_text(s_login_box, "", kValueX, kLoginY);
    make_text(s_login_box, "Password", kLabelX, kPasswordY + 14);
    s_password_label = make_text(s_login_box, "", kValueX, kPasswordY + 14);
    lv_obj_t *reveal = make_action(s_login_box, group, "Reveal", 120, LV_ALIGN_TOP_RIGHT, -kLabelX, kPasswordY,
                                   reveal_cb);
    s_reveal_label = lv_obj_get_child(reveal, 0);
    s_notes_label = make_text(s_login_box, "", kValueX, kNotesY);
    lv_label_set_long_mode(s_notes_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_notes_label, 300);

    /* Recovery codes: unused count + open. */
    s_recovery_box = make_box(screen, 0);
    make_text(s_recovery_box, "Recovery", kLabelX, kRecoveryY + 14);
    s_recovery_label = make_text(s_recovery_box, "", kValueX, kRecoveryY + 14);
    make_action(s_recovery_box, group, LV_SYMBOL_RIGHT, 72, LV_ALIGN_TOP_RIGHT, -kLabelX, kRecoveryY, recovery_cb);

    /* Status line in the touch band's gap (not a target). */
    s_status = lv_label_create(screen);
    lv_label_set_text(s_status, "");
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, kTouchBandTop + 10);

    make_action(screen, group, "Edit", 160, LV_ALIGN_TOP_LEFT, 48, kActionsY, edit_cb);
    make_action(screen, group, "Remove", 160, LV_ALIGN_TOP_RIGHT, -48, kActionsY, remove_cb);

    lv_obj_add_event_cb(screen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    lv_obj_add_event_cb(screen, screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, nullptr);
    return screen;
}
