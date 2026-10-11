/*
 Project: MySafeFob  ui_screen_pin.cpp
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
 * @file ui_screen_pin.cpp
 * @brief MySafeFob App — device PIN screen (F-03, UI-SPECS.md §2.1,
 *        ROADMAP 8.2 step B), three modes:
 *
 *        UNLOCK  6 digits, keypad reshuffled at every attempt (anti
 *                smudge), auto-submit on the 6th digit; wrong PIN and the
 *                back-off wait shown on the status line. Boot and wake land
 *                here when a store exists.
 *        CREATE  new PIN then confirmation, with the "forgotten PIN = data
 *                lost" warning (first boot, no store).
 *        CHANGE  current PIN, new PIN, confirmation (Settings > Security).
 *        CONFIRM current PIN once more before the Backup menu (ADR-005
 *                amendment 2026-10-11): an unlocked device alone must not
 *                be enough to copy every secret out or slip accounts in.
 *
 *        The key derivation (~1 s, Argon2id) runs in a worker task; a poll
 *        timer picks up its result, so the UI keeps drawing ("Unlocking...").
 *        Keys at fixed y, two rows above the X4 Pro touch band and two
 *        below (touch_safe_y()), the status line in the gap.
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "msf_symbols.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

extern "C" {
#include "accounts.h"
#include "settings_store.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

#include "app_log_workaround.h"

#include <atomic>
#include <cstdio>
#include <cstring>

static const char *TAG = "pin";

static constexpr int kPinLen = MSF_PIN_LEN;
static constexpr int32_t kKeyW = 120;
static constexpr int32_t kKeyH = 80;
static constexpr int32_t kKeyGapX = 16;
static constexpr int32_t kKeyGapY = 16;
/* 2026-10-11: everything moved up -- the two-line warning ended at ~298 and
 * the focus outline of the 1-2-3 row (300, outline 7 px above) overlapped it.
 * Top of the column: Back (88 + 56, outline to ~151). Rows 1-2 still end
 * above the touch band (kTouchBandTop 490). */
static constexpr int32_t kKeysTop = 294;
static constexpr int32_t kKeysLeft = (480 - (3 * kKeyW + 2 * kKeyGapX)) / 2;
static constexpr int kDel = -1;

enum Mode { kUnlock, kCreate, kChange, kConfirm };
enum Job { kJobNone, kJobUnlock, kJobCreate, kJobVerify, kJobChange, kJobConfirm };

static Mode s_mode = kUnlock;
static int s_step;                    /* CREATE: 0 new, 1 confirm; CHANGE: 0 current, 1 new, 2 confirm */
static char s_pin[kPinLen + 1];
static int s_len;
static char s_first[kPinLen + 1];     /* new PIN, kept until confirmed */
static int s_layout[10];              /* digit shown on key i (keys 0-8 the grid, 9 the bottom one) */
static lv_obj_t *s_title, *s_back, *s_prompt, *s_dots, *s_warning, *s_status;
static lv_obj_t *s_keys[10];
static lv_timer_t *s_poll;

/* Worker task: the PIN operation, its result picked up by the poll timer. */
static Job s_job;
static char s_job_pin[kPinLen + 1];
static std::atomic<bool> s_busy{false};
static std::atomic<bool> s_done{false};
static int s_job_result;

static void wipe(char *buf, size_t len)
{
    volatile char *p = buf;
    for (size_t i = 0; i < len; i++) p[i] = 0;
}

static void worker_task(void *)
{
    switch (s_job) {
    case kJobUnlock:
    case kJobVerify: s_job_result = accounts_unlock(s_job_pin); break;
    case kJobCreate: s_job_result = accounts_create(s_job_pin); break;
    case kJobChange: s_job_result = accounts_change_pin(s_job_pin); break;
    case kJobConfirm: s_job_result = accounts_verify_pin(s_job_pin); break;
    default: s_job_result = ESP_FAIL; break;
    }
    wipe(s_job_pin, sizeof(s_job_pin));
    s_done = true;
    vTaskDelete(nullptr);
}

static void start_job(Job job, const char *pin)
{
    s_job = job;
    strlcpy(s_job_pin, pin, sizeof(s_job_pin));
    s_done = false;
    s_busy = true;
    if (xTaskCreate(worker_task, "pin_job", 12288, nullptr, 3, nullptr) != pdPASS) {
        s_busy = false;
        s_job_result = ESP_ERR_NO_MEM;
        s_done = true;
    }
}

/* ---- Display ------------------------------------------------------------ */

static void set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static void show_dots(void)
{
    char buf[kPinLen * 2 + 1];
    int pos = 0;
    for (int i = 0; i < kPinLen; i++) {
        buf[pos++] = i < s_len ? '*' : '_';
        if (i + 1 < kPinLen) buf[pos++] = ' ';
    }
    buf[pos] = '\0';
    set_text(s_dots, buf);
}

/* Shuffled for the current PIN (unlock, change step 0): smudges on the
 * screen must not reveal it. In order when choosing a new PIN. */
static void layout_keys(bool shuffle)
{
    static const int kOrdered[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0};
    memcpy(s_layout, kOrdered, sizeof(s_layout));
    if (shuffle) {
        for (int i = 9; i > 0; i--) {
            int j = static_cast<int>(esp_random() % static_cast<uint32_t>(i + 1));
            int t = s_layout[i];
            s_layout[i] = s_layout[j];
            s_layout[j] = t;
        }
    }
    /* "Symbol keypad" setting: one fixed, clearly distinct picture per
     * digit (cosmetic, the PIN entered is still the digits). */
    static const char *const kSymbols[10] = {
        MSF_SYMBOL_STAR, LV_SYMBOL_EYE_OPEN, MSF_SYMBOL_SMILE, MSF_SYMBOL_POO, LV_SYMBOL_HOME,
        MSF_SYMBOL_CROWN, LV_SYMBOL_BELL, MSF_SYMBOL_SNOWFLAKE, MSF_SYMBOL_HEART, MSF_SYMBOL_CHESS_PAWN,
    };
    const bool symbols = settings_store_get_pin_symbols();
    for (int i = 0; i < 10; i++) {
        char text[2] = {static_cast<char>('0' + s_layout[i]), '\0'};
        lv_label_set_text(lv_obj_get_child(s_keys[i], 0), symbols ? kSymbols[s_layout[i]] : text);
    }
}

static const char *prompt_text(void)
{
    switch (s_mode) {
    case kUnlock: return MSF_SYMBOL_LOCK "  Enter PIN";
    case kCreate: return s_step == 0 ? "Choose a 6-digit PIN" : "Confirm the PIN";
    case kChange: return s_step == 0 ? "Current PIN" : s_step == 1 ? "New PIN" : "Confirm the new PIN";
    case kConfirm: return MSF_SYMBOL_KEY "  Confirm your PIN";
    }
    return "";
}

static void clear_entry(void)
{
    wipe(s_pin, sizeof(s_pin));
    s_len = 0;
    show_dots();
}

/* Shows the step: prompt, empty dots, keypad (shuffled when the current PIN
 * is asked). */
static void show_step(void)
{
    clear_entry();
    set_text(s_prompt, prompt_text());
    bool current = (s_mode == kUnlock) || (s_mode == kConfirm) || (s_mode == kChange && s_step == 0);
    layout_keys(current);
    bool warn = (s_mode == kCreate) || (s_mode == kChange && s_step > 0);
    set_text(s_warning, warn ? "If you forget it, the data is lost:\nkeep an SD backup." : "");
}

static void show_wait(void)
{
    uint32_t wait = msf_store_backoff_remaining_s();
    char buf[48];
    snprintf(buf, sizeof(buf), "Too many attempts: wait %lu s", static_cast<unsigned long>(wait));
    set_text(s_status, buf);
}

/* ---- Flow --------------------------------------------------------------- */

void pin_open_unlock(void)
{
    s_mode = kUnlock;
    s_step = 0;
    switch_screen(Screen::Pin);
}

void pin_open_create(void)
{
    s_mode = kCreate;
    s_step = 0;
    switch_screen(Screen::Pin);
}

void pin_open_change(void)
{
    s_mode = kChange;
    s_step = 0;
    switch_screen(Screen::Pin);
}

void pin_open_backup(void)
{
    s_mode = kConfirm;
    s_step = 0;
    switch_screen(Screen::Pin);
}

static void submit(void)
{
    switch (s_mode) {
    case kUnlock:
        set_text(s_status, "Unlocking...");
        start_job(kJobUnlock, s_pin);
        break;
    case kConfirm:
        set_text(s_status, "Checking...");
        start_job(kJobConfirm, s_pin);
        break;
    case kCreate:
    case kChange: {
        int first_new_step = (s_mode == kCreate) ? 0 : 1;
        if (s_mode == kChange && s_step == 0) {
            set_text(s_status, "Checking...");
            start_job(kJobVerify, s_pin);
        } else if (s_step == first_new_step) {
            strlcpy(s_first, s_pin, sizeof(s_first));
            s_step++;
            set_text(s_status, "");
            show_step();
        } else if (strcmp(s_first, s_pin) != 0) {
            wipe(s_first, sizeof(s_first));
            s_step = first_new_step;
            show_step();
            set_text(s_status, "The PINs differ, start again");
        } else {
            set_text(s_status, s_mode == kCreate ? "Creating the store..." : "Changing the PIN...");
            start_job(s_mode == kCreate ? kJobCreate : kJobChange, s_pin);
            wipe(s_first, sizeof(s_first));
        }
        break;
    }
    }
    clear_entry();
}

static void job_finished(void)
{
    Job job = s_job;
    int err = s_job_result;
    s_busy = false;
    ESP_LOGI(TAG, "job %d: %s", static_cast<int>(job), esp_err_to_name(err));
    if (err == ESP_OK) {
        switch (job) {
        case kJobUnlock:
        case kJobCreate: switch_screen(Screen::Home); return;
        case kJobVerify:
            s_step = 1;
            set_text(s_status, "");
            show_step();
            return;
        case kJobChange:
            switch_screen(Screen::SettingsSecurity);
            return;
        case kJobConfirm:
            backup_open_menu();
            return;
        default: return;
        }
    }
    if (err == ESP_ERR_INVALID_RESPONSE) {
        uint32_t fails = msf_store_fail_count();
        char buf[48];
        snprintf(buf, sizeof(buf), "Wrong PIN (%lu)", static_cast<unsigned long>(fails));
        set_text(s_status, buf);
    } else if (err == ESP_ERR_TIMEOUT) {
        show_wait();
    } else {
        set_text(s_status, esp_err_to_name(err));
    }
    if (s_mode == kChange) s_step = 0;
    show_step();   /* new shuffle after a failure */
}

static void poll_cb(lv_timer_t *)
{
    if (s_busy && s_done) {
        job_finished();
        return;
    }
    if (s_busy) return;
    /* Unlocked meanwhile (console `store unlock`): leave. */
    if (s_mode == kUnlock && accounts_is_unlocked()) {
        switch_screen(Screen::Home);
        return;
    }
    if (msf_store_backoff_remaining_s() > 0) show_wait();
    else if (strncmp(lv_label_get_text(s_status), "Too many", 8) == 0) set_text(s_status, "You can try again");
}

static void key_deferred(void *user_data)
{
    if (s_busy) return;
    bool asking_current = (s_mode == kUnlock) || (s_mode == kConfirm) || (s_mode == kChange && s_step == 0);
    if (asking_current && msf_store_backoff_remaining_s() > 0) {
        show_wait();
        return;
    }
    int key = static_cast<int>(reinterpret_cast<intptr_t>(user_data));
    if (key == kDel) {
        if (s_len > 0) s_pin[--s_len] = '\0';
        show_dots();
        return;
    }
    if (s_len >= kPinLen) return;
    s_pin[s_len++] = static_cast<char>('0' + s_layout[key]);
    s_pin[s_len] = '\0';
    show_dots();
    if (s_len == kPinLen) submit();
}

static void key_cb(lv_event_t *e)
{
    ui_defer(key_deferred, lv_event_get_user_data(e));
}

static void back_deferred(void *)
{
    if (s_busy) return;
    clear_entry();
    wipe(s_first, sizeof(s_first));
    switch_screen(Screen::SettingsSecurity);
}

static void back_cb(lv_event_t *)
{
    ui_defer(back_deferred, nullptr);
}

static void loaded_cb(lv_event_t *)
{
    static const char *const kTitles[] = {MSF_SYMBOL_LOCK " MySafeFob", MSF_SYMBOL_KEY " New PIN",
                                          MSF_SYMBOL_KEY " Change PIN", LV_SYMBOL_SD_CARD " Backup"};
    set_text(s_title, kTitles[s_mode]);
    if (s_mode == kChange || s_mode == kConfirm) lv_obj_remove_flag(s_back, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_back, LV_OBJ_FLAG_HIDDEN);   /* nothing to go back to */
    set_text(s_status, "");
    show_step();
    if (!s_poll) s_poll = lv_timer_create(poll_cb, 250, nullptr);
}

static void unloaded_cb(lv_event_t *)
{
    clear_entry();
    if (s_poll) {
        lv_timer_delete(s_poll);
        s_poll = nullptr;
    }
}

static lv_obj_t *make_key(lv_obj_t *screen, lv_group_t *group, int index, int32_t x, int32_t y)
{
    lv_obj_t *key = make_button(screen, "");
    lv_obj_set_style_min_width(key, 0, 0);
    lv_obj_set_size(key, kKeyW, kKeyH);
    lv_obj_set_ext_click_area(key, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(key, 0), &msf_font_32, 0);
    lv_obj_align(key, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_add_event_cb(key, key_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(index)));
    add_to_group(key, group);
    return key;
}

lv_obj_t *build_pin(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;
    lv_obj_t *screen = make_screen();
    lv_obj_t *bar = add_back_header_cb(screen, "", back_cb, nullptr, group, battery_label_out);
    s_title = lv_obj_get_child(bar, 0);
    s_back = lv_obj_get_child(screen, -1);   /* the "< Back" add_back_header_cb() just created */

    s_prompt = lv_label_create(screen);
    lv_obj_align(s_prompt, LV_ALIGN_TOP_MID, 0, 154);
    s_dots = lv_label_create(screen);
    lv_obj_set_style_text_font(s_dots, &msf_font_32, 0);
    lv_obj_align(s_dots, LV_ALIGN_TOP_MID, 0, 186);
    s_warning = lv_label_create(screen);
    lv_obj_set_style_text_align(s_warning, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_warning, LV_ALIGN_TOP_MID, 0, 224);
    lv_label_set_text(s_warning, "");

    /* Rows: 3 + 3 above the band, 3 + (bottom digit, DEL) below it. */
    int32_t y = kKeysTop;
    for (int row = 0; row < 4; row++) {
        /* Rows 3-4 always below the band, not just "no edge inside it"
         * (touch_safe_y): the gap between rows 2 and 3 holds s_status. */
        if (row == 2 && y < kTouchBandBottom + 6) y = kTouchBandBottom + 6;
        y = touch_safe_y(y, kKeyH);
        for (int col = 0; col < 3; col++) {
            int32_t x = kKeysLeft + col * (kKeyW + kKeyGapX);
            if (row < 3) {
                s_keys[row * 3 + col] = make_key(screen, group, row * 3 + col, x, y);
            } else if (col == 1) {
                s_keys[9] = make_key(screen, group, 9, x, y);
            } else if (col == 2) {
                lv_obj_t *del = make_key(screen, group, kDel, x, y);
                lv_label_set_text(lv_obj_get_child(del, 0), LV_SYMBOL_BACKSPACE);
            }
        }
        y += kKeyH + kKeyGapY;
    }

    s_status = lv_label_create(screen);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, kTouchBandTop + 10);
    lv_label_set_text(s_status, "");

    lv_obj_add_event_cb(screen, loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    lv_obj_add_event_cb(screen, unloaded_cb, LV_EVENT_SCREEN_UNLOADED, nullptr);
    return screen;
}
