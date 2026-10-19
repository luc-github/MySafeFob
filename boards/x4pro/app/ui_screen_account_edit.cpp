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
 *        "Discard changes?" (P6 confirmation). Three tabs (2026-10-13 user
 *        request): Info (Name, Profile, ID, Note, Extra), Security (Login,
 *        Password, PIN, TOTP) and Recovery (the code list,
 *        ui_screen_recovery.cpp, was a separate screen). Field rows at
 *        fixed y placed with touch_safe_y(): 3 above the X4 Pro touch band,
 *        the rest below, status line in the gap. Each tab's content sits in its own full-width
 *        container, shown / hidden as a whole.
 *
 *        FIELD edits one field of the draft with the right keyboard (P7):
 *        full for Name/Login/Password/Note, numeric for the PIN, Base32 for
 *        the TOTP key (plus 6/8 digits and 30/60 s, default 6/30). Profile
 *        (2026-10-17): full keyboard plus a button that steps through the
 *        existing profiles; a new account starts in the active one. The
 *        current value is prefilled in clear (to fix a typo without
 *        retyping); Enter validates back into the draft, Clear empties the
 *        field, Back leaves the draft unchanged. Both screens show secrets:
 *        F-05 auto-clear (ui_auto_clear_attach()).
 *
 *        The Recovery tab works on the same draft through
 *        account_edit_draft(); FIELD also enters a new recovery code
 *        (account_edit_new_rcv_code()) and comes back to that tab.
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "msf_symbols.h"
#include "ui_keyboard.h"
#include "hw_config.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

extern "C" {
#include "accounts.h"
#include "esp_log.h"
#include "esp_random.h"
}

#include "app_log_workaround.h"

#include <cstdio>
#include <cstring>

static const char *TAG = "account_edit";

/* ---- Shared state ------------------------------------------------------- */

/* Tab order: Info (kName..kExtra), then Security (kLogin..kTotp). */
enum Field {
    kName, kProfile, kIdent, kNote, kExtra, kLogin, kPassword, kPin, kTotp, kFieldCount,
    kRcvCode = kFieldCount
};
static constexpr int kTabInfo = 0, kTabSecurity = 1, kTabRecovery = 2, kTabCount = 3;

static const char *const kFieldText[kFieldCount] = {
    MSF_SYMBOL_ID_CARD " Name",     LV_SYMBOL_DIRECTORY " Profile", MSF_SYMBOL_FINGERPRINT " ID",
    MSF_SYMBOL_NOTE " Note",        MSF_SYMBOL_PAPERCLIP " Extra",  MSF_SYMBOL_USER " Login",
    MSF_SYMBOL_KEY " Password",     MSF_SYMBOL_HASHTAG " PIN",      MSF_SYMBOL_CLOCK " TOTP"};

static int field_tab(int f)
{
    return f < kLogin ? kTabInfo : kTabSecurity;
}

/* Row of `f` within its tab. */
static int field_row(int f)
{
    return f - (f < kLogin ? kName : kLogin);
}

static account_t s_draft;
static uint16_t s_edit_id;        /* 0 = adding a new account */
static bool s_dirty;
static int s_tab = kTabInfo;      /* kept while FIELD is open, so it comes back to it */
static Screen s_return_screen;    /* where Back / Discard goes */
static char s_return_letter;      /* NAMES letter to return to */

/* Field entry state */
static Field s_field;
static char s_new_code[ACCOUNTS_RCV_CODE_MAX + 1];   /* kRcvCode target */
static char s_text[ACCOUNTS_TOTP_KEY_MAX + 1];   /* largest field */
static int s_text_len;
static uint8_t s_digits;
static uint8_t s_period;

static void go_back(void)
{
    if (s_edit_id) account_open(s_edit_id);
    else if (s_return_screen == Screen::Names) names_open(s_return_letter);
    else if (s_return_screen == Screen::Profiles) profiles_open();   /* a new profile left unsaved */
    else switch_screen(Screen::Home);
}

void account_edit_open(uint16_t id)
{
    if (!accounts_get(id, &s_draft)) return;
    s_edit_id = id;
    s_dirty = false;
    s_tab = kTabInfo;
    switch_screen(Screen::AccountEdit);
}

/* A blank draft in `profile`; EDIT is shown by the caller. */
static void prepare_new(char letter, const char *profile)
{
    accounts_init_record(&s_draft);
    if (letter >= 'A' && letter <= 'Z') s_draft.name[0] = letter;   /* prefill, '#' left empty */
    strlcpy(s_draft.profile, profile, sizeof(s_draft.profile));
    s_edit_id = 0;
    s_dirty = false;
    s_tab = kTabInfo;
    s_return_letter = letter;
}

void account_edit_open_new(char letter)
{
    prepare_new(letter, accounts_profile());
    s_return_screen = letter ? Screen::Names : Screen::Home;
    switch_screen(Screen::AccountEdit);
}

void account_edit_open_new_in_profile(const char *profile)
{
    prepare_new(0, profile);
    s_dirty = true;   /* Back asks first: the new profile exists only once this account is saved */
    s_return_screen = Screen::Profiles;
    switch_screen(Screen::AccountEdit);
}

/* Field storage in the draft, and its maximum length. */
static char *field_buffer(Field f, size_t *max_len)
{
    switch (f) {
    case kName: *max_len = ACCOUNTS_NAME_MAX; return s_draft.name;
    case kProfile: *max_len = ACCOUNTS_PROFILE_MAX; return s_draft.profile;
    case kIdent: *max_len = ACCOUNTS_IDENT_MAX; return s_draft.ident;
    case kNote: *max_len = ACCOUNTS_NOTE_MAX; return s_draft.note;
    case kExtra: *max_len = ACCOUNTS_EXTRA_MAX; return s_draft.extra;
    case kLogin: *max_len = ACCOUNTS_USER_MAX; return s_draft.username;
    case kPassword: *max_len = ACCOUNTS_PASSWORD_MAX; return s_draft.password;
    case kPin: *max_len = ACCOUNTS_PIN_MAX; return s_draft.pin;
    case kTotp: *max_len = ACCOUNTS_TOTP_KEY_MAX; return s_draft.totp_key;
    case kRcvCode: *max_len = ACCOUNTS_RCV_CODE_MAX; return s_new_code;
    default: *max_len = 0; return nullptr;
    }
}

/* ---- EDIT screen -------------------------------------------------------- */

static constexpr int32_t kTabsY = 160;        /* below Back / Save */
static constexpr int32_t kContentY = 226;     /* tab containers start here */
static constexpr int32_t kRowTop = 240;
static constexpr int32_t kRowH = kMinTouchTarget;
static constexpr int32_t kRowGap = 28;

static lv_obj_t *s_title;
static lv_obj_t *s_values[kFieldCount];
static lv_obj_t *s_status;
static lv_obj_t *s_tab_cont[kTabCount];
static ui_tabs_t s_tabs;

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

static void show_text(Field f, const char *value)
{
    lv_label_set_text(s_values[f], value[0] ? value : "(none)");
}

static void show_rows(void)
{
    char buf[64];
    show_text(kName, s_draft.name);
    lv_label_set_text(s_values[kProfile], accounts_profile_label(s_draft.profile));
    show_text(kIdent, s_draft.ident);
    show_text(kNote, s_draft.note);
    show_text(kExtra, s_draft.extra);
    show_text(kLogin, s_draft.username);
    masked(buf, sizeof(buf), s_draft.password);
    lv_label_set_text(s_values[kPassword], buf);
    masked(buf, sizeof(buf), s_draft.pin);
    lv_label_set_text(s_values[kPin], buf);
    if (s_draft.totp_key[0]) snprintf(buf, sizeof(buf), "%u digits / %u s", s_draft.digits, s_draft.period);
    else snprintf(buf, sizeof(buf), "(none)");
    lv_label_set_text(s_values[kTotp], buf);
    lv_label_set_text(s_title, s_edit_id ? LV_SYMBOL_EDIT " Edit" : LV_SYMBOL_PLUS " Add");
}

static void show_tab(void)
{
    for (int t = 0; t < kTabCount; t++) {
        if (t == s_tab) lv_obj_remove_flag(s_tab_cont[t], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_tab_cont[t], LV_OBJ_FLAG_HIDDEN);
    }
    ui_tabs_set_active(&s_tabs, s_tab);
    if (s_tab == kTabRecovery) recovery_edit_tab_show();
}

static void tab_selected(int index)
{
    s_tab = index;
    lv_label_set_text(s_status, "");
    show_tab();
}

static void field_open(Field f);

account_t *account_edit_draft(void)
{
    return &s_draft;
}

void account_edit_mark_dirty(void)
{
    s_dirty = true;
}

void account_edit_new_rcv_code(void)
{
    s_new_code[0] = '\0';
    field_open(kRcvCode);
}

static void row_deferred(void *user_data)
{
    field_open(static_cast<Field>(reinterpret_cast<intptr_t>(user_data)));
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
                              discard_confirmed, nullptr, nullptr, true};
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
    show_tab();
}

/* A tab's content: the full width below the tabs, not clickable itself,
 * children placed in screen coordinates minus kContentY. */
static lv_obj_t *make_tab_container(lv_obj_t *screen)
{
    lv_obj_t *cont = lv_obj_create(screen);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, SCREEN_WIDTH, SCREEN_HEIGHT - kContentY);
    lv_obj_set_pos(cont, 0, kContentY);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    return cont;
}

lv_obj_t *build_account_edit(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;

    lv_obj_t *screen = make_screen();
    lv_obj_t *bar = add_back_header_cb(screen, LV_SYMBOL_EDIT " Edit", back_cb, nullptr, group, battery_label_out);
    s_title = lv_obj_get_child(bar, 0);

    lv_obj_t *save = make_button(screen, LV_SYMBOL_SAVE " Save");
    lv_obj_set_style_min_width(save, 120, 0);
    lv_obj_align(save, LV_ALIGN_TOP_RIGHT, -16, kBackTopMargin);
    lv_obj_add_event_cb(save, save_cb, LV_EVENT_CLICKED, nullptr);

    static const char *const kTabText[kTabCount] = {MSF_SYMBOL_INFO " Info", MSF_SYMBOL_SHIELD " Security",
                                                    MSF_SYMBOL_LIFE_RING " Recovery"};
    ui_tabs_build(&s_tabs, screen, group, kTabText, kTabCount, kTabsY, tab_selected);

    for (int t = 0; t < kTabCount; t++) s_tab_cont[t] = make_tab_container(screen);

    for (int f = 0; f < kFieldCount; f++) {
        lv_obj_t *cont = s_tab_cont[field_tab(f)];
        int32_t y = kRowTop;
        for (int k = 0; k <= field_row(f); k++) {   /* same y sequence in each tab */
            y = touch_safe_y(y, kRowH);
            if (k < field_row(f)) y += kRowH + kRowGap;
        }
        lv_obj_t *row = make_button(cont, "");
        lv_obj_set_style_min_width(row, 0, 0);
        lv_obj_set_size(row, 432, kRowH);   /* 90 % of the screen */
        lv_obj_set_ext_click_area(row, 0);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y - kContentY);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(f)));
        add_to_group(row, group);
        lv_obj_t *name = lv_obj_get_child(row, 0);
        lv_label_set_text(name, kFieldText[f]);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 12, 0);
        s_values[f] = lv_label_create(row);
        lv_label_set_long_mode(s_values[f], LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_values[f], 170);   /* after the icon + field name */
        lv_obj_align(s_values[f], LV_ALIGN_LEFT_MID, 190, 0);
        lv_obj_t *arrow = lv_label_create(row);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -12, 0);
    }
    recovery_edit_tab_build(s_tab_cont[kTabRecovery], kContentY, group);
    add_to_group(save, group);   /* last: Back, tabs, the active tab's rows, Save */

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
static constexpr int32_t kFieldW = 432;   /* 90% of the 480 px screen */

static lv_obj_t *s_field_title;
static lv_obj_t *s_field_box;
static lv_obj_t *s_field_label;
static lv_obj_t *s_field_status;
static lv_obj_t *s_options;
static lv_obj_t *s_digits_label;
static lv_obj_t *s_period_label;
static lv_obj_t *s_keyboards[kModeCount];
static lv_obj_t *s_dice;   /* password field only */
static lv_obj_t *s_pick;   /* profile field only: next existing profile */

/* Password generator (ROADMAP 8.3, 2026-10-11): kGenLen characters from the
 * hardware RNG, letters + digits + a few symbols that the on-device keyboard
 * also has, at least one of each class (many sites demand it). Rejection
 * sampling: no modulo bias. */
static constexpr int kGenLen = 20;
static const char kGenUpper[] = "ABCDEFGHJKLMNPQRSTUVWXYZ";    /* no I, O: easier to read back */
static const char kGenLower[] = "abcdefghijkmnopqrstuvwxyz";   /* no l */
static const char kGenDigit[] = "23456789";                    /* no 0, 1 */
static const char kGenSymbol[] = "-_.!@#$+=";    /* all on the keyboard's symbol layer */

static uint32_t gen_uniform(uint32_t n)
{
    uint32_t limit = UINT32_MAX - (UINT32_MAX % n);
    uint32_t r;
    do {
        r = esp_random();
    } while (r >= limit);
    return r % n;
}

static void generate_password(char *out)
{
    static const char *const kClasses[] = {kGenUpper, kGenLower, kGenDigit, kGenSymbol};
    char all[sizeof(kGenUpper) + sizeof(kGenLower) + sizeof(kGenDigit) + sizeof(kGenSymbol)];
    snprintf(all, sizeof(all), "%s%s%s%s", kGenUpper, kGenLower, kGenDigit, kGenSymbol);
    size_t all_len = strlen(all);
    bool ok;
    do {
        for (int i = 0; i < kGenLen; i++) out[i] = all[gen_uniform(all_len)];
        out[kGenLen] = '\0';
        ok = true;
        for (const char *cls : kClasses) ok = ok && strpbrk(out, cls) != nullptr;
    } while (!ok);
}

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

/* Enter: validate into the draft and go back to EDIT (or to the recovery
 * code list for a new code). */
static void kb_enter(void *)
{
    if (s_field == kRcvCode) {
        if (s_text_len == 0) {
            lv_label_set_text(s_field_status, "Empty code");
            return;
        }
        if (!accounts_rcv_append(&s_draft, s_text)) {
            lv_label_set_text(s_field_status, "No room for more codes");
            return;
        }
        s_dirty = true;
        switch_screen(Screen::AccountEdit);   /* back on the Recovery tab (s_tab) */
        return;
    }
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

static void dice_deferred(void *)
{
    if (s_field != kPassword) return;
    generate_password(s_text);
    s_text_len = kGenLen;
    show_field();
    lv_label_set_text(s_field_status, "New password: Enter to keep it");
}

static void dice_cb(lv_event_t *)
{
    ui_defer(dice_deferred, nullptr);
}

/* Steps through the existing profiles, from the one typed so far. */
static void pick_deferred(void *)
{
    if (s_field != kProfile) return;
    char next[ACCOUNTS_PROFILE_MAX + 1];
    if (!accounts_profile_after(s_text, next)) return;
    strlcpy(s_text, next, sizeof(s_text));
    s_text_len = static_cast<int>(strlen(s_text));
    show_field();
    lv_label_set_text(s_field_status, s_text[0] ? "Enter to keep it" : "Default profile: Enter to keep it");
}

static void pick_cb(lv_event_t *)
{
    ui_defer(pick_deferred, nullptr);
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
    /* Draft unchanged. */
    switch_screen(Screen::AccountEdit);   /* the tab it came from is kept (s_tab) */
}

static void field_back_cb(lv_event_t *)
{
    ui_defer(field_back_deferred, nullptr);
}

static void field_loaded_cb(lv_event_t *)
{
    lv_label_set_text(s_field_title, s_field == kRcvCode ? MSF_SYMBOL_LIFE_RING " New recovery code" : kFieldText[s_field]);
    int mode = (s_field == kTotp) ? 1 : (s_field == kPin) ? 2 : 0;
    for (int i = 0; i < kModeCount; i++) {
        if (i == mode) lv_obj_remove_flag(s_keyboards[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_keyboards[i], LV_OBJ_FLAG_HIDDEN);
    }
    /* TOTP: options row first, the (long) key below it. */
    bool totp = (s_field == kTotp);
    if (totp) lv_obj_remove_flag(s_options, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_options, LV_OBJ_FLAG_HIDDEN);
    /* Password / Profile: the field leaves room for the dice / the
     * profile picker at its end. */
    bool password = (s_field == kPassword);
    bool profile = (s_field == kProfile);
    if (password) lv_obj_remove_flag(s_dice, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_dice, LV_OBJ_FLAG_HIDDEN);
    if (profile) lv_obj_remove_flag(s_pick, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_pick, LV_OBJ_FLAG_HIDDEN);
    if (password || profile) {
        lv_obj_set_width(s_field_box, kFieldW - kMinTouchTarget - 12);
        lv_obj_align(s_field_box, LV_ALIGN_TOP_LEFT, (480 - kFieldW) / 2, kOptionsY);
    } else {
        lv_obj_set_width(s_field_box, kFieldW);
        lv_obj_align(s_field_box, LV_ALIGN_TOP_MID, 0, totp ? kOptionsY + kMinTouchTarget + 16 : kOptionsY);
    }
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

    lv_obj_t *clear = make_button(screen, MSF_SYMBOL_ERASER " Clear");
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
    lv_obj_set_size(s_field_box, kFieldW, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(s_field_box, kMinTouchTarget, 0);
    lv_obj_set_style_border_width(s_field_box, kButtonBorderWidth, 0);
    lv_obj_set_style_border_color(s_field_box, lv_color_black(), 0);
    lv_obj_set_style_radius(s_field_box, kButtonRadius, 0);
    lv_obj_set_style_pad_all(s_field_box, 10, 0);
    lv_obj_remove_flag(s_field_box, LV_OBJ_FLAG_CLICKABLE);
    s_field_label = lv_label_create(s_field_box);
    lv_obj_set_width(s_field_label, LV_PCT(100));
    lv_label_set_long_mode(s_field_label, LV_LABEL_LONG_WRAP);

    /* Dice drawn as an outline (2026-10-11): Font Awesome's dice glyphs are
     * solid only in the free set (black blobs at this size), none in
     * Regular. A 30 px rounded frame with five pips, Regular-style. */
    s_dice = make_button(screen, "");
    lv_obj_set_style_min_width(s_dice, 0, 0);
    lv_obj_set_size(s_dice, kMinTouchTarget, kMinTouchTarget);
    lv_obj_t *face = lv_obj_create(s_dice);
    lv_obj_remove_style_all(face);
    lv_obj_set_size(face, 30, 30);
    lv_obj_center(face);
    lv_obj_remove_flag(face, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(face, 5, 0);
    lv_obj_set_style_border_width(face, 3, 0);
    lv_obj_set_style_border_color(face, lv_color_black(), 0);
    /* Inside the 3 px border (children are placed in the content area, 24 px
     * here): 2 px margins, center pip at 9-15 -- exactly centered. */
    static const int8_t kPips[5][2] = {{2, 2}, {16, 2}, {9, 9}, {2, 16}, {16, 16}};
    for (const auto &pip : kPips) {
        lv_obj_t *dot = lv_obj_create(face);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_pos(dot, pip[0], pip[1]);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    }
    lv_obj_align(s_dice, LV_ALIGN_TOP_RIGHT, -(480 - kFieldW) / 2, kOptionsY);
    lv_obj_add_event_cb(s_dice, dice_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(s_dice, group);
    lv_obj_add_flag(s_dice, LV_OBJ_FLAG_HIDDEN);

    s_pick = make_button(screen, LV_SYMBOL_LOOP);
    lv_obj_set_style_min_width(s_pick, 0, 0);
    lv_obj_set_size(s_pick, kMinTouchTarget, kMinTouchTarget);
    lv_obj_align(s_pick, LV_ALIGN_TOP_RIGHT, -(480 - kFieldW) / 2, kOptionsY);
    lv_obj_add_event_cb(s_pick, pick_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(s_pick, group);
    lv_obj_add_flag(s_pick, LV_OBJ_FLAG_HIDDEN);

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
