/*
 Project: MySafeFob  wifi_time.c
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
 * @file wifi_time.c
 * @brief MySafeFob App — Wi-Fi scan + SNTP time sync, see wifi_time.h.
 */
#include "wifi_time.h"
#include "time_service.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "app_log_workaround.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define TAG "wifi_time"

#define BIT_GOT_IP BIT0
#define BIT_FAIL BIT1

#define kScanMaxRecords 20
#define kConnectTimeoutMs 15000
#define kSntpTimeoutMs 10000
#define kMaxAssocRetries 2

static _Atomic int s_state = WIFI_TIME_IDLE;
static atomic_bool s_busy;
static wifi_time_ap_t s_aps[WIFI_TIME_MAX_APS];
static int s_ap_count;
static char s_message[64] = "";

static char s_ssid[33];
static char s_password[65];

static EventGroupHandle_t s_evt;
static esp_netif_t *s_netif;
static int s_retries;
static int s_fail_reason;

/* Clock error at the exact instant SNTP sets the time. The system clock is
 * read (with a monotonic timestamp) just before SNTP starts; when the sync
 * callback fires, the expected clock is that reading plus the monotonic time
 * elapsed. Comparing the SNTP time with it excludes the connect/DNS/round-trip
 * time, which a naive "clock before vs after" would wrongly count as drift. */
static int64_t s_old_us;
static int64_t s_old_mono_us;
static bool s_old_valid;
static int32_t s_delta_s;

static void sntp_synced_cb(struct timeval *tv)
{
    int64_t expected_us = s_old_us + (esp_timer_get_time() - s_old_mono_us);
    int64_t actual_us = (int64_t)tv->tv_sec * 1000000 + tv->tv_usec;
    int64_t d_us = actual_us - expected_us;
    int64_t d_s = (d_us + (d_us >= 0 ? 500000 : -500000)) / 1000000;
    if (d_s > INT32_MAX - 1) d_s = INT32_MAX - 1;
    if (d_s < INT32_MIN + 1) d_s = INT32_MIN + 1;
    s_delta_s = (int32_t)d_s;
    ESP_LOGI(TAG, "SNTP set the clock, error was %lld ms", (long long)(d_us / 1000));
}

/* The message is written before the state is published (release/acquire),
 * so the UI task never reads a half-updated pair. */
static void set_state(wifi_time_state_t st, const char *msg)
{
    strlcpy(s_message, msg ? msg : "", sizeof(s_message));
    atomic_store_explicit(&s_state, st, memory_order_release);
}

wifi_time_state_t wifi_time_get_state(void)
{
    return (wifi_time_state_t)atomic_load_explicit(&s_state, memory_order_acquire);
}

const char *wifi_time_get_message(void)
{
    return s_message;
}

int wifi_time_get_ap_count(void)
{
    return s_ap_count;
}

bool wifi_time_get_ap(int index, wifi_time_ap_t *out)
{
    if (index < 0 || index >= s_ap_count) return false;
    *out = s_aps[index];
    return true;
}

static bool is_auth_failure(int reason)
{
    return reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
           reason == WIFI_REASON_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_MIC_FAILURE;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)data;
        s_fail_reason = ev->reason;
        if (!is_auth_failure(ev->reason) && s_retries < kMaxAssocRetries) {
            s_retries++;
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_evt, BIT_FAIL);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_evt, BIT_GOT_IP);
    }
}

static esp_err_t radio_start(void)
{
    s_evt = xEventGroupCreate();
    if (!s_evt) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    s_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);
    return esp_wifi_start();
}

static void radio_stop(void)
{
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event);
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_netif) {
        esp_netif_destroy_default_wifi(s_netif);
        s_netif = NULL;
    }
    if (s_evt) {
        vEventGroupDelete(s_evt);
        s_evt = NULL;
    }
}

static int cmp_rssi_desc(const void *a, const void *b)
{
    return ((const wifi_ap_record_t *)b)->rssi - ((const wifi_ap_record_t *)a)->rssi;
}

static void scan_task(void *arg)
{
    (void)arg;
    const char *fail = NULL;
    wifi_ap_record_t *recs = calloc(kScanMaxRecords, sizeof(*recs));
    uint16_t n = kScanMaxRecords;

    if (!recs || radio_start() != ESP_OK) {
        fail = "Wi-Fi start failed";
    } else {
        wifi_scan_config_t scan_cfg = {0};
        if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK || esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) {
            fail = "Scan failed";
        }
    }

    if (!fail) {
        qsort(recs, n, sizeof(*recs), cmp_rssi_desc);
        s_ap_count = 0;
        for (int i = 0; i < n && s_ap_count < WIFI_TIME_MAX_APS; i++) {
            const char *ssid = (const char *)recs[i].ssid;
            if (!ssid[0]) continue;
            bool dup = false;
            for (int j = 0; j < s_ap_count; j++) {
                if (strcmp(s_aps[j].ssid, ssid) == 0) dup = true;
            }
            if (dup) continue;
            strlcpy(s_aps[s_ap_count].ssid, ssid, sizeof(s_aps[0].ssid));
            s_aps[s_ap_count].rssi = recs[i].rssi;
            s_aps[s_ap_count].secure = recs[i].authmode != WIFI_AUTH_OPEN;
            s_ap_count++;
        }
    }
    radio_stop();
    free(recs);
    if (fail) {
        set_state(WIFI_TIME_FAILED, fail);
    } else {
        set_state(WIFI_TIME_SCAN_DONE, s_ap_count ? "Select a network" : "No network found");
    }
    atomic_store(&s_busy, false);
    vTaskDelete(NULL);
}

static void sync_task(void *arg)
{
    (void)arg;
    const char *fail = NULL;
    bool sntp_started = false;

    if (radio_start() != ESP_OK) {
        fail = "Wi-Fi start failed";
        goto cleanup;
    }
    s_retries = 0;
    s_fail_reason = 0;
    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, s_ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, s_password, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN; /* accept whatever the AP uses */
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    memset(&wc, 0, sizeof(wc));
    memset(s_password, 0, sizeof(s_password));
    esp_wifi_connect();

    EventBits_t bits =
        xEventGroupWaitBits(s_evt, BIT_GOT_IP | BIT_FAIL, pdTRUE, pdFALSE, pdMS_TO_TICKS(kConnectTimeoutMs));
    if (!(bits & BIT_GOT_IP)) {
        ESP_LOGW(TAG, "connect failed (reason %d)", s_fail_reason);
        fail = ((bits & BIT_FAIL) && is_auth_failure(s_fail_reason)) ? "Wrong password" : "Connection failed";
        goto cleanup;
    }

    s_old_valid = time_service_is_valid();
    struct timeval now_tv;
    gettimeofday(&now_tv, NULL);
    s_old_us = (int64_t)now_tv.tv_sec * 1000000 + now_tv.tv_usec;
    s_old_mono_us = esp_timer_get_time();
    s_delta_s = 0;
    set_state(WIFI_TIME_SYNCING, "Syncing time...");
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp_cfg.sync_cb = sntp_synced_cb;
    if (esp_netif_sntp_init(&sntp_cfg) != ESP_OK) {
        fail = "SNTP start failed";
        goto cleanup;
    }
    sntp_started = true;
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(kSntpTimeoutMs)) != ESP_OK) {
        fail = "Time server timeout";
        goto cleanup;
    }
    if (!time_service_is_valid()) {
        fail = "Invalid time received";
        goto cleanup;
    }
    bool rtc_ok = time_service_commit_system_time(s_delta_s, s_old_valid, TIME_SOURCE_WIFI);
    set_state(WIFI_TIME_SYNC_OK, rtc_ok ? "Time synchronized" : "Synced (RTC write failed)");

cleanup:
    if (sntp_started) esp_netif_sntp_deinit();
    radio_stop();
    memset(s_password, 0, sizeof(s_password));
    if (fail) set_state(WIFI_TIME_FAILED, fail);
    atomic_store(&s_busy, false);
    vTaskDelete(NULL);
}

bool wifi_time_scan_start(void)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_busy, &expected, true)) return false;
    set_state(WIFI_TIME_SCANNING, "Scanning...");
    if (xTaskCreate(scan_task, "wifi_scan", 6144, NULL, 4, NULL) != pdPASS) {
        set_state(WIFI_TIME_FAILED, "Out of memory");
        atomic_store(&s_busy, false);
        return false;
    }
    return true;
}

bool wifi_time_sync_start(const char *ssid, const char *password)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_busy, &expected, true)) return false;
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    strlcpy(s_password, password ? password : "", sizeof(s_password));
    set_state(WIFI_TIME_CONNECTING, "Connecting...");
    if (xTaskCreate(sync_task, "wifi_sync", 8192, NULL, 4, NULL) != pdPASS) {
        set_state(WIFI_TIME_FAILED, "Out of memory");
        atomic_store(&s_busy, false);
        return false;
    }
    return true;
}
