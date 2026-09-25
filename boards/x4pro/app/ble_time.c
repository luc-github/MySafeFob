/*
 Project: MySafeFob  ble_time.c
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
 * @file ble_time.c
 * @brief MySafeFob App — BLE Current Time Service client, see ble_time.h.
 *
 * Current Time (0x2A2B), 10 bytes: year u16 LE, month, day, hours, minutes,
 * seconds, day of week, fractions256, adjust reason. It is the peer's LOCAL
 * time. Local Time Information (0x2A0F), 2 bytes: time zone (int8, 15 min
 * units) and DST offset (uint8, 0/2/4/8 = +0/0.5/1/2 h). UTC = local - tz -
 * dst. If the peer has no 0x2A0F, the time zone configured on this device
 * (Time > Manual) is used instead.
 */
#include "ble_time.h"
#include "time_service.h"
#include "settings_store.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "app_log_workaround.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "ble_time"

/* Step-by-step BLE tracing (raw advertising packets, every device seen,
 * GATT discovery, reads). Off by default; set to 1 to debug a new phone or an
 * iPhone/iPad. Errors and warnings are always logged. */
#define BLE_TIME_VERBOSE 0
#if BLE_TIME_VERBOSE
#define BLE_LOGI(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#else
#define BLE_LOGI(fmt, ...) do { } while (0)
#endif

#define BIT_SYNCED BIT0
#define BIT_DISC_DONE BIT1
#define BIT_OP_DONE BIT2
#define BIT_DISCONNECTED BIT3

#define kScanMs 5000
#define kConnectTimeoutMs 8000
#define kOpTimeoutMs 15000
#define kRawMax 16

#define UUID_CTS 0x1805
#define UUID_CURRENT_TIME 0x2A2B
#define UUID_LOCAL_TIME_INFO 0x2A0F

typedef struct {
    ble_addr_t addr;
    ble_time_device_t info;
    bool connectable;
} raw_dev_t;

static _Atomic int s_state = BLE_TIME_IDLE;
static atomic_bool s_busy;
static char s_message[64] = "";

static ble_time_device_t s_devs[BLE_TIME_MAX_DEVICES];
static ble_addr_t s_dev_addrs[BLE_TIME_MAX_DEVICES];
static int s_dev_count;
static int s_target;

static EventGroupHandle_t s_evt;
static uint8_t s_own_addr_type;
static raw_dev_t *s_raw;
static int s_raw_count;

/* Connection/read state (all touched from the NimBLE host task callbacks). */
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_svc_start, s_svc_end;
static uint16_t s_ct_handle, s_lti_handle;
static bool s_time_read;
static bool s_time_ok;
static int32_t s_delta_s;
static bool s_delta_valid;
static const char *s_fail;

static void set_state(ble_time_state_t st, const char *msg)
{
    strlcpy(s_message, msg ? msg : "", sizeof(s_message));
    atomic_store_explicit(&s_state, st, memory_order_release);
}

ble_time_state_t ble_time_get_state(void)
{
    return (ble_time_state_t)atomic_load_explicit(&s_state, memory_order_acquire);
}

const char *ble_time_get_message(void)
{
    return s_message;
}

int ble_time_get_device_count(void)
{
    return s_dev_count;
}

bool ble_time_get_device(int index, ble_time_device_t *out)
{
    if (index < 0 || index >= s_dev_count) return false;
    *out = s_devs[index];
    return true;
}

/* ---- NimBLE bring-up / tear-down --------------------------------------- */

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset, reason %d", reason);
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) == 0 && ble_hs_id_infer_auto(0, &s_own_addr_type) == 0) {
        BLE_LOGI("host synced, own address type %d", s_own_addr_type);
        xEventGroupSetBits(s_evt, BIT_SYNCED);
    } else {
        ESP_LOGE(TAG, "host synced but no usable own address");
    }
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* esp3d_log silences every IDF log ("*" = NONE) at boot, which also hides the
 * NimBLE/controller error messages. Let warnings and errors through while the
 * BLE stack is up, then put the previous level back. */
static esp_log_level_t s_prev_log_level;

static void bt_logs_on(void)
{
    s_prev_log_level = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_WARN);
}

static void bt_logs_off(void)
{
    esp_log_level_set("*", s_prev_log_level);
}

static bool ble_start(void)
{
    bt_logs_on();
    s_evt = xEventGroupCreate();
    if (!s_evt) return false;
    esp_err_t init_err = nimble_port_init();
    if (init_err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(init_err));
        ESP_LOGE(TAG, "free internal heap %u, largest block %u, free PSRAM %u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        vEventGroupDelete(s_evt);
        s_evt = NULL;
        bt_logs_off();
        return false;
    }
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    if (!(xEventGroupWaitBits(s_evt, BIT_SYNCED, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000)) & BIT_SYNCED)) {
        ESP_LOGE(TAG, "host did not sync within 5 s");
        return false; /* caller still runs ble_stop() */
    }
    return true;
}

static void ble_stop(void)
{
    int rc = nimble_port_stop();
    if (rc == 0) nimble_port_deinit();
    else ESP_LOGE(TAG, "nimble_port_stop failed: %d", rc);
    BLE_LOGI("BLE stack stopped");
    bt_logs_off();
    if (s_evt) {
        vEventGroupDelete(s_evt);
        s_evt = NULL;
    }
}

/* ---- Scan -------------------------------------------------------------- */

static void addr_to_str(const ble_addr_t *a, char *out, size_t size)
{
    snprintf(out, size, "%02X:%02X:%02X:%02X:%02X:%02X", a->val[5], a->val[4], a->val[3], a->val[2], a->val[1],
             a->val[0]);
}

static int disc_cb(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    if (ev->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        xEventGroupSetBits(s_evt, BIT_DISC_DONE);
        return 0;
    }
    if (ev->type != BLE_GAP_EVENT_DISC) return 0;

    const struct ble_gap_disc_desc *d = &ev->disc;
    raw_dev_t *r = NULL;
    for (int i = 0; i < s_raw_count; i++) {
        if (ble_addr_cmp(&s_raw[i].addr, &d->addr) == 0) {
            r = &s_raw[i];
            break;
        }
    }
    if (!r) {
        if (s_raw_count >= kRawMax) return 0;
        r = &s_raw[s_raw_count++];
        memset(r, 0, sizeof(*r));
        r->addr = d->addr;
        addr_to_str(&d->addr, r->info.addr, sizeof(r->info.addr));
    }
    r->info.rssi = d->rssi;
    if (d->event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND || d->event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
        r->connectable = true;
    }
#if BLE_TIME_VERBOSE
    {
        /* Raw view of every advertising/scan-response packet (duplicates are
         * already filtered by the controller): what the phone really sends. */
        char hex[3 * 31 + 1];
        int n = d->length_data < 31 ? d->length_data : 31;
        for (int i = 0; i < n; i++) snprintf(hex + 3 * i, 4, "%02X ", d->data[i]);
        hex[3 * n] = '\0';
        BLE_LOGI("adv %s evtype %d addrtype %d rssi %d len %d: %s", r->info.addr, d->event_type, d->addr.type,
                 d->rssi, d->length_data, hex);
    }
#endif
    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) == 0) {
        if (f.name && f.name_len > 0) {
            size_t n = f.name_len < sizeof(r->info.name) - 1 ? f.name_len : sizeof(r->info.name) - 1;
            memcpy(r->info.name, f.name, n);
            r->info.name[n] = '\0';
        }
        for (int i = 0; i < f.num_uuids16; i++) {
            if (f.uuids16[i].value == UUID_CTS) r->info.has_cts = true;
        }
    }
    return 0;
}

static int cmp_dev(const void *a, const void *b)
{
    const raw_dev_t *x = a, *y = b;
    if (x->info.has_cts != y->info.has_cts) return y->info.has_cts - x->info.has_cts;
    return y->info.rssi - x->info.rssi;
}

static void scan_task(void *arg)
{
    (void)arg;
    const char *fail = NULL;
    s_raw = calloc(kRawMax, sizeof(raw_dev_t));
    s_raw_count = 0;

    if (!s_raw || !ble_start()) {
        fail = "Bluetooth start failed";
    } else {
        struct ble_gap_disc_params dp = {0}; /* active scan: also fetches names from scan responses */
        dp.filter_duplicates = 1;
        int rc = ble_gap_disc(s_own_addr_type, kScanMs, &dp, disc_cb, NULL);
        if (rc != 0) {
            ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
            fail = "Scan failed";
        } else {
            BLE_LOGI("scanning for %d ms", kScanMs);
            xEventGroupWaitBits(s_evt, BIT_DISC_DONE, pdFALSE, pdFALSE, pdMS_TO_TICKS(kScanMs + 3000));
        }
    }
    if (s_evt) ble_stop();

    if (!fail) {
#if BLE_TIME_VERBOSE
        for (int i = 0; i < s_raw_count; i++) {
            BLE_LOGI("seen %d: %s \"%s\" rssi %d%s%s", i, s_raw[i].info.addr, s_raw[i].info.name,
                     s_raw[i].info.rssi, s_raw[i].info.has_cts ? " [CTS]" : "", s_raw[i].connectable ? "" : " (not connectable)");
        }
#endif
        /* Connectable devices only, CTS advertisers first, then by signal. */
        int n = 0;
        for (int i = 0; i < s_raw_count; i++) {
            if (s_raw[i].connectable) s_raw[n++] = s_raw[i];
        }
        qsort(s_raw, n, sizeof(raw_dev_t), cmp_dev);
        s_dev_count = n < BLE_TIME_MAX_DEVICES ? n : BLE_TIME_MAX_DEVICES;
        for (int i = 0; i < s_dev_count; i++) {
            s_devs[i] = s_raw[i].info;
            s_dev_addrs[i] = s_raw[i].addr;
            BLE_LOGI("device %d: %s \"%s\" rssi %d%s", i, s_devs[i].addr, s_devs[i].name, s_devs[i].rssi,
                     s_devs[i].has_cts ? " [CTS]" : "");
        }
        BLE_LOGI("scan done: %d seen, %d connectable", s_raw_count, n);
    }
    free(s_raw);
    s_raw = NULL;
    if (fail) set_state(BLE_TIME_FAILED, fail);
    else set_state(BLE_TIME_SCAN_DONE, s_dev_count ? "Select a device" : "No device found");
    atomic_store(&s_busy, false);
    vTaskDelete(NULL);
}

/* ---- Connect + read Current Time -------------------------------------- */

static void finish_op(const char *fail)
{
    if (fail && !s_fail) s_fail = fail;
    if (fail) ESP_LOGW(TAG, "operation failed: %s", fail);
    xEventGroupSetBits(s_evt, BIT_OP_DONE);
    if (s_conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
}

static bool parse_current_time(const uint8_t *b, int len, time_t *local_sec, int *usec)
{
    if (len < 7) return false;
    time_service_dt_t dt = {.year = b[0] | (b[1] << 8), .month = b[2], .day = b[3], .hour = b[4], .minute = b[5], .second = b[6]};
    if (!time_service_dt_is_valid(&dt)) return false;
    *local_sec = time_service_dt_to_epoch(&dt);
    *usec = len >= 9 ? (int)((int64_t)b[8] * 1000000 / 256) : 0;
    return true;
}

static void apply_time(time_t local_sec, int usec, bool has_lti, int tz_units, int dst_units)
{
    int offset_s;
    if (has_lti) {
        offset_s = tz_units * 900 + (dst_units == 255 ? 0 : dst_units * 900);
        BLE_LOGI("peer local time info: tz %d x15min, dst %d x15min", tz_units, dst_units);
    } else {
        offset_s = settings_store_get_time_tz_offset_min() * 60;
        ESP_LOGW(TAG, "peer has no Local Time Information, using the configured time zone (%d s)", offset_s);
    }
    time_service_set_system_precise(local_sec - offset_s, usec, &s_delta_s, &s_delta_valid);
    s_time_ok = true;
}

static time_t s_local_sec;
static int s_local_usec;

static int read_cb(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)conn;
    if (err->status != 0 || !attr) return 0; /* the completion call carries no data */
    int which = (int)(intptr_t)arg; /* 0 = Current Time, 1 = Local Time Information */
    uint8_t buf[16];
    uint16_t len = OS_MBUF_PKTLEN(attr->om);
    if (len > sizeof(buf)) len = sizeof(buf);
    os_mbuf_copydata(attr->om, 0, len, buf);

    BLE_LOGI("read %s (%d bytes)", which == 0 ? "Current Time" : "Local Time Info", len);
#if BLE_TIME_VERBOSE
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, len, ESP_LOG_INFO);
#endif
    if (which == 0) {
        if (!parse_current_time(buf, len, &s_local_sec, &s_local_usec)) {
            finish_op("Invalid time from device");
            return 0;
        }
        s_time_read = true;
        if (s_lti_handle) {
            ble_gattc_read(s_conn, s_lti_handle, read_cb, (void *)1);
        } else {
            apply_time(s_local_sec, s_local_usec, false, 0, 0);
            finish_op(NULL);
        }
    } else {
        if (len >= 2) apply_time(s_local_sec, s_local_usec, true, (int8_t)buf[0], buf[1]);
        else apply_time(s_local_sec, s_local_usec, false, 0, 0);
        finish_op(NULL);
    }
    return 0;
}

static int chr_cb(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (err->status == 0 && chr) {
        BLE_LOGI("characteristic 0x%04X, value handle %u", ble_uuid_u16(&chr->uuid.u), chr->val_handle);
        if (ble_uuid_u16(&chr->uuid.u) == UUID_CURRENT_TIME) s_ct_handle = chr->val_handle;
        else if (ble_uuid_u16(&chr->uuid.u) == UUID_LOCAL_TIME_INFO) s_lti_handle = chr->val_handle;
    } else if (err->status == BLE_HS_EDONE) {
        if (!s_ct_handle) {
            finish_op("No Current Time characteristic");
        } else if (ble_gattc_read(conn, s_ct_handle, read_cb, (void *)0) != 0) {
            finish_op("Read failed");
        } else {
            set_state(BLE_TIME_READING, "Reading time...");
        }
    } else {
        finish_op("Discovery failed");
    }
    return 0;
}

static int svc_cb(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;
    if (err->status == 0 && svc) {
        BLE_LOGI("time service found, handles %u-%u", svc->start_handle, svc->end_handle);
        s_svc_start = svc->start_handle;
        s_svc_end = svc->end_handle;
    } else if (err->status == BLE_HS_EDONE) {
        if (!s_svc_start) {
            finish_op("No time service on this device");
        } else if (ble_gattc_disc_all_chrs(conn, s_svc_start, s_svc_end, chr_cb, NULL) != 0) {
            finish_op("Discovery failed");
        }
    } else {
        finish_op("Discovery failed");
    }
    return 0;
}

static int gap_cb(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        BLE_LOGI("connect event, status %d", ev->connect.status);
        if (ev->connect.status == 0) {
            s_conn = ev->connect.conn_handle;
            static const ble_uuid16_t kCts = BLE_UUID16_INIT(UUID_CTS);
            if (ble_gattc_disc_svc_by_uuid(s_conn, &kCts.u, svc_cb, NULL) != 0) finish_op("Discovery failed");
        } else {
            s_fail = "Connection failed";
            xEventGroupSetBits(s_evt, BIT_OP_DONE | BIT_DISCONNECTED);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        BLE_LOGI("disconnected, reason %d", ev->disconnect.reason);
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        if (!s_time_ok && !s_fail) s_fail = "Device disconnected";
        xEventGroupSetBits(s_evt, BIT_OP_DONE | BIT_DISCONNECTED);
        break;
    default:
        break;
    }
    return 0;
}

static void sync_task(void *arg)
{
    (void)arg;
    s_conn = BLE_HS_CONN_HANDLE_NONE;
    s_svc_start = s_svc_end = s_ct_handle = s_lti_handle = 0;
    s_time_read = s_time_ok = false;
    s_fail = NULL;

    if (!ble_start()) {
        s_fail = "Bluetooth start failed";
    } else {
        ble_addr_t peer = s_dev_addrs[s_target];
        BLE_LOGI("connecting to %s", s_devs[s_target].addr);
        if (ble_gap_connect(s_own_addr_type, &peer, kConnectTimeoutMs, NULL, gap_cb, NULL) != 0) {
            s_fail = "Connection failed";
        } else {
            EventBits_t bits = xEventGroupWaitBits(s_evt, BIT_OP_DONE, pdFALSE, pdFALSE, pdMS_TO_TICKS(kOpTimeoutMs));
            if (!(bits & BIT_OP_DONE)) {
                s_fail = "Timeout";
                ble_gap_conn_cancel();
            }
            if (s_conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
            xEventGroupWaitBits(s_evt, BIT_DISCONNECTED, pdFALSE, pdFALSE, pdMS_TO_TICKS(3000));
        }
    }
    if (s_evt) ble_stop();
    BLE_LOGI("sync result: %s", s_time_ok ? "OK" : (s_fail ? s_fail : "failed"));

    if (s_time_ok) {
        /* The system clock was set at the instant the time was read; only the
         * (slow, aligned) RTC write and the sync record are left. */
        bool rtc_ok = time_service_commit_system_time(s_delta_s, s_delta_valid, TIME_SOURCE_BLE);
        set_state(BLE_TIME_SYNC_OK, rtc_ok ? "Time synchronized" : "Synced (RTC write failed)");
    } else {
        set_state(BLE_TIME_FAILED, s_fail ? s_fail : "Sync failed");
    }
    atomic_store(&s_busy, false);
    vTaskDelete(NULL);
}

bool ble_time_scan_start(void)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_busy, &expected, true)) return false;
    set_state(BLE_TIME_SCANNING, "Scanning...");
    if (xTaskCreate(scan_task, "ble_scan", 6144, NULL, 4, NULL) != pdPASS) {
        set_state(BLE_TIME_FAILED, "Out of memory");
        atomic_store(&s_busy, false);
        return false;
    }
    return true;
}

bool ble_time_sync_start(int index)
{
    if (index < 0 || index >= s_dev_count) return false;
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_busy, &expected, true)) return false;
    s_target = index;
    set_state(BLE_TIME_CONNECTING, "Connecting...");
    if (xTaskCreate(sync_task, "ble_sync", 8192, NULL, 4, NULL) != pdPASS) {
        set_state(BLE_TIME_FAILED, "Out of memory");
        atomic_store(&s_busy, false);
        return false;
    }
    return true;
}
