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
 *        account record. The TOTP code first, with a countdown of its
 *        seconds left and the next code shown when the period ends
 *        (2026-10-10 user request, amends the "no countdown redraw" of
 *        ADR-009/ADR-019 for this page only). Optimised for e-paper: the
 *        countdown shows tens (30, 20, 10) then every second from 9, and a
 *        new code comes with a forced full refresh, which also resets the
 *        driver's ghost budget: one flash per period, when the code
 *        changes, about 11 fast refreshes in between. Measured offset with
 *        Authy: 1 s. Refresh recomputes at once (after a desync). Then the login (password and PIN
 *        masked, one eye button for both), the recovery codes count and the
 *        one-line note. Parts the record does not have are hidden. Remove
 *        goes through the P6 confirmation.
 *
 *        F-05 auto-clear: a secret is on screen here, so after
 *        SecretAutoClearS seconds without any input the page returns to
 *        HOME (e-paper keeps the image even while the device sleeps).
 *
 *        Targets sit at fixed y above the X4 Pro touch band, Edit/Remove
 *        below it (touch_safe_y(), ui_widgets.h). Edit opens EDIT
 *        (ui_screen_account_edit.cpp); the recovery code list is not built
 *        yet (status line only).
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
/* Layout 2026-10-10 (user review): login below the countdown (which ends
 * at y~253), eye icon instead of Reveal/Hide text, every target above the
 * X4 Pro touch band (ends before y=490). */
static constexpr int32_t kTotpY = 176;
static constexpr int32_t kLoginY = 270;
static constexpr int32_t kPasswordY = 300;   /* eye button row */
static constexpr int32_t kPinY = 366;
static constexpr int32_t kRecoveryY = 400;   /* open button row */
static constexpr int32_t kNoteY = 466;
static constexpr int32_t kEyeX = 480 - kLabelX - kMinTouchTarget;   /* eye button left edge */
static constexpr int32_t kActionsY = 600;

static uint16_t s_id;
static account_t s_account;
static bool s_revealed;
static lv_obj_t *s_title;
/* A record part = the objects shown or hidden together. Placed directly on
 * the screen, no container: a container sized to its content clipped the
 * focus ring and border of its buttons (2026-10-10 report). */
struct Part {
    lv_obj_t *objs[6];
    int count;
};
static Part s_totp_part, s_login_part, s_pin_part, s_recovery_part;
static lv_obj_t *s_code_label, *s_left_label;
static lv_obj_t *s_user_label, *s_password_label, *s_eye_label;
static lv_obj_t *s_pin_label, *s_note_label;
static lv_obj_t *s_recovery_label;
static lv_obj_t *s_status;
static lv_obj_t *s_refresh_btn;
static lv_timer_t *s_tick;
static constexpr uint32_t kTickMs = 1000;
static long long s_shown_counter = -1;   /* TOTP period index of the code on screen */

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

static void set_part_hidden(const Part &part, bool hidden)
{
    for (int i = 0; i < part.count; i++) set_hidden(part.objs[i], hidden);
}

/* Only touch a label when its text changes: an unchanged set still
 * invalidates, and every invalidation is an e-ink refresh. */
static void set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static void show_code(void)
{
    if (!time_service_is_valid()) {
        set_text(s_code_label, "--- ---");
        set_text(s_left_label, "clock not set");
        return;
    }
    char code[9];
    int left = 0;
    time_t now = time_service_get_utc();
    if (!accounts_totp(s_id, now, code, &left)) {
        set_text(s_code_label, "error");
        set_text(s_left_label, "");
        return;
    }
    /* "123 456" / "1234 5678": grouped for reading then typing. */
    char shown[12];
    int half = static_cast<int>(strlen(code)) / 2;
    snprintf(shown, sizeof(shown), "%.*s %s", half, code, code + half);
    /* New code (period changed since the last one shown): full refresh,
     * which also clears the ghosting of the fast refreshes before it. */
    long long counter = static_cast<long long>(now) / (s_account.period ? s_account.period : 30);
    if (s_shown_counter >= 0 && counter != s_shown_counter) lv_port_disp_request_full_refresh();
    s_shown_counter = counter;
    set_text(s_code_label, shown);
    /* Tens rounded up (30, 20, 10), then every second from 9. */
    int shown_left = left <= 9 ? left : ((left + 9) / 10) * 10;
    char buf[16];
    snprintf(buf, sizeof(buf), "(%d s)", shown_left);
    set_text(s_left_label, buf);
}

/* Countdown: recomputed every second, so the code changes by itself when
 * its period ends. */
static void tick_cb(lv_timer_t *)
{
    show_code();
}

static void show_secret(lv_obj_t *label, const char *secret)
{
    if (s_revealed) {
        lv_label_set_text(label, secret);
        return;
    }
    char masked[24];
    size_t n = strlen(secret);
    if (n > sizeof(masked) - 1) n = sizeof(masked) - 1;
    memset(masked, '*', n);
    masked[n] = '\0';
    lv_label_set_text(label, masked);
}

/* Password and PIN share one eye button: both are login secrets. */
static void show_password(void)
{
    show_secret(s_password_label, s_account.password);
    show_secret(s_pin_label, s_account.pin);
    lv_label_set_text(s_eye_label, s_revealed ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
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
    bool has_totp = s_account.totp_key[0] != '\0';
    bool has_login = s_account.username[0] || s_account.password[0];
    bool has_pin = s_account.pin[0] != '\0';
    set_part_hidden(s_totp_part, !has_totp);
    if (has_totp) show_code();
    /* The login part also holds the eye: shown for a PIN alone too. */
    set_part_hidden(s_login_part, !has_login && !has_pin);
    set_part_hidden(s_pin_part, !has_pin);
    lv_label_set_text(s_user_label, s_account.username);
    show_password();
    set_part_hidden(s_recovery_part, s_account.rcv_count == 0);
    if (s_account.rcv_count) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d / %u unused", accounts_rcv_unused(&s_account), s_account.rcv_count);
        lv_label_set_text(s_recovery_label, buf);
    }
    lv_label_set_text(s_note_label, s_account.note);
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

static void edit_deferred(void *)
{
    account_edit_open(s_id);
}

static void edit_cb(lv_event_t *)
{
    ui_defer(edit_deferred, nullptr);
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

static void screen_loaded_cb(lv_event_t *)
{
    s_shown_counter = -1;   /* the screen load is already a full refresh */
    show_account();
    if (s_account.totp_key[0] && !s_tick) s_tick = lv_timer_create(tick_cb, kTickMs, nullptr);
}

static void screen_unloaded_cb(lv_event_t *)
{
    if (s_tick) {
        lv_timer_delete(s_tick);
        s_tick = nullptr;
    }
}

/* ---- Build --------------------------------------------------------------- */

static lv_obj_t *add_to_part(Part *part, lv_obj_t *obj)
{
    if (part) part->objs[part->count++] = obj;
    return obj;
}

static lv_obj_t *make_text(lv_obj_t *screen, Part *part, const char *text, int32_t x, int32_t y)
{
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, x, y);
    return add_to_part(part, label);
}

static lv_obj_t *make_action(lv_obj_t *screen, Part *part, lv_group_t *group, const char *text, int32_t w,
                             lv_align_t align, int32_t x, int32_t y, lv_event_cb_t cb)
{
    lv_obj_t *btn = make_button(screen, text);
    lv_obj_set_style_min_width(btn, 0, 0);
    lv_obj_set_size(btn, w, kMinTouchTarget);
    lv_obj_set_ext_click_area(btn, 0);
    lv_obj_align(btn, align, x, touch_safe_y(y, kMinTouchTarget));
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(btn, group);
    return add_to_part(part, btn);
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
    make_text(screen, &s_totp_part, "TOTP", kLabelX, kTotpY + 14);
    s_code_label = make_text(screen, &s_totp_part, "", kValueX - 30, kTotpY + 8);
    lv_obj_set_style_text_font(s_code_label, &lv_font_montserrat_32, 0);
    s_left_label = make_text(screen, &s_totp_part, "", kValueX - 30, kTotpY + 50);
    s_refresh_btn = make_action(screen, &s_totp_part, group, LV_SYMBOL_REFRESH, kMinTouchTarget, LV_ALIGN_TOP_LEFT,
                                kEyeX, kTotpY, refresh_cb);

    /* Login: username, password (masked) + eye, PIN (masked). */
    make_text(screen, &s_login_part, "Login", kLabelX, kLoginY);
    s_user_label = make_text(screen, &s_login_part, "", kValueX, kLoginY);
    lv_label_set_long_mode(s_user_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_user_label, 480 - kValueX - kLabelX);
    make_text(screen, &s_login_part, "Password", kLabelX, kPasswordY + 14);
    s_password_label = make_text(screen, &s_login_part, "", kValueX, kPasswordY + 14);
    lv_label_set_long_mode(s_password_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_password_label, kEyeX - kValueX - 12);   /* never under the eye */
    lv_obj_t *eye = make_action(screen, &s_login_part, group, LV_SYMBOL_EYE_OPEN, kMinTouchTarget, LV_ALIGN_TOP_LEFT,
                                kEyeX, kPasswordY, reveal_cb);
    s_eye_label = lv_obj_get_child(eye, 0);
    lv_obj_set_style_text_font(s_eye_label, &lv_font_montserrat_32, 0);
    make_text(screen, &s_pin_part, "PIN", kLabelX, kPinY);
    s_pin_label = make_text(screen, &s_pin_part, "", kValueX, kPinY);

    /* One-line note (website, e-mail...): plain text, not a target. */
    s_note_label = make_text(screen, nullptr, "", kLabelX, kNoteY);
    lv_label_set_long_mode(s_note_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_note_label, 480 - 2 * kLabelX);

    /* Recovery codes: unused count + open. */
    make_text(screen, &s_recovery_part, "Recovery", kLabelX, kRecoveryY + 14);
    s_recovery_label = make_text(screen, &s_recovery_part, "", kValueX, kRecoveryY + 14);
    make_action(screen, &s_recovery_part, group, LV_SYMBOL_RIGHT, kMinTouchTarget, LV_ALIGN_TOP_LEFT, kEyeX,
                kRecoveryY, recovery_cb);

    /* Status line in the touch band's gap (not a target). */
    s_status = lv_label_create(screen);
    lv_label_set_text(s_status, "");
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, kTouchBandTop + 10);

    make_action(screen, nullptr, group, "Edit", 160, LV_ALIGN_TOP_LEFT, 48, kActionsY, edit_cb);
    make_action(screen, nullptr, group, "Remove", 160, LV_ALIGN_TOP_RIGHT, -48, kActionsY, remove_cb);

    lv_obj_add_event_cb(screen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    lv_obj_add_event_cb(screen, screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, nullptr);
    ui_auto_clear_attach(screen);   /* F-05 */
    return screen;
}
