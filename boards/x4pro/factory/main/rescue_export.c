/*
 Project: MySafeFob  rescue_export.c
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
 * @file rescue_export.c
 * @brief MySafeFob Factory — rescue export, see rescue_export.h.
 *
 * Screens use the factory's 1 bpp gfx (font 16x32: 29 characters per line
 * inside the frame). Only the validated inputs: Left/Right buttons, Home
 * pad (touch zone, no calibration needed), Power.
 */
#include "rescue_export.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "buttons.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gfx.h"
#include "hw_config.h"
#include "sdcard.h"
#include "secret_store.h"
#include "touch.h"

static const char *TAG = "RESCUE";

#define RESCUE_DIR "/sdcard/MSF"
#define RESCUE_TMP RESCUE_DIR "/RESCUE.TMP"   /* 8.3 names: no long file names in the factory */

typedef enum { IN_NONE, IN_LEFT, IN_RIGHT, IN_HOME, IN_POWER } input_t;

static bool s_touch_ok;
static bool s_touch_was_pressed;

static input_t wait_input(void)
{
    while (1) {
        button_id_t b = button_wait_press(50);
        if (b == BTN_1) return IN_LEFT;
        if (b == BTN_2) return IN_RIGHT;
        if (b == BTN_3) return IN_POWER;
        if (s_touch_ok) {
            touch_point_t tp = touch_read();
            bool edge = tp.pressed && !s_touch_was_pressed;
            s_touch_was_pressed = tp.pressed;
            if (edge && tp.home) return IN_HOME;
        }
    }
}

static void frame(const char *title)
{
    gfx_clear(COLOR_WHITE);
    gfx_rect(2, 2, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, COLOR_BLACK);
    gfx_rect(3, 3, SCREEN_WIDTH - 6, SCREEN_HEIGHT - 6, COLOR_BLACK);
    gfx_draw_string(GFX_CENTER_X(title), 20, title, COLOR_BLACK, COLOR_WHITE);
    gfx_hline(20, 62, SCREEN_WIDTH - 40, COLOR_BLACK);
}

static void draw_lines(int y, const char *const *lines, int count)
{
    for (int i = 0; i < count; i++) {
        gfx_draw_string(GFX_CENTER_X(lines[i]), y + i * 40, lines[i], COLOR_BLACK, COLOR_WHITE);
    }
}

/* Full-screen message, any key to go on. */
static void show_message(const char *const *lines, int count)
{
    frame("Export accounts");
    draw_lines(160, lines, count);
    const char *hint = "Any key: back to the menu";
    gfx_draw_string(GFX_CENTER_X(hint), SCREEN_HEIGHT - 56, hint, COLOR_BLACK, COLOR_WHITE);
    gfx_flush();
    wait_input();
}

/* PIN entry: one digit at a time. Digits already entered are masked. */
static bool enter_pin(char pin[MSF_PIN_LEN + 1])
{
    int pos = 0, digit = 0;
    bool first = true;
    memset(pin, 0, MSF_PIN_LEN + 1);
    while (pos < MSF_PIN_LEN) {
        frame("Export accounts");
        static const char *const kText[] = {"Enter the device PIN", "to export the accounts"};
        draw_lines(110, kText, 2);
        char row[MSF_PIN_LEN * 2 + 1];
        int p = 0;
        for (int i = 0; i < MSF_PIN_LEN; i++) {
            row[p++] = i < pos ? '*' : i == pos ? (char)('0' + digit) : '_';
            if (i + 1 < MSF_PIN_LEN) row[p++] = ' ';
        }
        row[p] = '\0';
        int rx = GFX_CENTER_X(row);
        gfx_draw_string(rx, 260, row, COLOR_BLACK, COLOR_WHITE);
        /* Box around the digit being chosen. */
        gfx_rect(rx + pos * 2 * GFX_FONT_W - 6, 252, GFX_FONT_W + 12, GFX_FONT_H + 16, COLOR_BLACK);
        static const char *const kHelp[] = {"Left/Right: change digit", "Home: next digit", "Power: cancel"};
        draw_lines(380, kHelp, 3);
        if (first) gfx_flush();
        else gfx_flush_fast();
        first = false;

        switch (wait_input()) {
        case IN_LEFT: digit = (digit + 9) % 10; break;
        case IN_RIGHT: digit = (digit + 1) % 10; break;
        case IN_HOME:
            pin[pos++] = (char)('0' + digit);
            digit = 0;
            break;
        case IN_POWER:
            memset(pin, 0, MSF_PIN_LEN + 1);
            return false;
        default: break;
        }
    }
    return true;
}

static void busy(const char *text)
{
    frame("Export accounts");
    const char *const lines[] = {text};
    draw_lines(260, lines, 1);
    gfx_flush_fast();
}

/* First free RESCUEnn.MSR (never overwrites). */
/* The PIN check runs Argon2id + BLAKE2b, which need several KiB of stack:
 * far more than the factory's main task has (3.5 KiB, a stack overflow that
 * corrupted the heap, crash found on hardware 2026-10-12). Same pattern as
 * the app's PIN screen: a worker task with its own 12 KiB stack. */
typedef struct {
    char pin[MSF_PIN_LEN + 1];
    msf_account_t *recs;
    int count;
    esp_err_t err;
    volatile bool done;
} unlock_job_t;

static void unlock_task(void *arg)
{
    unlock_job_t *job = arg;
    job->err = msf_store_unlock(job->pin, job->recs, MSF_STORE_MAX_ACCOUNTS, &job->count);
    memset(job->pin, 0, sizeof(job->pin));
    job->done = true;
    vTaskDelete(NULL);
}

static esp_err_t unlock_in_task(const char *pin, msf_account_t *recs, int *count)
{
    static unlock_job_t job;
    memset(&job, 0, sizeof(job));
    strncpy(job.pin, pin, MSF_PIN_LEN);
    job.recs = recs;
    if (xTaskCreate(unlock_task, "rescue_pin", 12288, &job, 5, NULL) != pdPASS) {
        memset(&job, 0, sizeof(job));
        return ESP_ERR_NO_MEM;
    }
    while (!job.done) vTaskDelay(pdMS_TO_TICKS(50));
    *count = job.count;
    return job.err;
}

static bool free_name(char *path, size_t size, char *name, size_t name_size)
{
    struct stat st;
    for (int n = 1; n <= 99; n++) {
        snprintf(name, name_size, "RESCUE%02d.MSR", n);
        snprintf(path, size, RESCUE_DIR "/%s", name);
        if (stat(path, &st) != 0) return true;
    }
    return false;
}

static bool write_image(const uint8_t *img, size_t len, char *name, size_t name_size)
{
    mkdir(RESCUE_DIR, 0777);
    char path[48];
    if (!free_name(path, sizeof(path), name, name_size)) return false;
    unlink(RESCUE_TMP);
    FILE *f = fopen(RESCUE_TMP, "wb");
    if (!f) return false;
    bool ok = fwrite(img, 1, len, f) == len;
    ok = (fflush(f) == 0) && ok;
    ok = (fsync(fileno(f)) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    if (ok) ok = rename(RESCUE_TMP, path) == 0;
    if (!ok) unlink(RESCUE_TMP);
    return ok;
}

bool rescue_export_run(bool touch_ok, char *msg, size_t msg_size, bool *ok)
{
    s_touch_ok = touch_ok;
    s_touch_was_pressed = true;   /* the Home tap that opened us must be released first */
    *ok = false;

    if (msf_store_init() != ESP_OK || msf_store_state() == MSF_STORE_ERROR) {
        snprintf(msg, msg_size, "Store unreadable!");
        return true;
    }
    if (msf_store_state() == MSF_STORE_BLANK) {
        snprintf(msg, msg_size, "No account store");
        return true;
    }
    if (sdcard_mount() != ESP_OK) {
        snprintf(msg, msg_size, "No SD card!");
        return true;
    }
    sdcard_unmount();   /* mounted again to write, after the PIN */

    uint32_t wait = msf_store_backoff_remaining_s();
    if (wait) {
        snprintf(msg, msg_size, "Too many tries: wait %lus", (unsigned long)wait);
        return true;
    }

    char pin[MSF_PIN_LEN + 1];
    if (!enter_pin(pin)) return false;

    busy("Checking the PIN...");
    msf_account_t *recs = heap_caps_calloc(MSF_STORE_MAX_ACCOUNTS, sizeof(msf_account_t), MALLOC_CAP_SPIRAM);
    int count = 0;
    esp_err_t err = recs ? unlock_in_task(pin, recs, &count) : ESP_ERR_NO_MEM;
    memset(pin, 0, sizeof(pin));
    if (recs) {
        memset(recs, 0, MSF_STORE_MAX_ACCOUNTS * sizeof(msf_account_t));   /* only the check was needed */
        heap_caps_free(recs);
    }
    if (err == ESP_ERR_INVALID_RESPONSE) {
        snprintf(msg, msg_size, "Wrong PIN (%lu)", (unsigned long)msf_store_fail_count());
        return true;
    }
    if (err == ESP_ERR_TIMEOUT) {
        snprintf(msg, msg_size, "Too many tries: wait %lus", (unsigned long)msf_store_backoff_remaining_s());
        return true;
    }
    if (err != ESP_OK) {
        snprintf(msg, msg_size, "PIN check failed!");
        return true;
    }

    busy("Writing to the SD card...");
    uint8_t *img = NULL;
    size_t len = 0;
    err = msf_store_rescue_image(&img, &len);
    msf_store_lock();
    char name[16] = "";
    bool written = false;
    if (err == ESP_OK && sdcard_mount() == ESP_OK) {
        written = write_image(img, len, name, sizeof(name));
        sdcard_unmount();
    }
    msf_backup_free(img);   /* ciphertext only */
    if (!written) {
        snprintf(msg, msg_size, "SD write failed!");
        return true;
    }
    ESP_LOGI(TAG, "rescue export: %d account(s), %u bytes -> MSF/%s", count, (unsigned)len, name);

    char line1[32], line2[32];
    snprintf(line1, sizeof(line1), "%d account(s) saved to", count);
    snprintf(line2, sizeof(line2), "MSF/%s", name);
    const char *const lines[] = {line1, line2, "", "Protected by the PIN only:", "decrypt it on the PC with", "msf_backup.py, then",
                                 "DELETE it from the card."};
    show_message(lines, 7);
    snprintf(msg, msg_size, "Saved MSF/%s", name);
    *ok = true;
    return true;
}
