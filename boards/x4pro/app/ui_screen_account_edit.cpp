/*
 Project: MySafeFob  ui_screen_account_edit.cpp
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
 * @file ui_screen_account_edit.cpp
 * @brief MySafeFob App — account EDIT and FIELD entry (ADR-019 amendment
 *        2026-10-09), also used by "+ Add".
 *
 *        EDIT works on a copy (s_draft) of the record: nothing is written
 *        until Save (header, top right); Back with unsaved changes asks
 *        "Discard changes?" (P6 confirmation). Seven field rows (Name,
 *        Login, Password, PIN, Note, TOTP, Recovery) at fixed y placed with
 *        touch_safe_y(): 4 above the X4 Pro touch band, 3 below, status
 *        line in the gap.
 *
 *        FIELD edits one field of the draft with the right keyboard (P7):
 *        full for Name/Login/Password/Note, numeric for the PIN, Base32 for
 *        the TOTP key (plus 6/8 digits and 30/60 s, default 6/30). The
 *        current value is prefilled in clear (to fix a typo without
 *        retyping); Enter validates back into the draft, Clear empties the
 *        field, Back leaves the draft unchanged. Both screens show secrets:
 *        F-05 auto-clear (ui_auto_clear_attach()).
 *
 *        The recovery code list editor is not built yet (status line).
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

static const char *TAG = "account_edit";

/* ---- Shared state ------------------------------------------------------- */

enum Field { kName, kLogin, kPassword, kPin, kNote, kTotp, kRecovery, kFieldCount };

static const char *const kFieldText[kFieldCount] = {"Name", "Login", "Password", "PIN", "Note", "TOTP", "Recovery"};

static account_t s_draft;
static uint16_t s_edit_id;        /* 0 = adding a new account */
static bool s_dirty;
static Screen s_return_screen;    /* where Back / Discard goes */
static char s_return_letter;      /* NAMES letter to return to */

/* Field entry state */
static Field s_field;
static char s_text[ACCOUNTS_TOTP_KEY_MAX + 1];   /* largest field */
static int s_text_len;
static uint8_t s_digits;
static uint8_t s_period;

static void go_back(void)
{
    if (s_edit_id) account_open(s_edit_id);
    else if (s_return_screen == Screen::Names) names_open(s_return_letter);
    else switch_screen(Screen::Home);
}

void account_edit_open(uint16_t id)
{
    if (!accounts_get(id, &s_draft)) return;
    s_edit_id = id;
    s_dirty = false;
    switch_screen(Screen::AccountEdit);
}

void account_edit_open_new(char letter)
{
    accounts_init_record(&s_draft);
    if (letter >= 'A' && letter <= 'Z') s_draft.name[0] = letter;   /* prefill, '#' left empty */
    s_edit_id = 0;
    s_dirty = false;
    s_return_screen = letter ? Screen::Names : Screen::Home;
    s_return_letter = letter;
    switch_screen(Screen::AccountEdit);
}

/* Field storage in the draft, and its maximum length. */
static char *field_buffer(Field f, size_t *max_len)
{
    switch (f) {
    case kName: *max_len = ACCOUNTS_NAME_MAX; return s_draft.name;
    case kLogin: *max_len = ACCOUNTS_USER_MAX; return s_draft.username;
    case kPassword: *max_len = ACCOUNTS_PASSWORD_MAX; return s_draft.password;
    case kPin: *max_len = ACCOUNTS_PIN_MAX; return s_draft.pin;
    case kNote: *max_len = ACCOUNTS_NOTE_MAX; return s_draft.note;
    case kTotp: *max_len = ACCOUNTS_TOTP_KEY_MAX; return s_draft.totp_key;
    default: *max_len = 0; return nullptr;
    }
}

/* ---- EDIT screen -------------------------------------------------------- */

static constexpr int32_t kRowTop = 178;
static constexpr int32_t kRowH = kMinTouchTarget;
static constexpr int32_t kRowGap = 28;

static lv_obj_t *s_title;
static lv_obj_t *s_values[kFieldCount];
static lv_obj_t *s_status;

static void masked(char *buf, size_t size, const char *secret)
{
    size_t n = strlen(secret);
    if (n == 0) {
        snprintf(buf, size, "(none)");
        return;
    }
    if (n > 12) n = 12;
    memset(buf, '*', n);
    buf[n] = '\0';
}

static void show_rows(void)
{
    char buf[64];
    lv_label_set_text(s_values[kName], s_draft.name[0] ? s_draft.name : "(none)");
    lv_label_set_text(s_values[kLogin], s_draft.username[0] ? s_draft.username : "(none)");
    masked(buf, sizeof(buf), s_draft.password);
    lv_label_set_text(s_values[kPassword], buf);
    masked(buf, sizeof(buf), s_draft.pin);
    lv_label_set_text(s_values[kPin], buf);
    lv_label_set_text(s_values[kNote], s_draft.note[0] ? s_draft.note : "(none)");
    if (s_draft.totp_key[0]) snprintf(buf, sizeof(buf), "%u digits / %u s", s_draft.digits, s_draft.period);
    else snprintf(buf, sizeof(buf), "(none)");
    lv_label_set_text(s_values[kTotp], buf);
    if (s_draft.rcv_count) snprintf(buf, sizeof(buf), "%u codes", s_draft.rcv_count);
    else snprintf(buf, sizeof(buf), "(none)");
    lv_label_set_text(s_values[kRecovery], buf);
    lv_label_set_text(s_title, s_edit_id ? "Edit" : "Add");
}

static void field_open(Field f);

static void row_deferred(void *user_data)
{
    Field f = static_cast<Field>(reinterpret_cast<intptr_t>(user_data));
    if (f == kRecovery) {
        lv_label_set_text(s_status, "Recovery codes: next step");
        return;
    }
    field_open(f);
}

static void row_cb(lv_event_t *e)
{
    ui_defer(row_deferred, lv_event_get_user_data(e));
}

static void save_deferred(void *)
{
    uint16_t id = s_edit_id;
    accounts_result_t r = s_edit_id ? accounts_update(s_edit_id, &s_draft) : accounts_add(&s_draft, &id);
    ESP_LOGI(TAG, "save %s account: %s", s_edit_id ? "edited" : "new", accounts_result_text(r));
    if (r != ACCOUNTS_OK) {
        lv_label_set_text(s_status, accounts_result_text(r));
        return;
    }
    s_dirty = false;
    account_open(id);
}

static void save_cb(lv_event_t *)
{
    ui_defer(save_deferred, nullptr);
}

static void discard_confirmed(void *)
{
    s_dirty = false;
    go_back();
}

static void back_deferred(void *)
{
    if (!s_dirty) {
        go_back();
        return;
    }
    const ui_confirm_t cfg = {"Discard changes?", "The changes made to this account\nwill be lost.", "Discard",
                              discard_confirmed, nullptr, nullptr};
    ui_confirm_show(&cfg);
}

static void back_cb(lv_event_t *)
{
    ui_defer(back_deferred, nullptr);
}

static void edit_loaded_cb(lv_event_t *)
{
    lv_label_set_text(s_status, "");
    show_rows();
}

lv_obj_t *build_account_edit(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;

    lv_obj_t *screen = make_screen();
    lv_obj_t *bar = add_back_header_cb(screen, "Edit", back_cb, nullptr, group, battery_label_out);
    s_title = lv_obj_get_child(bar, 0);

    lv_obj_t *save = make_button(screen, "Save");
    lv_obj_set_style_min_width(save, 120, 0);
    lv_obj_align(save, LV_ALIGN_TOP_RIGHT, -16, kBackTopMargin);
    lv_obj_add_event_cb(save, save_cb, LV_EVENT_CLICKED, nullptr);

    int32_t y = kRowTop;
    for (int f = 0; f < kFieldCount; f++) {
        y = touch_safe_y(y, kRowH);
        lv_obj_t *row = make_button(screen, "");
        lv_obj_set_style_min_width(row, 0, 0);
        lv_obj_set_size(row, LV_PCT(90), kRowH);
        lv_obj_set_ext_click_area(row, 0);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(f)));
        add_to_group(row, group);
        lv_obj_t *name = lv_obj_get_child(row, 0);
        lv_label_set_text(name, kFieldText[f]);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 12, 0);
        s_values[f] = lv_label_create(row);
        lv_label_set_long_mode(s_values[f], LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_values[f], 220);
        lv_obj_align(s_values[f], LV_ALIGN_LEFT_MID, 140, 0);
        lv_obj_t *arrow = lv_label_create(row);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -12, 0);
        y += kRowH + kRowGap;
    }
    add_to_group(save, group);   /* after the rows: Back, fields, Save */

    s_status = lv_label_create(screen);
    lv_label_set_text(s_status, "");
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, kTouchBandTop + 10);

    lv_obj_add_event_cb(screen, edit_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    ui_auto_clear_attach(screen);   /* F-05: the draft holds secrets */
    return screen;
}

/* ---- FIELD screen ------------------------------------------------------- */

static constexpr int kModeCount = 3;   /* full, Base32, numeric keyboards */
static constexpr int32_t kOptionsY = 176;

static lv_obj_t *s_field_title;
static lv_obj_t *s_field_box;
static lv_obj_t *s_field_label;
static lv_obj_t *s_field_status;
static lv_obj_t *s_options;
static lv_obj_t *s_digits_label;
static lv_obj_t *s_period_label;
static lv_obj_t *s_keyboards[kModeCount];

static void show_field(void)
{
    char buf[ACCOUNTS_TOTP_KEY_MAX + 4];
    snprintf(buf, sizeof(buf), "%s_", s_text);
    lv_label_set_text(s_field_label, buf);
}

static void show_options(void)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%u digits", s_digits);
    lv_label_set_text(s_digits_label, buf);
    snprintf(buf, sizeof(buf), "%u s", s_period);
    lv_label_set_text(s_period_label, buf);
}

static void field_open(Field f)
{
    s_field = f;
    size_t max_len = 0;
    const char *value = field_buffer(f, &max_len);
    strlcpy(s_text, value ? value : "", sizeof(s_text));
    s_text_len = static_cast<int>(strlen(s_text));
    s_digits = s_draft.digits ? s_draft.digits : 6;
    s_period = s_draft.period ? s_draft.period : 30;
    switch_screen(Screen::FieldEdit);
}

static size_t field_max(void)
{
    size_t max_len = 0;
    field_buffer(s_field, &max_len);
    return max_len;
}

static void kb_char(char c, void *)
{
    if (static_cast<size_t>(s_text_len) < field_max()) {
        s_text[s_text_len++] = c;
        s_text[s_text_len] = '\0';
        show_field();
        lv_label_set_text(s_field_status, "");
    }
}

static void kb_backspace(void *)
{
    if (s_text_len > 0) {
        s_text[--s_text_len] = '\0';
        show_field();
    }
}

/* Enter: validate into the draft and go back to EDIT. */
static void kb_enter(void *)
{
    if (s_field == kTotp && s_text_len > 0 && !accounts_totp_key_valid(s_text)) {
        lv_label_set_text(s_field_status, "Invalid TOTP key (Base32, 1-64 bytes)");
        return;
    }
    size_t max_len = 0;
    char *dest = field_buffer(s_field, &max_len);
    if (strcmp(dest, s_text) != 0) s_dirty = true;
    strlcpy(dest, s_text, max_len + 1);
    if (s_field == kTotp) {
        if (s_draft.digits != s_digits || s_draft.period != s_period) s_dirty = true;
        s_draft.digits = s_digits;
        s_draft.period = s_period;
    }
    switch_screen(Screen::AccountEdit);
}

static void clear_deferred(void *)
{
    s_text[0] = '\0';
    s_text_len = 0;
    show_field();
    lv_label_set_text(s_field_status, "Cleared: press Enter to apply");
}

static void clear_cb(lv_event_t *)
{
    ui_defer(clear_deferred, nullptr);
}

static void option_deferred(void *user_data)
{
    if (reinterpret_cast<intptr_t>(user_data) == 0) s_digits = (s_digits == 6) ? 8 : 6;
    else s_period = (s_period == 30) ? 60 : 30;
    show_options();
}

static void option_cb(lv_event_t *e)
{
    ui_defer(option_deferred, lv_event_get_user_data(e));
}

static void field_back_deferred(void *)
{
    switch_screen(Screen::AccountEdit);   /* draft unchanged */
}

static void field_back_cb(lv_event_t *)
{
    ui_defer(field_back_deferred, nullptr);
}

static void field_loaded_cb(lv_event_t *)
{
    lv_label_set_text(s_field_title, kFieldText[s_field]);
    int mode = (s_field == kTotp) ? 1 : (s_field == kPin) ? 2 : 0;
    for (int i = 0; i < kModeCount; i++) {
        if (i == mode) lv_obj_remove_flag(s_keyboards[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_keyboards[i], LV_OBJ_FLAG_HIDDEN);
    }
    /* TOTP: options row first, the (long) key below it. */
    bool totp = (s_field == kTotp);
    if (totp) lv_obj_remove_flag(s_options, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_options, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_field_box, LV_ALIGN_TOP_MID, 0, totp ? kOptionsY + kMinTouchTarget + 16 : kOptionsY);
    show_field();
    show_options();
    lv_label_set_text(s_field_status, "");
}

static lv_obj_t *make_option(lv_obj_t *parent, lv_group_t *group, intptr_t which, lv_obj_t **label_out)
{
    lv_obj_t *btn = make_button(parent, "");
    lv_obj_set_ext_click_area(btn, 0);
    lv_obj_add_event_cb(btn, option_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(which));
    add_to_group(btn, group);
    *label_out = lv_obj_get_child(btn, 0);
    return btn;
}

lv_obj_t *build_field_edit(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;

    lv_obj_t *screen = make_screen();
    lv_obj_t *bar = add_back_header_cb(screen, "", field_back_cb, nullptr, group, battery_label_out);
    s_field_title = lv_obj_get_child(bar, 0);

    lv_obj_t *clear = make_button(screen, "Clear");
    lv_obj_set_style_min_width(clear, 120, 0);
    lv_obj_align(clear, LV_ALIGN_TOP_RIGHT, -16, kBackTopMargin);
    lv_obj_add_event_cb(clear, clear_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(clear, group);

    /* TOTP options: "6 digits" / "30 s", each toggles. */
    s_options = lv_obj_create(screen);
    lv_obj_remove_style_all(s_options);
    lv_obj_set_size(s_options, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_options, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(s_options, kFocusOutlineSlack, 0);
    lv_obj_set_style_pad_column(s_options, 2 * kExtClickMargin, 0);
    lv_obj_align(s_options, LV_ALIGN_TOP_MID, 0, kOptionsY - kFocusOutlineSlack);
    make_option(s_options, group, 0, &s_digits_label);
    make_option(s_options, group, 1, &s_period_label);

    s_field_box = lv_obj_create(screen);
    lv_obj_remove_style_all(s_field_box);
    lv_obj_set_size(s_field_box, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(s_field_box, kMinTouchTarget, 0);
    lv_obj_set_style_border_width(s_field_box, kButtonBorderWidth, 0);
    lv_obj_set_style_border_color(s_field_box, lv_color_black(), 0);
    lv_obj_set_style_radius(s_field_box, kButtonRadius, 0);
    lv_obj_set_style_pad_all(s_field_box, 10, 0);
    lv_obj_remove_flag(s_field_box, LV_OBJ_FLAG_CLICKABLE);
    s_field_label = lv_label_create(s_field_box);
    lv_obj_set_width(s_field_label, LV_PCT(100));
    lv_label_set_long_mode(s_field_label, LV_LABEL_LONG_WRAP);

    /* Validation message, just above the keyboard (not a target). */
    s_field_status = lv_label_create(screen);
    lv_label_set_text(s_field_status, "");
    lv_obj_align(s_field_status, LV_ALIGN_TOP_MID, 0, 412);

    static const ui_keyboard_cb_t kCb = {kb_char, kb_backspace, kb_enter, nullptr};
    static const ui_keyboard_mode_t kModes[kModeCount] = {UI_KEYBOARD_FULL, UI_KEYBOARD_BASE32, UI_KEYBOARD_NUMERIC};
    for (int i = 0; i < kModeCount; i++) {
        s_keyboards[i] = ui_keyboard_create(screen, group, &kCb, kModes[i]);
        lv_obj_align(s_keyboards[i], LV_ALIGN_BOTTOM_MID, 0, -10);
    }

    lv_obj_add_event_cb(screen, field_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    ui_auto_clear_attach(screen);   /* F-05: the field may hold a secret */
    return screen;
}
