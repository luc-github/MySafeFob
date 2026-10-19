/*
 Project: MySafeFob  ui_screen_backup.cpp
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
 * @file ui_screen_backup.cpp
 * @brief MySafeFob App — encrypted SD backup screens (ROADMAP 8.0 P11 step
 *        B, ADR-005 + amendment 2026-10-11). Reached from Settings >
 *        Security > Backup after ONE device PIN confirmation (pin screen,
 *        CONFIRM mode).
 *
 *        BACKUP         Export / Import, and the last result.
 *        BACKUP_PASS    passphrase on the full keyboard, masked (eye to
 *                       show): twice for an export (8-64 characters), once
 *                       for an import. Export with two profiles or more:
 *                       a Scope button, all profiles (default) or the
 *                       active one only (2026-10-17).
 *        BACKUP_FILES   the .msfx files of the card, newest first, pages of
 *                       6 rows (no scrolling on e-paper).
 *        BACKUP_IMPORT  the backup's profiles and what each mode would do
 *                       (new / changed / unchanged, removed for Replace),
 *                       then Merge or Replace (the latter behind the shared
 *                       confirmation). Replace only touches the backup's
 *                       profiles. A backup without profiles can go "Into"
 *                       the active profile instead of Default.
 *
 *        Card access and Argon2id (seconds) run in a worker task; a poll
 *        timer picks up the result, so the screen keeps drawing. Decrypted
 *        records live in PSRAM only while BACKUP_IMPORT is shown, then are
 *        wiped.
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "ui_keyboard.h"
#include "ui_nav.h"
#include "msf_symbols.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

extern "C" {
#include "accounts.h"
#include "backup.h"
#include "settings_store.h"
#include "time_service.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

#include "app_log_workaround.h"

#include <atomic>
#include <cstdio>
#include <cstring>

static const char *TAG = "backup_ui";

/* ---- shared state ------------------------------------------------------- */

enum Flow { kFlowExport, kFlowImport };
enum Job { kJobNone, kJobList, kJobExport, kJobLoad, kJobApply };

static constexpr int kMaxFiles = 48;
static constexpr int kRowsPerPage = 6;

static Flow s_flow;
static int s_pass_step;                                /* export: 0 first entry, 1 confirmation */
static char s_entry[MSF_BACKUP_PASS_MAX + 1];          /* being typed */
static int s_entry_len;
static char s_pass[MSF_BACKUP_PASS_MAX + 1];           /* accepted passphrase */
static bool s_reveal;

static backup_file_t *s_files;                         /* PSRAM, kMaxFiles */
static int s_file_count = -2;                          /* -2 not read yet, -1 no card */
static int s_page;
static char s_file[BACKUP_NAME_MAX];                   /* chosen for import */

static account_t *s_recs;                              /* PSRAM, decrypted backup */
static int s_rec_count;
static bool s_loaded;
static accounts_import_summary_t s_merge, s_replace;
static accounts_import_mode_t s_apply_mode;

static char s_menu_status[160];                        /* last result, shown on BACKUP */

/* Profiles (2026-10-17) */
static bool s_export_one;                              /* export the active profile only */
static char s_export_profile[ACCOUNTS_PROFILE_MAX + 1];
static bool s_profileless;                             /* loaded backup: every record in Default */
static bool s_into_active;                             /* ...imported into the active profile instead */

/* Worker */
static Job s_job;
static std::atomic<bool> s_busy{false};
static std::atomic<bool> s_done{false};
static esp_err_t s_job_err;
static accounts_result_t s_job_result;
static char s_job_name[BACKUP_NAME_MAX];
static int s_job_count;
static lv_timer_t *s_poll;

static void wipe(void *buf, size_t len)
{
    volatile uint8_t *p = static_cast<volatile uint8_t *>(buf);
    for (size_t i = 0; i < len; i++) p[i] = 0;
}

static void wipe_records(void)
{
    if (s_recs) wipe(s_recs, MSF_STORE_MAX_ACCOUNTS * sizeof(account_t));
    s_rec_count = 0;
    s_loaded = false;
}

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static const char *err_text(esp_err_t err)
{
    switch (err) {
    case ESP_ERR_NOT_FOUND: return "No SD card (FAT32), or the file is gone.";
    case ESP_ERR_INVALID_ARG: return "The passphrase must be 8 to 64 characters.";
    case ESP_ERR_INVALID_STATE: return "The store is locked.";
    case ESP_ERR_INVALID_RESPONSE: return "Wrong passphrase, or the file was altered.";
    case ESP_ERR_INVALID_VERSION: return "Not a MySafeFob backup, or a newer format.";
    case ESP_ERR_INVALID_SIZE: return "Too many accounts, or a damaged file.";
    case ESP_ERR_NO_MEM: return "Out of memory.";
    default: return "Read or write error.";
    }
}

/* "2026-10-11 15:30" in local time, or the file name if the date is unknown. */
static void file_label(const backup_file_t *f, char *out, size_t size)
{
    if (f->created <= 0) {
        snprintf(out, size, "%s  (%u)", f->name, f->count);
        return;
    }
    time_service_dt_t dt;
    time_service_epoch_to_dt(static_cast<time_t>(f->created + static_cast<int64_t>(settings_store_get_time_tz_offset_min()) * 60), &dt);
    snprintf(out, size, "%04d-%02d-%02d %02d:%02d  (%u)", dt.year, dt.month, dt.day, dt.hour, dt.minute, f->count);
}

/* ---- worker ------------------------------------------------------------- */

static void worker_task(void *)
{
    switch (s_job) {
    case kJobList:
        s_file_count = backup_list(s_files, kMaxFiles);
        s_job_err = s_file_count < 0 ? ESP_ERR_NOT_FOUND : ESP_OK;
        break;
    case kJobExport:
        s_job_err = backup_export(s_pass, s_export_one ? s_export_profile : nullptr, s_job_name, &s_job_count);
        break;
    case kJobLoad:
        s_job_err = backup_load(s_file, s_pass, s_recs, MSF_STORE_MAX_ACCOUNTS, &s_rec_count);
        break;
    case kJobApply: {
        accounts_import_summary_t sum;
        s_job_result = accounts_import(s_recs, s_rec_count, s_apply_mode, &sum);
        if (s_apply_mode == ACCOUNTS_IMPORT_MERGE) s_merge = sum;
        else s_replace = sum;
        s_job_err = s_job_result == ACCOUNTS_OK ? ESP_OK : ESP_FAIL;
        break;
    }
    default: s_job_err = ESP_FAIL; break;
    }
    /* The passphrase is not needed after an export or a load. */
    if (s_job == kJobExport || s_job == kJobLoad) wipe(s_pass, sizeof(s_pass));
    s_done = true;
    vTaskDelete(nullptr);
}

static void job_finished(Job job);

static void poll_cb(lv_timer_t *)
{
    if (!s_busy || !s_done) return;
    Job job = s_job;
    s_busy = false;
    s_job = kJobNone;
    lv_timer_delete(s_poll);
    s_poll = nullptr;
    ESP_LOGI(TAG, "job %d: %s", static_cast<int>(job), esp_err_to_name(s_job_err));
    job_finished(job);
}

static bool start_job(Job job)
{
    if (s_busy) return false;
    s_job = job;
    s_done = false;
    s_busy = true;
    if (xTaskCreate(worker_task, "backup_job", 12288, nullptr, 3, nullptr) != pdPASS) {
        s_busy = false;
        s_job = kJobNone;
        return false;
    }
    if (!s_poll) s_poll = lv_timer_create(poll_cb, 200, nullptr);
    board_activity_notify();   /* a long job is activity: no idle sleep in the middle */
    return true;
}

/* ======================================================================== */
/* BACKUP (menu)                                                            */
/* ======================================================================== */

static lv_obj_t *s_menu_status_label;

static void menu_show(void)
{
    set_text(s_menu_status_label, s_menu_status[0] ? s_menu_status
             : "The backup is encrypted with a passphrase\nof your choice (not the PIN). Without it,\n"
               "the file cannot be read: keep it safe.");
}

static void export_deferred(void *)
{
    if (s_busy) return;
    s_flow = kFlowExport;
    s_pass_step = 0;
    s_export_one = false;   /* a full backup unless asked otherwise */
    strlcpy(s_export_profile, accounts_profile(), sizeof(s_export_profile));
    switch_screen(Screen::BackupPass);
}

static void export_cb(lv_event_t *)
{
    ui_defer(export_deferred, nullptr);
}

static void import_deferred(void *)
{
    if (s_busy) return;
    s_flow = kFlowImport;
    s_page = 0;
    switch_screen(Screen::BackupFiles);
}

static void import_cb(lv_event_t *)
{
    ui_defer(import_deferred, nullptr);
}

static void sd_check_deferred(void *user_data)
{
    settings_store_set_sd_check(static_cast<bool>(reinterpret_cast<intptr_t>(user_data)));
}

static void sd_check_cb(lv_event_t *e)
{
    bool checked = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    ui_defer(sd_check_deferred, reinterpret_cast<void *>(static_cast<intptr_t>(checked)));
}

static void menu_back_deferred(void *)
{
    if (s_busy) return;   /* an export is writing: stay until it ends */
    s_menu_status[0] = '\0';
    switch_screen(Screen::SettingsSecurity);
}

static void menu_back_cb(lv_event_t *)
{
    ui_defer(menu_back_deferred, nullptr);
}

static void menu_loaded_cb(lv_event_t *)
{
    menu_show();
}

void backup_open_menu(void)
{
    s_menu_status[0] = '\0';
    switch_screen(Screen::Backup);
}

static void add_menu_action(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_group_t *group)
{
    lv_obj_t *btn = make_button(parent, text);
    lv_obj_set_width(btn, LV_PCT(90));
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(btn, group);
}

/* USB disk mode (2026-10-17, ui_screen_usb_disk.cpp). */
static void usb_disk_confirmed(void *)
{
    switch_screen(Screen::UsbDisk);
}

static void usb_disk_deferred(void *)
{
    if (s_busy) return;
    const ui_confirm_t cfg = {"SD card on the PC?",
                              "The SD card becomes a USB disk\non the connected PC. The store is\nlocked, and "
                              "the device restarts\nwhen you are done.",
                              "Start", usb_disk_confirmed, nullptr, nullptr, false};
    ui_confirm_show(&cfg);
}

static void usb_disk_cb(lv_event_t *)
{
    ui_defer(usb_disk_deferred, nullptr);
}

lv_obj_t *build_backup(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;
    lv_obj_t *screen = make_screen();
    add_back_header_cb(screen, LV_SYMBOL_SD_CARD " Backup", menu_back_cb, nullptr, group, battery_label_out);
    lv_obj_t *content = make_content(screen);
    lv_obj_set_style_pad_row(content, 2 * kExtClickMargin, 0);   /* as the Settings menus */
    add_menu_action(content, MSF_SYMBOL_FILE_EXPORT "  Export to the SD card", export_cb, group);
    add_menu_action(content, MSF_SYMBOL_FILE_IMPORT "  Import from the SD card", import_cb, group);
    add_menu_action(content, LV_SYMBOL_USB "  SD card as a USB disk", usb_disk_cb, group);
    /* Keeps the toggle below the X4 Pro touch band (ui_widgets.h): the
     * three actions above end at y = 408, the band runs 490-554. */
    lv_obj_t *spacer = lv_obj_create(content);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 1, kTouchBandBottom + 6 - 408 - 2 * (2 * kExtClickMargin));
    /* SD sanity check after unlock (ui_sd_check.cpp): against the right
     * edge, no stepper on this screen to line up with. */
    lv_obj_t *check_row = make_round_toggle_row(content, group, MSF_SYMBOL_SEARCH " Check SD at start",
                                                settings_store_get_sd_check(), sd_check_cb);
    lv_obj_set_style_margin_right(lv_obj_get_child(check_row, -1), 0, 0);
    s_menu_status_label = lv_label_create(content);
    lv_obj_set_width(s_menu_status_label, LV_PCT(90));
    lv_obj_set_style_text_align(s_menu_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_menu_status_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_menu_status_label, "");
    lv_obj_add_event_cb(screen, menu_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    return screen;
}

/* ======================================================================== */
/* BACKUP_PASS                                                              */
/* ======================================================================== */

static constexpr int32_t kFieldY = 176;
static constexpr int32_t kScopeY = 330;   /* below the field, even with 64 characters shown in clear */
static lv_obj_t *s_pass_title, *s_pass_field, *s_pass_status, *s_eye_label;
static lv_obj_t *s_scope_btn, *s_scope_label;

static void scope_show(void)
{
    bool show = s_flow == kFlowExport && accounts_profile_count() >= 2;
    set_hidden(s_scope_btn, !show);
    if (!show) return;
    char buf[ACCOUNTS_PROFILE_MAX + 24];
    if (s_export_one) snprintf(buf, sizeof(buf), "Export: %s only", accounts_profile_label(s_export_profile));
    else snprintf(buf, sizeof(buf), "Export: all profiles");
    set_text(s_scope_label, buf);
}

static void scope_deferred(void *)
{
    s_export_one = !s_export_one;
    scope_show();
}

static void scope_cb(lv_event_t *)
{
    ui_defer(scope_deferred, nullptr);
}

static void pass_show(void)
{
    char buf[MSF_BACKUP_PASS_MAX + 16];
    if (s_reveal) {
        snprintf(buf, sizeof(buf), "%s", s_entry);
    } else {
        int n = s_entry_len < 24 ? s_entry_len : 24;   /* one line of stars, the count says the rest */
        memset(buf, '*', n);
        snprintf(buf + n, sizeof(buf) - n, " (%d)", s_entry_len);
    }
    set_text(s_pass_field, s_entry_len ? buf : "");
    set_text(s_eye_label, s_reveal ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

static void pass_clear_entry(void)
{
    wipe(s_entry, sizeof(s_entry));
    s_entry_len = 0;
}

static void pass_kb_char(char c, void *)
{
    if (s_entry_len >= MSF_BACKUP_PASS_MAX) {
        set_text(s_pass_status, "64 characters at most");
        return;
    }
    s_entry[s_entry_len++] = c;
    s_entry[s_entry_len] = '\0';
    set_text(s_pass_status, "");
    pass_show();
}

static void pass_kb_backspace(void *)
{
    if (s_entry_len > 0) s_entry[--s_entry_len] = '\0';
    pass_show();
}

static void pass_kb_enter(void *)
{
    if (s_flow == kFlowImport) {
        if (s_entry_len == 0) return;
        strlcpy(s_pass, s_entry, sizeof(s_pass));
        pass_clear_entry();
        switch_screen(Screen::BackupImport);   /* starts the load */
        return;
    }
    if (s_pass_step == 0) {
        if (s_entry_len < MSF_BACKUP_PASS_MIN) {
            set_text(s_pass_status, "8 characters at least");
            return;
        }
        strlcpy(s_pass, s_entry, sizeof(s_pass));
        pass_clear_entry();
        s_pass_step = 1;
        set_text(s_pass_title, MSF_SYMBOL_KEY " Passphrase again");
        set_text(s_pass_status, "");
        pass_show();
        return;
    }
    if (strcmp(s_entry, s_pass) != 0) {
        wipe(s_pass, sizeof(s_pass));
        pass_clear_entry();
        s_pass_step = 0;
        set_text(s_pass_title, MSF_SYMBOL_KEY " New passphrase");
        set_text(s_pass_status, "The two entries differ: start again");
        pass_show();
        return;
    }
    pass_clear_entry();
    if (start_job(kJobExport)) snprintf(s_menu_status, sizeof(s_menu_status), "Encrypting and writing...");
    else snprintf(s_menu_status, sizeof(s_menu_status), LV_SYMBOL_CLOSE " Busy, try again");
    switch_screen(Screen::Backup);
}

static void eye_deferred(void *)
{
    s_reveal = !s_reveal;
    pass_show();
}

static void eye_cb(lv_event_t *)
{
    ui_defer(eye_deferred, nullptr);
}

static void pass_back_deferred(void *)
{
    pass_clear_entry();
    wipe(s_pass, sizeof(s_pass));
    switch_screen(s_flow == kFlowImport ? Screen::BackupFiles : Screen::Backup);
}

static void pass_back_cb(lv_event_t *)
{
    ui_defer(pass_back_deferred, nullptr);
}

static void pass_loaded_cb(lv_event_t *)
{
    s_reveal = false;
    pass_clear_entry();
    const char *title = s_flow == kFlowImport ? MSF_SYMBOL_KEY " Backup passphrase"
                        : s_pass_step == 0    ? MSF_SYMBOL_KEY " New passphrase"
                                              : MSF_SYMBOL_KEY " Passphrase again";
    set_text(s_pass_title, title);
    set_text(s_pass_status, s_flow == kFlowImport ? "" : "8 to 64 characters, not the PIN");
    pass_show();
    scope_show();
}

static void pass_unloaded_cb(lv_event_t *)
{
    pass_clear_entry();
    s_reveal = false;
}

lv_obj_t *build_backup_pass(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;
    lv_obj_t *screen = make_screen();
    lv_obj_t *bar = add_back_header_cb(screen, "", pass_back_cb, nullptr, group, battery_label_out);
    s_pass_title = lv_obj_get_child(bar, 0);

    lv_obj_t *eye = make_button(screen, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_min_width(eye, kMinTouchTarget, 0);
    lv_obj_align(eye, LV_ALIGN_TOP_RIGHT, -16, kBackTopMargin);
    lv_obj_add_event_cb(eye, eye_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(eye, group);
    s_eye_label = lv_obj_get_child(eye, 0);

    lv_obj_t *box = lv_obj_create(screen);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(box, kMinTouchTarget, 0);
    lv_obj_set_style_border_width(box, kButtonBorderWidth, 0);
    lv_obj_set_style_border_color(box, lv_color_black(), 0);
    lv_obj_set_style_radius(box, kButtonRadius, 0);
    lv_obj_set_style_pad_all(box, 10, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, kFieldY);
    s_pass_field = lv_label_create(box);
    lv_obj_set_width(s_pass_field, LV_PCT(100));
    lv_label_set_long_mode(s_pass_field, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_pass_field, "");

    s_scope_btn = make_button(screen, "");
    lv_obj_set_width(s_scope_btn, LV_PCT(90));
    lv_obj_align(s_scope_btn, LV_ALIGN_TOP_MID, 0, kScopeY);
    lv_obj_add_event_cb(s_scope_btn, scope_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(s_scope_btn, group);
    s_scope_label = lv_obj_get_child(s_scope_btn, 0);
    lv_label_set_long_mode(s_scope_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_scope_label, LV_PCT(90));
    lv_obj_set_style_text_align(s_scope_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_scope_label);
    set_hidden(s_scope_btn, true);

    s_pass_status = lv_label_create(screen);
    lv_label_set_text(s_pass_status, "");
    lv_obj_align(s_pass_status, LV_ALIGN_TOP_MID, 0, 412);

    static const ui_keyboard_cb_t kCb = {pass_kb_char, pass_kb_backspace, pass_kb_enter, nullptr};
    lv_obj_t *kb = ui_keyboard_create(screen, group, &kCb, UI_KEYBOARD_FULL);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_obj_add_event_cb(screen, pass_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    lv_obj_add_event_cb(screen, pass_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, nullptr);
    ui_auto_clear_attach(screen);   /* F-05: a passphrase is a secret */
    return screen;
}

/* ======================================================================== */
/* BACKUP_FILES                                                             */
/* ======================================================================== */

static constexpr int32_t kRowTop = 178;
static constexpr int32_t kRowH = kMinTouchTarget;
static constexpr int32_t kRowGap = 28;
static constexpr int32_t kPagerY = 720;
static constexpr int32_t kArrowW = 72;
static constexpr int32_t kSideMargin = 24;

static lv_group_t *s_files_group;
static lv_obj_t *s_rows[kRowsPerPage], *s_row_labels[kRowsPerPage];
static lv_obj_t *s_prev, *s_next, *s_page_label, *s_files_status;

static void files_show(void)
{
    int count = s_file_count < 0 ? 0 : s_file_count;
    int pages = count == 0 ? 1 : (count + kRowsPerPage - 1) / kRowsPerPage;
    if (s_page >= pages) s_page = pages - 1;
    if (s_page < 0) s_page = 0;
    for (int slot = 0; slot < kRowsPerPage; slot++) {
        int i = s_page * kRowsPerPage + slot;
        if (i < count) {
            char buf[64];
            file_label(&s_files[i], buf, sizeof(buf));
            set_text(s_row_labels[slot], buf);
        }
        set_hidden(s_rows[slot], i >= count);
    }
    set_hidden(s_prev, s_page == 0);
    set_hidden(s_next, s_page >= pages - 1);
    set_hidden(s_page_label, pages <= 1);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d / %d", s_page + 1, pages);
    set_text(s_page_label, buf);

    const char *status = s_busy && s_job == kJobList ? "Reading the SD card..."
                         : s_file_count == -1        ? "No SD card (FAT32)"
                         : s_file_count == 0         ? "No backup in the MSF folder"
                                                     : "";
    set_text(s_files_status, status);
    if (count > 0) lv_group_focus_obj(s_rows[0]);
}

static void row_deferred(void *user_data)
{
    int i = s_page * kRowsPerPage + static_cast<int>(reinterpret_cast<intptr_t>(user_data));
    if (s_busy || i >= s_file_count) return;
    strlcpy(s_file, s_files[i].name, sizeof(s_file));
    ESP_LOGI(TAG, "import %s", s_file);
    switch_screen(Screen::BackupPass);
}

static void row_cb(lv_event_t *e)
{
    ui_defer(row_deferred, lv_event_get_user_data(e));
}

static void page_deferred(void *user_data)
{
    s_page += static_cast<int>(reinterpret_cast<intptr_t>(user_data));
    files_show();
    lv_port_disp_request_full_refresh();
}

static void page_cb(lv_event_t *e)
{
    ui_defer(page_deferred, lv_event_get_user_data(e));
}

static void files_back_deferred(void *)
{
    if (s_busy) return;
    switch_screen(Screen::Backup);
}

static void files_back_cb(lv_event_t *)
{
    ui_defer(files_back_deferred, nullptr);
}

/* Re-read the card on every visit: it may have been swapped meanwhile. */
static void files_loaded_deferred(void *)
{
    if (!s_files) s_files = static_cast<backup_file_t *>(heap_caps_calloc(kMaxFiles, sizeof(backup_file_t), MALLOC_CAP_SPIRAM));
    s_file_count = 0;
    if (s_files && start_job(kJobList)) {
        files_show();
    } else {
        s_file_count = -1;
        files_show();
    }
}

static void files_loaded_cb(lv_event_t *)
{
    ui_defer(files_loaded_deferred, nullptr);
}

static lv_obj_t *make_fixed_button(lv_obj_t *screen, const char *text, int32_t w)
{
    lv_obj_t *btn = make_button(screen, text);
    lv_obj_set_style_min_width(btn, 0, 0);
    lv_obj_set_size(btn, w, kRowH);
    lv_obj_set_ext_click_area(btn, 0);   /* gaps < 2 x kExtClickMargin */
    add_to_group(btn, s_files_group);
    return btn;
}

lv_obj_t *build_backup_files(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    s_files_group = lv_port_indev_new_group();
    *group_out = s_files_group;
    lv_obj_t *screen = make_screen();
    add_back_header_cb(screen, MSF_SYMBOL_FILE_IMPORT " Choose a backup", files_back_cb, nullptr, s_files_group,
                       battery_label_out);

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
        set_hidden(row, true);
        y += kRowH + kRowGap;
    }

    /* Status line in the gap the touch band leaves between rows 4 and 5. */
    s_files_status = lv_label_create(screen);
    lv_label_set_text(s_files_status, "");
    lv_obj_align(s_files_status, LV_ALIGN_TOP_MID, 0, kTouchBandTop + 10);

    s_prev = make_fixed_button(screen, LV_SYMBOL_LEFT, kArrowW);
    lv_obj_align(s_prev, LV_ALIGN_TOP_LEFT, kSideMargin, kPagerY);
    lv_obj_add_event_cb(s_prev, page_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(-1)));
    s_page_label = lv_label_create(screen);
    lv_obj_align(s_page_label, LV_ALIGN_TOP_MID, 0, kPagerY + 14);
    s_next = make_fixed_button(screen, LV_SYMBOL_RIGHT, kArrowW);
    lv_obj_align(s_next, LV_ALIGN_TOP_RIGHT, -kSideMargin, kPagerY);
    lv_obj_add_event_cb(s_next, page_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(1)));
    set_hidden(s_prev, true);
    set_hidden(s_next, true);
    set_hidden(s_page_label, true);

    lv_obj_add_event_cb(screen, files_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    return screen;
}

/* ======================================================================== */
/* BACKUP_IMPORT                                                            */
/* ======================================================================== */

static constexpr int32_t kInfoY = 168;
/* Below the touch band: the summary (up to ~8 lines) has 168-480 to itself. */
static constexpr int32_t kModeBtnY = kTouchBandBottom + 12;
static constexpr int32_t kModeBtnGap = 20;

static lv_obj_t *s_info, *s_merge_btn, *s_replace_btn, *s_into_btn, *s_into_label;

/* "Into" only for a backup without profiles, while another profile than
 * Default is active. */
static bool into_offered(void)
{
    return s_profileless && accounts_profile()[0] != '\0';
}

static void import_show_buttons(bool show)
{
    set_hidden(s_merge_btn, !show);
    set_hidden(s_replace_btn, !show);
    set_hidden(s_into_btn, !show || !into_offered());
    if (show) lv_group_focus_obj(s_merge_btn);
}

/* "Luc, Pat, Min (+2)": the profiles the backup holds. */
static void profiles_text(char *out, size_t size)
{
    char names[3][ACCOUNTS_PROFILE_MAX + 1];
    int n = accounts_records_profiles(s_recs, s_rec_count, names, 3);
    int pos = 0;
    out[0] = '\0';
    for (int i = 0; i < n && i < 3; i++) {
        pos += snprintf(out + pos, size - pos, "%s%s", i ? ", " : "", accounts_profile_label(names[i]));
        if (pos >= static_cast<int>(size)) return;
    }
    if (n > 3) snprintf(out + pos, size - pos, " (+%d)", n - 3);
}

static void import_preview(void)
{
    accounts_import_preview(s_recs, s_rec_count, ACCOUNTS_IMPORT_MERGE, &s_merge);
    accounts_import_preview(s_recs, s_rec_count, ACCOUNTS_IMPORT_REPLACE, &s_replace);
}

static void import_show_preview(void)
{
    char profiles[3 * (ACCOUNTS_PROFILE_MAX + 2) + 16];
    profiles_text(profiles, sizeof(profiles));
    char buf[448];
    int pos = snprintf(buf, sizeof(buf), "%s\n%d account(s), profile(s): %s", s_file, s_rec_count, profiles);
    if (s_merge.invalid) pos += snprintf(buf + pos, sizeof(buf) - pos, "\n%d unusable (skipped)", s_merge.invalid);
    pos += snprintf(buf + pos, sizeof(buf) - pos, "\n\nMerge: %d new, %d changed,\n%d unchanged", s_merge.added,
                    s_merge.changed, s_merge.unchanged);
    if (s_merge.full) pos += snprintf(buf + pos, sizeof(buf) - pos, ", %d left out (full)", s_merge.full);
    snprintf(buf + pos, sizeof(buf) - pos, "\nReplace: the same, and %d removed\nfrom these profiles (others untouched)",
             s_replace.removed);
    set_text(s_info, buf);
    char into[ACCOUNTS_PROFILE_MAX + 16];
    snprintf(into, sizeof(into), "Into: %s", s_into_active ? accounts_profile_label(accounts_profile())
                                                           : ACCOUNTS_PROFILE_DEFAULT_LABEL);
    set_text(s_into_label, into);
    import_show_buttons(true);
}

/* Default <-> active profile, for a backup without profiles. */
static void into_deferred(void *)
{
    if (s_busy || !s_loaded || !into_offered()) return;
    s_into_active = !s_into_active;
    accounts_records_set_profile(s_recs, s_rec_count, s_into_active ? accounts_profile() : "");
    import_preview();
    import_show_preview();
    lv_group_focus_obj(s_into_btn);
}

static void into_cb(lv_event_t *)
{
    ui_defer(into_deferred, nullptr);
}

static void apply(accounts_import_mode_t mode)
{
    s_apply_mode = mode;
    if (!start_job(kJobApply)) return;
    set_text(s_info, "Importing...");
    import_show_buttons(false);
}

static void merge_deferred(void *)
{
    if (s_busy || !s_loaded) return;
    if (s_merge.added == 0 && s_merge.changed == 0) {
        set_text(s_info, LV_SYMBOL_OK " Nothing to change:\nthe device already matches the backup.");
        import_show_buttons(false);
        wipe_records();
        return;
    }
    apply(ACCOUNTS_IMPORT_MERGE);
}

static void merge_cb(lv_event_t *)
{
    ui_defer(merge_deferred, nullptr);
}

static void replace_confirmed(void *)
{
    apply(ACCOUNTS_IMPORT_REPLACE);
}

static char s_confirm_msg[200];

static void replace_deferred(void *)
{
    if (s_busy || !s_loaded) return;
    snprintf(s_confirm_msg, sizeof(s_confirm_msg),
             "The backup's profiles will hold exactly\nthe backup: %d removed, %d new, %d changed.\n"
             "Other profiles are untouched.", s_replace.removed, s_replace.added, s_replace.changed);
    const ui_confirm_t cfg = {"Replace these profiles?", s_confirm_msg, "Replace", replace_confirmed, nullptr, nullptr,
                              true};
    ui_confirm_show(&cfg);
}

static void replace_cb(lv_event_t *)
{
    ui_defer(replace_deferred, nullptr);
}

static void import_back_deferred(void *)
{
    if (s_busy) return;
    wipe_records();
    switch_screen(Screen::BackupFiles);
}

static void import_back_cb(lv_event_t *)
{
    ui_defer(import_back_deferred, nullptr);
}

static void import_loaded_deferred(void *)
{
    wipe_records();
    if (!s_recs) s_recs = static_cast<account_t *>(heap_caps_calloc(MSF_STORE_MAX_ACCOUNTS, sizeof(account_t), MALLOC_CAP_SPIRAM));
    import_show_buttons(false);
    if (!s_recs || !start_job(kJobLoad)) {
        wipe(s_pass, sizeof(s_pass));
        set_text(s_info, LV_SYMBOL_CLOSE " Out of memory.");
        return;
    }
    set_text(s_info, "Decrypting...");
}

static void import_loaded_cb(lv_event_t *)
{
    ui_defer(import_loaded_deferred, nullptr);
}

static void import_unloaded_cb(lv_event_t *)
{
    if (!s_busy) wipe_records();   /* a running job finishes first, then wipes in job_finished() */
}

static lv_obj_t *make_mode_button(lv_obj_t *screen, lv_group_t *group, const char *text, lv_event_cb_t cb, int32_t y)
{
    lv_obj_t *btn = make_button(screen, text);
    lv_obj_set_width(btn, LV_PCT(80));
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(btn, group);
    return btn;
}

lv_obj_t *build_backup_import(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;
    lv_obj_t *screen = make_screen();
    add_back_header_cb(screen, MSF_SYMBOL_FILE_IMPORT " Import", import_back_cb, nullptr, group, battery_label_out);

    s_info = lv_label_create(screen);
    lv_obj_set_width(s_info, LV_PCT(90));
    lv_obj_set_style_text_align(s_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_info, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_info, LV_ALIGN_TOP_MID, 0, kInfoY);
    lv_label_set_text(s_info, "");

    s_merge_btn = make_mode_button(screen, group, LV_SYMBOL_DOWNLOAD " Merge", merge_cb, kModeBtnY);
    s_replace_btn = make_mode_button(screen, group, LV_SYMBOL_REFRESH " Replace", replace_cb,
                                     kModeBtnY + kMinTouchTarget + kModeBtnGap);
    s_into_btn = make_mode_button(screen, group, "", into_cb, kModeBtnY + 2 * (kMinTouchTarget + kModeBtnGap));
    s_into_label = lv_obj_get_child(s_into_btn, 0);
    lv_label_set_long_mode(s_into_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_into_label, LV_PCT(90));
    lv_obj_set_style_text_align(s_into_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_into_label);
    import_show_buttons(false);

    lv_obj_add_event_cb(screen, import_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    lv_obj_add_event_cb(screen, import_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, nullptr);
    return screen;
}

/* ======================================================================== */
/* job results                                                              */
/* ======================================================================== */

static void job_finished(Job job)
{
    switch (job) {
    case kJobList:
        files_show();
        break;
    case kJobExport:
        if (s_job_err == ESP_OK) {
            snprintf(s_menu_status, sizeof(s_menu_status), LV_SYMBOL_OK " Saved on the SD card:\nMSF/%s\n(%d account(s)%s)",
                     s_job_name, s_job_count, s_export_one ? ", one profile" : "");
        } else {
            snprintf(s_menu_status, sizeof(s_menu_status), LV_SYMBOL_CLOSE " Export failed:\n%s", err_text(s_job_err));
        }
        menu_show();
        break;
    case kJobLoad:
        if (s_job_err != ESP_OK) {
            wipe_records();
            char buf[128];
            snprintf(buf, sizeof(buf), LV_SYMBOL_CLOSE " %s", err_text(s_job_err));
            set_text(s_info, buf);
            break;
        }
        s_loaded = true;
        {
            char first[1][ACCOUNTS_PROFILE_MAX + 1];
            s_profileless = accounts_records_profiles(s_recs, s_rec_count, first, 1) == 1 && first[0][0] == '\0';
            s_into_active = false;   /* as in the file until asked */
        }
        import_preview();
        import_show_preview();
        break;
    case kJobApply: {
        const accounts_import_summary_t &sum = s_apply_mode == ACCOUNTS_IMPORT_MERGE ? s_merge : s_replace;
        char buf[200];
        if (s_job_err == ESP_OK) {
            snprintf(buf, sizeof(buf), LV_SYMBOL_OK " Imported:\n%d new, %d changed, %d removed", sum.added,
                     sum.changed, sum.removed);
        } else {
            snprintf(buf, sizeof(buf), LV_SYMBOL_CLOSE " %s\nNothing was changed.", accounts_result_text(s_job_result));
        }
        set_text(s_info, buf);
        wipe_records();
        break;
    }
    default: break;
    }
    /* Left BACKUP_IMPORT while decrypting: nothing may stay in RAM. */
    if (job == kJobLoad && lv_screen_active() != lv_obj_get_screen(s_info)) wipe_records();
}
