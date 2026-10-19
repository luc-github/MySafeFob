/*
 Project: MySafeFob  ui_screen_usb_disk.cpp
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
 * @file ui_screen_usb_disk.cpp
 * @brief MySafeFob App (X4 Pro) — USB DISK (2026-10-17): the SD card as a
 *        USB disk on the PC (usb_disk.h), opened from Settings > Security >
 *        Backup after a confirmation. Showing the screen locks the store
 *        (nothing decrypted stays in RAM while a PC is attached), then
 *        starts the disk. One way out: "Done" (or Back) restarts the
 *        device, after asking to eject the disk on the PC first.
 *
 *        The state line ("Waiting for the PC", "Connected"...) is checked
 *        every second but redrawn only when it changes: a handful of
 *        e-paper refreshes per session, each one telling something new.
 *        No auto-sleep and no PIN screen while it is shown (ui_nav.cpp).
 */
#include "ui_screens.h"
#include "ui_widgets.h"
#include "lv_port_indev.h"

extern "C" {
#include "accounts.h"
#include "usb_disk.h"
}

#include "esp_log.h"
#include "app_log_workaround.h"

static const char *TAG = "usb_disk_ui";

static constexpr uint32_t kPollMs = 1000;

static lv_obj_t *s_state;
static lv_obj_t *s_help;
static lv_timer_t *s_timer;
static int s_shown = -1;   /* usb_disk_state_t on screen, -1 = none yet */
static bool s_failed;

static void show_state(void)
{
    if (s_failed) return;   /* the error text stays */
    usb_disk_state_t st = usb_disk_state();
    if (static_cast<int>(st) == s_shown) return;
    s_shown = static_cast<int>(st);
    switch (st) {
    case USB_DISK_OFF: lv_label_set_text(s_state, "Starting..."); break;
    case USB_DISK_WAITING: lv_label_set_text(s_state, "Waiting for the PC"); break;
    case USB_DISK_CONNECTED: lv_label_set_text(s_state, LV_SYMBOL_OK " Connected to the PC"); break;
    case USB_DISK_DISCONNECTED: lv_label_set_text(s_state, "PC disconnected"); break;
    }
}

static void poll_cb(lv_timer_t *)
{
    show_state();
}

static void start_deferred(void *)
{
    esp_err_t err = usb_disk_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
        s_failed = true;
        lv_label_set_text(s_state, LV_SYMBOL_CLOSE " Could not start");
        lv_label_set_text(s_help, err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_RESPONSE
                                      ? "No readable SD card.\nInsert one, then restart."
                                      : "USB disk mode failed.\nRestart the device.");
        return;
    }
    show_state();
}

static void loaded_cb(lv_event_t *)
{
    s_shown = -1;
    s_failed = false;
    lv_label_set_text(s_help, "Copy .msfx backups to or from the MSF folder.\n\n"
                              "When done: eject the disk on the PC,\nthen press Done. The device restarts\n"
                              "and asks for the PIN.");
    accounts_lock();   /* before any PC is attached; ui_nav does not open the PIN screen here */
    show_state();
    if (!s_timer) s_timer = lv_timer_create(poll_cb, kPollMs, nullptr);
    ui_defer(start_deferred, nullptr);   /* after this screen's first refresh */
}

static void restart_confirmed(void *)
{
    lv_label_set_text(s_state, "Restarting...");
    lv_refr_now(nullptr);
    usb_disk_finish_and_restart();
}

static void done_deferred(void *)
{
    if (usb_disk_state() != USB_DISK_CONNECTED) {
        restart_confirmed(nullptr);
        return;
    }
    const ui_confirm_t cfg = {"Disk ejected?", "Eject the disk on the PC first:\nfiles still being written\nwould be damaged.",
                              "Restart", restart_confirmed, nullptr, nullptr, true};
    ui_confirm_show(&cfg);
}

static void done_cb(lv_event_t *)
{
    ui_defer(done_deferred, nullptr);
}

lv_obj_t *build_usb_disk(lv_group_t **group_out, lv_obj_t **battery_label_out)
{
    lv_group_t *group = lv_port_indev_new_group();
    *group_out = group;
    lv_obj_t *screen = make_screen();
    add_back_header_cb(screen, LV_SYMBOL_USB " USB disk", done_cb, nullptr, group, battery_label_out);

    s_state = lv_label_create(screen);
    lv_obj_set_style_text_font(s_state, &msf_font_32, 0);
    lv_obj_set_width(s_state, LV_PCT(90));
    lv_obj_set_style_text_align(s_state, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_state, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_state, LV_ALIGN_TOP_MID, 0, 200);

    s_help = lv_label_create(screen);
    lv_obj_set_width(s_help, LV_PCT(90));
    lv_obj_set_style_text_align(s_help, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_help, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_help, LV_ALIGN_TOP_MID, 0, 290);

    /* Below the X4 Pro touch band. */
    lv_obj_t *done = make_button(screen, LV_SYMBOL_OK " Done: restart");
    lv_obj_set_style_min_width(done, 280, 0);
    lv_obj_align(done, LV_ALIGN_TOP_MID, 0, touch_safe_y(kTouchBandTop, kMinTouchTarget) + 60);
    lv_obj_add_event_cb(done, done_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(done, group);

    lv_obj_add_event_cb(screen, loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
    return screen;
}
