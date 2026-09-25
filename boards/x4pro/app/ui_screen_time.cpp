/*
 Project: MySafeFob  ui_screen_time.cpp
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
 * @file ui_screen_time.cpp
 * @brief MySafeFob App — Settings > Time: three ways to set the clock.
 *        Wi-Fi (SNTP; this step: password entry UI only), BLE (placeholder)
 *        and Manual (functional). The clock and RTC hold UTC; the manual
 *        entry is LOCAL time converted with the stored fixed UTC offset.
 *        Nothing on this screen refreshes by itself: the "current time" line
 *        is redrawn only when the screen is shown or the clock is set.
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "ui_keyboard.h"
#include "lv_port_indev.h"
#include "settings_store.h"
#include "time_service.h"
#include "wifi_time.h"
#include "ble_time.h"
#include "ui_nav.h"

#include <cstdio>
#include <cstring>

enum Tab { kTabWifi, kTabBle, kTabManual, kTabCount };

static constexpr int kDateTimeDigits = 12; /* YYYYMMDDHHMM */
static constexpr int kMaxPassword = 63;
static constexpr int kPasswordShown = 18;
static constexpr int32_t kTabWidth = 130;
static constexpr int32_t kTabHeight = 64;
static constexpr int32_t kKeyW = 120;
static constexpr int32_t kKeyH = 56;
static constexpr int32_t kKeyGap = 12;
static constexpr int32_t kSmallBtn = 56;
static constexpr int32_t kApRowGap = 16;
static constexpr int kTzStepMin = 30;
static constexpr int kTzMinMin = -12 * 60;
static constexpr int kTzMaxMin = 14 * 60;

static constexpr int kKeyBackspace = -1;
static constexpr int kKeyOk = -2;

static lv_obj_t *s_tab_btn[kTabCount];
static lv_obj_t *s_panel[kTabCount];
static lv_obj_t *s_keyboard;
static int s_tab;

/* Manual tab state */
static char s_digits[kDateTimeDigits + 1];
static int s_digits_len;
static lv_obj_t *s_now_label;
static lv_obj_t *s_entry_label;
static lv_obj_t *s_manual_status;
static lv_obj_t *s_tz_label;

/* Wi-Fi tab state (password field only for now) */
static char s_password[kMaxPassword + 1];
static int s_password_len;
static bool s_password_visible;
static lv_obj_t *s_password_label;
static lv_obj_t *s_wifi_status;
static lv_obj_t *s_wifi_eye_label;
enum WifiPhase { kPhaseList, kPhasePassword };
static int s_wifi_phase;
static char s_sel_ssid[33];
static lv_obj_t *s_list_box;
static lv_obj_t *s_pw_box;
static lv_obj_t *s_ap_btn[WIFI_TIME_MAX_APS];
static lv_timer_t *s_poll_timer;
static wifi_time_state_t s_wifi_shown_state;

static void set_label(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static void format_tz(char *buf, size_t size, int minutes)
{
    int a = minutes < 0 ? -minutes : minutes;
    snprintf(buf, size, "UTC%c%02d:%02d", minutes < 0 ? '-' : '+', a / 60, a % 60);
}

/* ---- Manual tab -------------------------------------------------------- */

static void show_now(void)
{
    char tz[16];
    char buf[64];
    int tz_min = settings_store_get_time_tz_offset_min();
    format_tz(tz, sizeof(tz), tz_min);
    if (!time_service_is_valid()) {
        snprintf(buf, sizeof(buf), "Now: not set (%s)", tz);
    } else {
        time_service_dt_t dt;
        time_service_epoch_to_dt(time_service_get_utc() + tz_min * 60, &dt);
        snprintf(buf, sizeof(buf), "Now: %04d-%02d-%02d %02d:%02d %s", dt.year, dt.month, dt.day, dt.hour,
                 dt.minute, tz);
    }
    set_label(s_now_label, buf);
}

static void show_entry(void)
{
    /* Template YYYY-MM-DD HH:MM, typed digits fill the '_' slots in order. */
    static const char kTemplate[] = "____-__-__ __:__";
    char buf[sizeof(kTemplate)];
    memcpy(buf, kTemplate, sizeof(kTemplate));
    int d = 0;
    for (size_t i = 0; buf[i] && d < s_digits_len; i++) {
        if (buf[i] == '_') buf[i] = s_digits[d++];
    }
    set_label(s_entry_label, buf);
}

static void show_tz(void)
{
    char tz[16];
    format_tz(tz, sizeof(tz), settings_store_get_time_tz_offset_min());
    set_label(s_tz_label, tz);
}

static void apply_manual_time(void)
{
    time_service_dt_t dt = {};
    int v[kDateTimeDigits];
    for (int i = 0; i < kDateTimeDigits; i++) v[i] = s_digits[i] - '0';
    dt.year = v[0] * 1000 + v[1] * 100 + v[2] * 10 + v[3];
    dt.month = v[4] * 10 + v[5];
    dt.day = v[6] * 10 + v[7];
    dt.hour = v[8] * 10 + v[9];
    dt.minute = v[10] * 10 + v[11];
    dt.second = 0; /* press OK when the reference clock reaches :00 */
    if (!time_service_dt_is_valid(&dt)) {
        set_label(s_manual_status, "Invalid date or time");
        return;
    }
    time_t utc = time_service_dt_to_epoch(&dt) - settings_store_get_time_tz_offset_min() * 60;
    bool rtc_ok = time_service_set_utc(utc, TIME_SOURCE_MANUAL);
    s_digits_len = 0;
    memset(s_digits, 0, sizeof(s_digits));
    show_entry();
    show_now();
    set_label(s_manual_status, rtc_ok ? "Time set" : "Time set (RTC write failed)");
}

static void manual_key_cb(lv_event_t *e)
{
    int key = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (key >= 0 && key <= 9) {
        if (s_digits_len < kDateTimeDigits) {
            s_digits[s_digits_len++] = static_cast<char>('0' + key);
            show_entry();
            set_label(s_manual_status, s_digits_len == kDateTimeDigits ? "Press OK at :00" : "Local date and time");
        }
    } else if (key == kKeyBackspace) {
        if (s_digits_len > 0) s_digits[--s_digits_len] = '\0';
        show_entry();
        set_label(s_manual_status, "Local date and time");
    } else if (key == kKeyOk) {
        if (s_digits_len == kDateTimeDigits) {
            apply_manual_time();
        } else {
            set_label(s_manual_status, "Need 12 digits");
        }
    }
}

static void tz_step_cb(lv_event_t *e)
{
    int step = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))) * kTzStepMin;
    int tz = settings_store_get_time_tz_offset_min() + step;
    if (tz < kTzMinMin) tz = kTzMinMin;
    if (tz > kTzMaxMin) tz = kTzMaxMin;
    settings_store_set_time_tz_offset_min(tz);
    show_tz();
    show_now();
}

static lv_obj_t *add_manual_key(lv_obj_t *parent, lv_group_t *group, const char *text, int key)
{
    lv_obj_t *btn = make_button(parent, text);
    lv_obj_set_style_min_width(btn, 0, 0);
    lv_obj_set_size(btn, kKeyW, kKeyH);
    lv_obj_set_ext_click_area(btn, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(btn, 0), &lv_font_montserrat_32, 0);
    lv_obj_add_event_cb(btn, manual_key_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(key)));
    add_to_group(btn, group);
    return btn;
}

static lv_obj_t *make_panel(lv_obj_t *content)
{
    lv_obj_t *p = lv_obj_create(content);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(p, 8, 0);
    return p;
}

static lv_obj_t *make_flex_row(lv_obj_t *parent, int32_t gap)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, kFocusOutlineSlack, 0);
    lv_obj_set_style_pad_column(row, gap, 0);
    return row;
}

static lv_obj_t *make_square_button(lv_obj_t *parent, lv_group_t *group, const char *text, lv_event_cb_t cb,
                                    intptr_t user_data)
{
    lv_obj_t *btn = make_button(parent, text);
    lv_obj_set_style_min_width(btn, 0, 0);
    lv_obj_set_size(btn, kSmallBtn, kSmallBtn);
    lv_obj_set_ext_click_area(btn, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(user_data));
    add_to_group(btn, group);
    return btn;
}

static void build_manual_tab(lv_obj_t *panel, lv_group_t *group)
{
    s_now_label = lv_label_create(panel);
    s_entry_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_entry_label, &lv_font_montserrat_32, 0);
    s_manual_status = lv_label_create(panel);
    lv_label_set_text(s_manual_status, "Local date and time");

    lv_obj_t *tz_row = make_flex_row(panel, kKeyGap);
    make_square_button(tz_row, group, "-", tz_step_cb, -1);
    s_tz_label = lv_label_create(tz_row);
    make_square_button(tz_row, group, "+", tz_step_cb, 1);

    lv_obj_t *pad = make_flex_row(panel, kKeyGap);
    lv_obj_set_flex_flow(pad, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(pad, kKeyGap, 0);
    lv_obj_set_width(pad, 3 * kKeyW + 2 * kKeyGap + 2 * kFocusOutlineSlack);
    static const char *const kDigits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
    for (int i = 0; i < 9; i++) add_manual_key(pad, group, kDigits[i], i + 1);
    add_manual_key(pad, group, LV_SYMBOL_BACKSPACE, kKeyBackspace);
    add_manual_key(pad, group, "0", 0);
    add_manual_key(pad, group, LV_SYMBOL_NEW_LINE, kKeyOk);
}

/* ---- Wi-Fi tab (password entry UI; scan/connect/SNTP come next) --------- */

static void show_password(void)
{
    char buf[kPasswordShown + 8];
    int shown = s_password_len < kPasswordShown ? s_password_len : kPasswordShown;
    int pos = 0;
    if (s_password_len > kPasswordShown) buf[pos++] = '<';
    for (int i = s_password_len - shown; i < s_password_len; i++) {
        buf[pos++] = s_password_visible ? s_password[i] : '*';
    }
    buf[pos] = '\0';
    set_label(s_password_label, pos ? buf : "(password)");
}

static void kb_char(char c, void *)
{
    if (s_password_len < kMaxPassword) {
        s_password[s_password_len++] = c;
        s_password[s_password_len] = '\0';
        show_password();
    }
}

static void kb_backspace(void *)
{
    if (s_password_len > 0) {
        s_password[--s_password_len] = '\0';
        show_password();
    }
}

static void update_wifi_visibility(void)
{
    bool pw = s_wifi_phase == kPhasePassword;
    if (pw) lv_obj_add_flag(s_list_box, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(s_list_box, LV_OBJ_FLAG_HIDDEN);
    if (pw) lv_obj_remove_flag(s_pw_box, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_pw_box, LV_OBJ_FLAG_HIDDEN);
    if (s_tab == kTabWifi && pw) lv_obj_remove_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void fill_ap_list(void)
{
    for (int i = 0; i < WIFI_TIME_MAX_APS; i++) {
        wifi_time_ap_t ap;
        if (wifi_time_get_ap(i, &ap)) {
            char buf[64];
            /* Rough RSSI -> quality: -50 dBm or better = 100%, -100 dBm or worse = 0%. */
            int quality = 2 * (ap.rssi + 100);
            if (quality < 0) quality = 0;
            if (quality > 100) quality = 100;
            snprintf(buf, sizeof(buf), "%s%s  %d%%", ap.secure ? "* " : "", ap.ssid, quality);
            lv_label_set_text(lv_obj_get_child(s_ap_btn[i], 0), buf);
            lv_obj_remove_flag(s_ap_btn[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ap_btn[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* Runs only while a scan/sync is in flight (created on the user's tap,
 * deleted once the worker reaches a final state), so the screen never
 * refreshes on its own outside an operation the user started. */
static void wifi_poll_cb(lv_timer_t *timer)
{
    wifi_time_state_t st = wifi_time_get_state();
    if (st == s_wifi_shown_state) return;
    s_wifi_shown_state = st;
    set_label(s_wifi_status, wifi_time_get_message());
    bool final_state = (st == WIFI_TIME_SCAN_DONE || st == WIFI_TIME_SYNC_OK || st == WIFI_TIME_FAILED);
    if (st == WIFI_TIME_SCAN_DONE) {
        fill_ap_list();
    } else if (st == WIFI_TIME_SYNC_OK) {
        s_password_len = 0;
        s_password[0] = '\0';
        show_password();
        s_wifi_phase = kPhaseList;
        update_wifi_visibility();
    }
    if (final_state) {
        lv_timer_delete(timer);
        s_poll_timer = nullptr;
    } else {
        board_activity_notify();
    }
}

static void start_poll(void)
{
    s_wifi_shown_state = WIFI_TIME_IDLE;
    if (!s_poll_timer) s_poll_timer = lv_timer_create(wifi_poll_cb, 300, nullptr);
}

static void kb_enter(void *)
{
    if (s_wifi_phase != kPhasePassword) return;
    if (s_password_len < 8) {
        set_label(s_wifi_status, "Password too short (8+)");
        return;
    }
    if (wifi_time_sync_start(s_sel_ssid, s_password)) {
        set_label(s_wifi_status, "Connecting...");
        start_poll();
    } else {
        set_label(s_wifi_status, "Busy, try again");
    }
}

static void wifi_eye_cb(lv_event_t *)
{
    s_password_visible = !s_password_visible;
    lv_label_set_text(s_wifi_eye_label, s_password_visible ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    show_password();
}

static void wifi_scan_cb(lv_event_t *)
{
    if (!wifi_time_scan_start()) {
        set_label(s_wifi_status, "Busy, try again");
        return;
    }
    s_wifi_phase = kPhaseList;
    for (int i = 0; i < WIFI_TIME_MAX_APS; i++) lv_obj_add_flag(s_ap_btn[i], LV_OBJ_FLAG_HIDDEN);
    update_wifi_visibility();
    set_label(s_wifi_status, "Scanning...");
    start_poll();
}

static void ap_click_cb(lv_event_t *e)
{
    int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    wifi_time_ap_t ap;
    if (!wifi_time_get_ap(idx, &ap)) return;
    strlcpy(s_sel_ssid, ap.ssid, sizeof(s_sel_ssid));
    if (!ap.secure) {
        if (wifi_time_sync_start(s_sel_ssid, nullptr)) {
            set_label(s_wifi_status, "Connecting...");
            start_poll();
        } else {
            set_label(s_wifi_status, "Busy, try again");
        }
        return;
    }
    s_password_len = 0;
    s_password[0] = '\0';
    s_password_visible = false;
    lv_label_set_text(s_wifi_eye_label, LV_SYMBOL_EYE_OPEN);
    show_password();
    s_wifi_phase = kPhasePassword;
    update_wifi_visibility();
    char msg[64];
    snprintf(msg, sizeof(msg), "Password for %s", s_sel_ssid);
    set_label(s_wifi_status, msg);
}

static void build_wifi_tab(lv_obj_t *panel, lv_group_t *group)
{
    /* Top row: scan button + status text. The status sits above the
     * keyboard (never under it) so progress and results stay visible while
     * a password is being typed. */
    lv_obj_t *top = make_flex_row(panel, kKeyGap);
    lv_obj_t *scan = make_button(top, LV_SYMBOL_WIFI " " LV_SYMBOL_REFRESH);
    lv_obj_set_style_min_width(scan, 0, 0);
    lv_obj_set_size(scan, 120, kSmallBtn);
    lv_obj_set_ext_click_area(scan, 0);
    lv_obj_add_event_cb(scan, wifi_scan_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(scan, group);
    s_wifi_status = lv_label_create(top);
    lv_label_set_long_mode(s_wifi_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_wifi_status, 290);
    lv_label_set_text(s_wifi_status, "Scan for networks");

    s_list_box = make_panel(panel);
    lv_obj_set_style_pad_all(s_list_box, kFocusOutlineSlack, 0);
    lv_obj_set_style_pad_row(s_list_box, kApRowGap, 0);
    for (int i = 0; i < WIFI_TIME_MAX_APS; i++) {
        s_ap_btn[i] = make_button(s_list_box, "");
        lv_obj_set_width(s_ap_btn[i], LV_PCT(100));
        lv_obj_set_ext_click_area(s_ap_btn[i], 0);
        lv_obj_t *l = lv_obj_get_child(s_ap_btn[i], 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, LV_PCT(95));
        lv_obj_add_event_cb(s_ap_btn[i], ap_click_cb, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        add_to_group(s_ap_btn[i], group);
        lv_obj_add_flag(s_ap_btn[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_pw_box = make_panel(panel);
    lv_obj_t *row = make_flex_row(s_pw_box, kKeyGap);
    s_password_label = lv_label_create(row);
    lv_obj_t *eye = make_square_button(row, group, LV_SYMBOL_EYE_OPEN, wifi_eye_cb, 0);
    s_wifi_eye_label = lv_obj_get_child(eye, 0);
    lv_obj_set_style_text_font(s_wifi_eye_label, &lv_font_montserrat_32, 0);

    s_wifi_phase = kPhaseList;
    s_password_visible = false;
    s_password_len = 0;
    s_password[0] = '\0';
    show_password();
}

/* ---- BLE tab (Current Time Service client, ADR-006) --------------------- */

static lv_obj_t *s_ble_status;
static lv_obj_t *s_ble_btn[BLE_TIME_MAX_DEVICES];
static lv_timer_t *s_ble_timer;
static ble_time_state_t s_ble_shown_state;

static void fill_ble_list(void)
{
    for (int i = 0; i < BLE_TIME_MAX_DEVICES; i++) {
        ble_time_device_t d;
        if (ble_time_get_device(i, &d)) {
            int quality = 2 * (d.rssi + 100);
            if (quality < 0) quality = 0;
            if (quality > 100) quality = 100;
            char buf[64];
            snprintf(buf, sizeof(buf), "%s%s  %d%%", d.has_cts ? "* " : "", d.name[0] ? d.name : d.addr, quality);
            lv_label_set_text(lv_obj_get_child(s_ble_btn[i], 0), buf);
            lv_obj_remove_flag(s_ble_btn[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ble_btn[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* Same model as the Wi-Fi poll: alive only while an operation the user
 * started is in flight. */
static void ble_poll_cb(lv_timer_t *timer)
{
    ble_time_state_t st = ble_time_get_state();
    if (st == s_ble_shown_state) return;
    s_ble_shown_state = st;
    set_label(s_ble_status, ble_time_get_message());
    bool final_state = (st == BLE_TIME_SCAN_DONE || st == BLE_TIME_SYNC_OK || st == BLE_TIME_FAILED);
    if (st == BLE_TIME_SCAN_DONE) fill_ble_list();
    if (final_state) {
        lv_timer_delete(timer);
        s_ble_timer = nullptr;
    } else {
        board_activity_notify();
    }
}

static void start_ble_poll(void)
{
    s_ble_shown_state = BLE_TIME_IDLE;
    if (!s_ble_timer) s_ble_timer = lv_timer_create(ble_poll_cb, 300, nullptr);
}

static void ble_scan_cb(lv_event_t *)
{
    if (!ble_time_scan_start()) {
        set_label(s_ble_status, "Busy, try again");
        return;
    }
    for (int i = 0; i < BLE_TIME_MAX_DEVICES; i++) lv_obj_add_flag(s_ble_btn[i], LV_OBJ_FLAG_HIDDEN);
    set_label(s_ble_status, "Scanning...");
    start_ble_poll();
}

static void ble_dev_cb(lv_event_t *e)
{
    int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (ble_time_sync_start(idx)) {
        set_label(s_ble_status, "Connecting...");
        start_ble_poll();
    } else {
        set_label(s_ble_status, "Busy, try again");
    }
}

static void build_ble_tab(lv_obj_t *panel, lv_group_t *group)
{
    lv_obj_t *top = make_flex_row(panel, kKeyGap);
    lv_obj_t *scan = make_button(top, LV_SYMBOL_BLUETOOTH " " LV_SYMBOL_REFRESH);
    lv_obj_set_style_min_width(scan, 0, 0);
    lv_obj_set_size(scan, 120, kSmallBtn);
    lv_obj_set_ext_click_area(scan, 0);
    lv_obj_add_event_cb(scan, ble_scan_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(scan, group);
    s_ble_status = lv_label_create(top);
    lv_label_set_long_mode(s_ble_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ble_status, 290);
    lv_label_set_text(s_ble_status, "Scan for CTS Bluetooth");

    lv_obj_t *list = make_panel(panel);
    lv_obj_set_style_pad_all(list, kFocusOutlineSlack, 0);
    lv_obj_set_style_pad_row(list, kApRowGap, 0);
    for (int i = 0; i < BLE_TIME_MAX_DEVICES; i++) {
        s_ble_btn[i] = make_button(list, "");
        lv_obj_set_width(s_ble_btn[i], LV_PCT(100));
        lv_obj_set_ext_click_area(s_ble_btn[i], 0);
        lv_obj_t *l = lv_obj_get_child(s_ble_btn[i], 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, LV_PCT(95));
        lv_obj_add_event_cb(s_ble_btn[i], ble_dev_cb, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        add_to_group(s_ble_btn[i], group);
        lv_obj_add_flag(s_ble_btn[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---- Tabs -------------------------------------------------------------- */

static void update_wifi_visibility(void);

static void select_tab(int tab)
{
    s_tab = tab;
    for (int i = 0; i < kTabCount; i++) {
        bool active = (i == tab);
        if (active) lv_obj_remove_flag(s_panel[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_panel[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_tab_btn[i], active ? lv_color_black() : lv_color_white(), 0);
        lv_obj_set_style_bg_opa(s_tab_btn[i], LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(s_tab_btn[i], active ? lv_color_white() : lv_color_black(), 0);
    }
    update_wifi_visibility();
    if (tab == kTabManual) {
        show_now();
        show_tz();
    }
}

static void tab_cb(lv_event_t *e)
{
    select_tab(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));
}

static void screen_loaded_cb(lv_event_t *)
{
    if (s_tab == kTabManual) show_now();
}

static void screen_unloaded_deferred(void *)
{
    s_digits_len = 0;
    memset(s_digits, 0, sizeof(s_digits));
    show_entry();
    set_label(s_manual_status, "Local date and time");
    s_password_len = 0;
    s_password[0] = '\0';
    s_password_visible = false;
    lv_label_set_text(s_wifi_eye_label, LV_SYMBOL_EYE_OPEN);
    show_password();
    if (s_wifi_phase == kPhasePassword) {
        s_wifi_phase = kPhaseList;
        update_wifi_visibility();
    }
}

static void screen_unloaded_cb(lv_event_t *)
{
    ui_defer(screen_unloaded_deferred, nullptr);
}

lv_obj_t *build_time(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;

    lv_obj_t *screen = make_screen();
    add_back_header(screen, "Time", Screen::Settings, group, battery_label_out);
    lv_obj_t *content = make_content(screen);
    lv_obj_set_style_pad_row(content, 8, 0);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *tabs = make_flex_row(content, kKeyGap);
    static const char *const kTabText[kTabCount] = {LV_SYMBOL_WIFI, LV_SYMBOL_BLUETOOTH, LV_SYMBOL_KEYBOARD};
    for (int i = 0; i < kTabCount; i++) {
        s_tab_btn[i] = make_button(tabs, kTabText[i]);
        lv_obj_set_style_min_width(s_tab_btn[i], 0, 0);
        lv_obj_set_size(s_tab_btn[i], kTabWidth, kTabHeight);
        lv_obj_set_ext_click_area(s_tab_btn[i], 0);
        lv_obj_set_style_text_font(lv_obj_get_child(s_tab_btn[i], 0), &lv_font_montserrat_32, 0);
        lv_obj_add_event_cb(s_tab_btn[i], tab_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        add_to_group(s_tab_btn[i], group);
    }

    for (int i = 0; i < kTabCount; i++) s_panel[i] = make_panel(content);
    build_wifi_tab(s_panel[kTabWifi], group);
    build_ble_tab(s_panel[kTabBle], group);
    build_manual_tab(s_panel[kTabManual], group);

    /* Screen-wide keyboard, anchored under the Wi-Fi panel. */
    static const ui_keyboard_cb_t kCb = {kb_char, kb_backspace, kb_enter, nullptr};
    s_keyboard = ui_keyboard_create(screen, group, &kCb);
    lv_obj_align(s_keyboard, LV_ALIGN_TOP_MID, 0, 414);

    s_digits_len = 0;
    memset(s_digits, 0, sizeof(s_digits));
    show_entry();
    select_tab(kTabManual);

    lv_obj_add_event_cb(screen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    lv_obj_add_event_cb(screen, screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, nullptr);
    return screen;
}
