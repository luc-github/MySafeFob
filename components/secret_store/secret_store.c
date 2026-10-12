/*
 Project: MySafeFob  secret_store.c
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
 * @file secret_store.c
 * @brief MySafeFob — encrypted account store, see secret_store.h and
 *        INTERFACES.md §1 for the on-flash format.
 *
 * Partition layout (`secrets`):
 *   0x00000  control sector (plaintext): "MSFC" | version | fail_count | crc
 *   0x10000  slot 0 (128 KiB): header | ciphertext | tag
 *   0x30000  slot 1 (128 KiB)
 * A save goes to the slot that does not hold the current blob, body first,
 * header (with its magic) last: until the header is written the new slot is
 * invalid and the previous one is still used.
 */
#include "secret_store.h"

#include <string.h>

#include "bootloader_random.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/gcm.h"
#include "monocypher.h"

static const char *TAG = "secret_store";

#define CONTROL_OFF 0x00000
#define SLOT_SIZE 0x20000
static const uint32_t kSlotOff[2] = {0x10000, 0x30000};

#define KDF_ALGO_ARGON2ID 2
#define KDF_M_MIB 2       /* ADR-004 decision 2026-10-10 */
#define KDF_T 1
#define KDF_P 1
#define KEY_LEN 32
#define SALT_LEN 16
#define NONCE_LEN 12
#define TAG_LEN 16
#define FORMAT_VERSION 1

/* Slot header, authenticated as GCM additional data. Little-endian. */
typedef struct __attribute__((packed)) {
    char magic[4];         /* "MSF1" */
    uint8_t format_version;
    uint8_t kdf_algo;
    uint8_t kdf_t;
    uint8_t kdf_p;
    uint16_t kdf_m_mib;
    uint8_t salt[SALT_LEN];
    uint8_t nonce[NONCE_LEN];
    uint32_t seq;          /* higher = newer */
    uint32_t ct_len;
} slot_header_t;

#define MAX_CT_LEN (SLOT_SIZE - sizeof(slot_header_t) - TAG_LEN)

typedef struct __attribute__((packed)) {
    char magic[4];         /* "MSFC" */
    uint8_t version;
    uint8_t reserved[3];
    uint32_t fail_count;
    uint32_t crc;          /* CRC32 of the bytes above */
} control_t;

static const esp_partition_t *s_part;
static SemaphoreHandle_t s_lock;
static msf_store_state_t s_state = MSF_STORE_ERROR;
static slot_header_t s_hdr[2];
static bool s_hdr_valid[2];
static int s_current = -1;             /* slot holding the newest blob */
static uint8_t s_key[KEY_LEN];         /* valid while UNLOCKED */
static uint8_t s_salt[SALT_LEN];
static uint16_t s_kdf_m_mib;
static uint8_t s_kdf_t;
static uint32_t s_fail_count;
static int64_t s_last_fail_us;         /* back-off start: last failure, or boot */

/* ---- helpers ------------------------------------------------------------ */

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

bool msf_store_pin_valid(const char *pin)
{
    if (!pin || strlen(pin) != MSF_PIN_LEN) return false;
    for (int i = 0; i < MSF_PIN_LEN; i++) {
        if (pin[i] < '0' || pin[i] > '9') return false;
    }
    return true;
}

static uint32_t backoff_s(uint32_t fails)
{
    static const uint32_t kDelays[] = {10, 30, 120, 600};   /* from the 3rd failure */
    if (fails < 3) return 0;
    uint32_t i = fails - 3;
    return i < sizeof(kDelays) / sizeof(kDelays[0]) ? kDelays[i] : 3600;
}

static esp_err_t write_control(uint32_t fail_count)
{
    control_t c = {.magic = {'M', 'S', 'F', 'C'}, .version = 1, .fail_count = fail_count};
    c.crc = esp_rom_crc32_le(0, (const uint8_t *)&c, offsetof(control_t, crc));
    esp_err_t err = esp_partition_erase_range(s_part, CONTROL_OFF, 0x1000);
    if (err == ESP_OK) err = esp_partition_write(s_part, CONTROL_OFF, &c, sizeof(c));
    if (err == ESP_OK) s_fail_count = fail_count;
    return err;
}

static void read_control(void)
{
    control_t c;
    s_fail_count = 0;
    if (esp_partition_read(s_part, CONTROL_OFF, &c, sizeof(c)) != ESP_OK) return;
    if (memcmp(c.magic, "MSFC", 4) != 0) return;
    if (esp_rom_crc32_le(0, (const uint8_t *)&c, offsetof(control_t, crc)) != c.crc) return;
    s_fail_count = c.fail_count;
}

static void read_headers(void)
{
    s_current = -1;
    for (int i = 0; i < 2; i++) {
        s_hdr_valid[i] = esp_partition_read(s_part, kSlotOff[i], &s_hdr[i], sizeof(s_hdr[i])) == ESP_OK &&
                         memcmp(s_hdr[i].magic, "MSF1", 4) == 0 && s_hdr[i].format_version == FORMAT_VERSION &&
                         s_hdr[i].kdf_algo == KDF_ALGO_ARGON2ID && s_hdr[i].ct_len <= MAX_CT_LEN &&
                         s_hdr[i].kdf_m_mib >= 1 && s_hdr[i].kdf_m_mib <= 6 && s_hdr[i].kdf_t >= 1;
        if (s_hdr_valid[i] && (s_current < 0 || s_hdr[i].seq > s_hdr[s_current].seq)) s_current = i;
    }
}

static esp_err_t derive_key(const char *pin, const uint8_t salt[SALT_LEN], uint16_t m_mib, uint8_t t,
                            uint8_t key[KEY_LEN])
{
    size_t work_size = (size_t)m_mib * 1024 * 1024;
    void *work = heap_caps_malloc(work_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!work) return ESP_ERR_NO_MEM;
    crypto_argon2_config cfg = {.algorithm = CRYPTO_ARGON2_ID, .nb_blocks = (uint32_t)m_mib * 1024,
                                .nb_passes = t, .nb_lanes = KDF_P};
    crypto_argon2_inputs in = {.pass = (const uint8_t *)pin, .salt = salt,
                               .pass_size = (uint32_t)strlen(pin), .salt_size = SALT_LEN};
    int64_t t0 = esp_timer_get_time();
    crypto_argon2(key, KEY_LEN, work, cfg, in, crypto_argon2_no_extras);
    ESP_LOGI(TAG, "key derived in %lld ms", (long long)((esp_timer_get_time() - t0) / 1000));
    crypto_wipe(work, work_size);
    heap_caps_free(work);
    return ESP_OK;
}

/* Salt from the hardware RNG with its entropy source on (radios are off). */
static void random_bytes(uint8_t *buf, size_t len)
{
    bootloader_random_enable();
    esp_fill_random(buf, len);
    bootloader_random_disable();
}

/* ---- record serialization (INTERFACES.md §1.2, ADR-019) ----------------
 * Plaintext = "MSFS" | version u8 | records. Each record = 0x30 | len u16 |
 * fields; each field = tag u8 | len u16 | bytes; empty fields are omitted,
 * unknown tags are skipped (forward compatible). Recovery codes: one 0x41
 * field per code, in order. */

enum {
    REC_ACCOUNT = 0x30,
    F_ID = 0x01, F_NAME = 0x02,
    F_TOTP_KEY = 0x10, F_DIGITS = 0x11, F_PERIOD = 0x12,
    F_USER = 0x20, F_PASSWORD = 0x21, F_PIN = 0x22, F_NOTE = 0x23,
    F_RCV_MASK = 0x40, F_RCV_CODE = 0x41,
};

typedef struct {
    uint8_t *buf;
    size_t size, pos;
    bool overflow;
} writer_t;

static void put(writer_t *w, const void *data, size_t len)
{
    if (w->overflow || w->pos + len > w->size) {
        w->overflow = true;
        return;
    }
    memcpy(w->buf + w->pos, data, len);
    w->pos += len;
}

static void put_tlv(writer_t *w, uint8_t tag, const void *data, size_t len)
{
    uint8_t h[3] = {tag, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
    put(w, h, 3);
    put(w, data, len);
}

static void put_str(writer_t *w, uint8_t tag, const char *s)
{
    size_t len = strlen(s);
    if (len) put_tlv(w, tag, s, len);
}

static size_t serialize(const msf_account_t *recs, int count, uint8_t *buf, size_t size)
{
    writer_t w = {.buf = buf, .size = size};
    put(&w, "MSFS", 4);
    uint8_t version = 1;
    put(&w, &version, 1);
    for (int i = 0; i < count; i++) {
        const msf_account_t *a = &recs[i];
        size_t start = w.pos;
        uint8_t h[3] = {REC_ACCOUNT, 0, 0};
        put(&w, h, 3);   /* length patched below */
        put_tlv(&w, F_ID, &a->id, 2);
        put_str(&w, F_NAME, a->name);
        if (a->totp_key[0]) {
            put_str(&w, F_TOTP_KEY, a->totp_key);
            put_tlv(&w, F_DIGITS, &a->digits, 1);
            put_tlv(&w, F_PERIOD, &a->period, 1);
        }
        put_str(&w, F_USER, a->username);
        put_str(&w, F_PASSWORD, a->password);
        put_str(&w, F_PIN, a->pin);
        put_str(&w, F_NOTE, a->note);
        if (a->rcv_count) {
            put_tlv(&w, F_RCV_MASK, &a->rcv_used_mask, 4);
            for (int c = 0; c < a->rcv_count && c < MSF_RCV_MAX; c++) put_str(&w, F_RCV_CODE, a->rcv_codes[c]);
        }
        if (w.overflow) return 0;
        size_t len = w.pos - start - 3;
        buf[start + 1] = (uint8_t)(len & 0xFF);
        buf[start + 2] = (uint8_t)(len >> 8);
    }
    return w.overflow ? 0 : w.pos;
}

static void get_str(char *dst, size_t dst_size, const uint8_t *src, size_t len)
{
    if (len > dst_size - 1) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* Returns the number of records, or -1 if the plaintext is malformed. */
static int deserialize(const uint8_t *buf, size_t len, msf_account_t *out, int max)
{
    if (len < 5 || memcmp(buf, "MSFS", 4) != 0 || buf[4] != 1) return -1;
    size_t pos = 5;
    int n = 0;
    while (pos + 3 <= len) {
        uint8_t tag = buf[pos];
        size_t rlen = buf[pos + 1] | (buf[pos + 2] << 8);
        pos += 3;
        if (pos + rlen > len) return -1;
        if (tag == REC_ACCOUNT && n < max) {
            msf_account_t *a = &out[n++];
            memset(a, 0, sizeof(*a));
            a->digits = 6;
            a->period = 30;
            size_t p = pos, end = pos + rlen;
            while (p + 3 <= end) {
                uint8_t ft = buf[p];
                size_t fl = buf[p + 1] | (buf[p + 2] << 8);
                p += 3;
                if (p + fl > end) return -1;
                const uint8_t *v = buf + p;
                switch (ft) {
                case F_ID: if (fl == 2) memcpy(&a->id, v, 2); break;
                case F_NAME: get_str(a->name, sizeof(a->name), v, fl); break;
                case F_TOTP_KEY: get_str(a->totp_key, sizeof(a->totp_key), v, fl); break;
                case F_DIGITS: if (fl == 1) a->digits = v[0]; break;
                case F_PERIOD: if (fl == 1) a->period = v[0]; break;
                case F_USER: get_str(a->username, sizeof(a->username), v, fl); break;
                case F_PASSWORD: get_str(a->password, sizeof(a->password), v, fl); break;
                case F_PIN: get_str(a->pin, sizeof(a->pin), v, fl); break;
                case F_NOTE: get_str(a->note, sizeof(a->note), v, fl); break;
                case F_RCV_MASK: if (fl == 4) memcpy(&a->rcv_used_mask, v, 4); break;
                case F_RCV_CODE:
                    if (a->rcv_count < MSF_RCV_MAX) {
                        get_str(a->rcv_codes[a->rcv_count], sizeof(a->rcv_codes[0]), v, fl);
                        a->rcv_count++;
                    }
                    break;
                default: break;   /* unknown field: skipped */
                }
                p += fl;
            }
        }
        pos += rlen;
    }
    return n;
}

/* ---- blob write / read -------------------------------------------------- */

/* Encrypts the records with s_key into the slot that is not current. */
static esp_err_t write_blob(const msf_account_t *recs, int count)
{
    uint8_t *plain = heap_caps_malloc(MAX_CT_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *cipher = heap_caps_malloc(MAX_CT_LEN + TAG_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_err_t err = ESP_ERR_NO_MEM;
    if (!plain || !cipher) goto out;

    size_t len = serialize(recs, count, plain, MAX_CT_LEN);
    err = ESP_ERR_INVALID_SIZE;
    if (len == 0) goto out;

    int slot = (s_current < 0) ? 0 : 1 - s_current;
    uint32_t seq = (s_current < 0) ? 1 : s_hdr[s_current].seq + 1;
    slot_header_t h = {.magic = {'M', 'S', 'F', '1'}, .format_version = FORMAT_VERSION,
                       .kdf_algo = KDF_ALGO_ARGON2ID, .kdf_t = s_kdf_t, .kdf_p = KDF_P,
                       .kdf_m_mib = s_kdf_m_mib, .seq = seq, .ct_len = (uint32_t)len};
    memcpy(h.salt, s_salt, SALT_LEN);
    esp_fill_random(h.nonce, 8);
    memcpy(h.nonce + 8, &seq, 4);   /* the sequence makes the nonce unique */

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, s_key, KEY_LEN * 8);
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, len, h.nonce, NONCE_LEN, (const uint8_t *)&h,
                                       sizeof(h), plain, cipher, TAG_LEN, cipher + len);
    }
    mbedtls_gcm_free(&gcm);
    err = ESP_FAIL;
    if (rc != 0) goto out;

    size_t total = sizeof(h) + len + TAG_LEN;
    size_t erase = (total + 0xFFF) & ~(size_t)0xFFF;
    err = esp_partition_erase_range(s_part, kSlotOff[slot], erase);
    if (err == ESP_OK) err = esp_partition_write(s_part, kSlotOff[slot] + sizeof(h), cipher, len + TAG_LEN);
    if (err == ESP_OK) err = esp_partition_write(s_part, kSlotOff[slot], &h, sizeof(h));   /* last */
    if (err == ESP_OK) {
        s_hdr[slot] = h;
        s_hdr_valid[slot] = true;
        s_current = slot;
        ESP_LOGI(TAG, "saved %d account(s), %u bytes, slot %d, seq %lu", count, (unsigned)len, slot,
                 (unsigned long)seq);
    }

out:
    if (plain) {
        crypto_wipe(plain, MAX_CT_LEN);
        heap_caps_free(plain);
    }
    if (cipher) heap_caps_free(cipher);
    return err;
}

/* Decrypts slot `slot` with `key`. Returns the record count, -1 on failure. */
static int read_blob(int slot, const uint8_t key[KEY_LEN], msf_account_t *out, int max)
{
    const slot_header_t *h = &s_hdr[slot];
    uint8_t *cipher = heap_caps_malloc(h->ct_len + TAG_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *plain = heap_caps_malloc(h->ct_len ? h->ct_len : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int n = -1;
    if (cipher && plain &&
        esp_partition_read(s_part, kSlotOff[slot] + sizeof(*h), cipher, h->ct_len + TAG_LEN) == ESP_OK) {
        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, KEY_LEN * 8);
        if (rc == 0) {
            rc = mbedtls_gcm_auth_decrypt(&gcm, h->ct_len, h->nonce, NONCE_LEN, (const uint8_t *)h, sizeof(*h),
                                          cipher + h->ct_len, TAG_LEN, cipher, plain);
        }
        mbedtls_gcm_free(&gcm);
        if (rc == 0) n = deserialize(plain, h->ct_len, out, max);
    }
    if (plain) {
        crypto_wipe(plain, h->ct_len ? h->ct_len : 1);
        heap_caps_free(plain);
    }
    if (cipher) heap_caps_free(cipher);
    return n;
}

/* ---- public API --------------------------------------------------------- */

esp_err_t msf_store_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "secrets");
    if (!s_part || s_part->size < kSlotOff[1] + SLOT_SIZE) {
        ESP_LOGE(TAG, "partition 'secrets' missing or too small");
        s_state = MSF_STORE_ERROR;
        return ESP_ERR_NOT_FOUND;
    }
    lock();
    read_control();
    read_headers();
    s_state = (s_current < 0) ? MSF_STORE_BLANK : MSF_STORE_LOCKED;
    if (backoff_s(s_fail_count)) s_last_fail_us = esp_timer_get_time();   /* wait counted from boot */
    ESP_LOGI(TAG, "%s, %lu failed attempt(s)", s_state == MSF_STORE_BLANK ? "blank" : "store present",
             (unsigned long)s_fail_count);
    unlock();
    return ESP_OK;
}

msf_store_state_t msf_store_state(void)
{
    return s_state;
}

esp_err_t msf_store_create(const char *pin)
{
    if (!msf_store_pin_valid(pin)) return ESP_ERR_INVALID_ARG;
    lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_state == MSF_STORE_BLANK) {
        random_bytes(s_salt, SALT_LEN);
        s_kdf_m_mib = KDF_M_MIB;
        s_kdf_t = KDF_T;
        err = derive_key(pin, s_salt, s_kdf_m_mib, s_kdf_t, s_key);
        if (err == ESP_OK) err = write_blob(NULL, 0);
        if (err == ESP_OK) err = write_control(0);
        if (err == ESP_OK) s_state = MSF_STORE_UNLOCKED;
        else crypto_wipe(s_key, KEY_LEN);
    }
    unlock();
    return err;
}

esp_err_t msf_store_unlock(const char *pin, msf_account_t *out, int max, int *count)
{
    lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_state != MSF_STORE_LOCKED && s_state != MSF_STORE_UNLOCKED) goto done;
    err = ESP_ERR_TIMEOUT;
    if (msf_store_backoff_remaining_s() > 0) goto done;
    err = ESP_ERR_INVALID_RESPONSE;
    if (!msf_store_pin_valid(pin)) goto done;

    /* Counted BEFORE the check: cutting the power during it gains nothing. */
    err = write_control(s_fail_count + 1);
    if (err != ESP_OK) goto done;

    /* Newest slot first; the older one if the newest does not decrypt. */
    int order[2] = {s_current, 1 - s_current};
    uint8_t key[KEY_LEN];
    bool have_key = false;
    const slot_header_t *keyed = NULL;
    for (int k = 0; k < 2; k++) {
        int slot = order[k];
        if (slot < 0 || !s_hdr_valid[slot]) continue;
        const slot_header_t *h = &s_hdr[slot];
        bool same = keyed && memcmp(keyed->salt, h->salt, SALT_LEN) == 0 && keyed->kdf_m_mib == h->kdf_m_mib &&
                    keyed->kdf_t == h->kdf_t;
        if (!have_key || !same) {
            err = derive_key(pin, h->salt, h->kdf_m_mib, h->kdf_t, key);
            if (err != ESP_OK) goto wipe;
            have_key = true;
            keyed = h;
        }
        int n = read_blob(slot, key, out, max);
        if (n >= 0) {
            memcpy(s_key, key, KEY_LEN);
            memcpy(s_salt, h->salt, SALT_LEN);
            s_kdf_m_mib = h->kdf_m_mib;
            s_kdf_t = h->kdf_t;
            s_current = slot;
            *count = n;
            s_state = MSF_STORE_UNLOCKED;
            write_control(0);
            ESP_LOGI(TAG, "unlocked, %d account(s)", n);
            err = ESP_OK;
            goto wipe;
        }
    }
    s_last_fail_us = esp_timer_get_time();
    ESP_LOGW(TAG, "wrong PIN (%lu consecutive)", (unsigned long)s_fail_count);
    err = ESP_ERR_INVALID_RESPONSE;
wipe:
    crypto_wipe(key, KEY_LEN);
done:
    unlock();
    return err;
}

esp_err_t msf_store_save(const msf_account_t *records, int count)
{
    lock();
    esp_err_t err = (s_state == MSF_STORE_UNLOCKED) ? write_blob(records, count) : ESP_ERR_INVALID_STATE;
    unlock();
    return err;
}

esp_err_t msf_store_change_pin(const char *new_pin, const msf_account_t *records, int count)
{
    if (!msf_store_pin_valid(new_pin)) return ESP_ERR_INVALID_ARG;
    lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_state == MSF_STORE_UNLOCKED) {
        random_bytes(s_salt, SALT_LEN);
        s_kdf_m_mib = KDF_M_MIB;
        s_kdf_t = KDF_T;
        err = derive_key(new_pin, s_salt, s_kdf_m_mib, s_kdf_t, s_key);
        /* Written twice: both slots under the new PIN, so the old PIN no
         * longer opens the older slot. */
        if (err == ESP_OK) err = write_blob(records, count);
        if (err == ESP_OK) err = write_blob(records, count);
        if (err != ESP_OK) {
            crypto_wipe(s_key, KEY_LEN);
            s_state = MSF_STORE_LOCKED;
        }
    }
    unlock();
    return err;
}

esp_err_t msf_store_verify_pin(const char *pin)
{
    lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_state != MSF_STORE_UNLOCKED) goto done;
    err = ESP_ERR_TIMEOUT;
    if (msf_store_backoff_remaining_s() > 0) goto done;
    err = write_control(s_fail_count + 1);   /* counted before the check, as unlock */
    if (err != ESP_OK) goto done;
    err = ESP_ERR_INVALID_RESPONSE;
    if (msf_store_pin_valid(pin)) {
        uint8_t key[KEY_LEN];
        if (derive_key(pin, s_salt, s_kdf_m_mib, s_kdf_t, key) == ESP_OK) {
            uint8_t diff = 0;   /* constant time */
            for (int i = 0; i < KEY_LEN; i++) diff |= key[i] ^ s_key[i];
            if (diff == 0) err = ESP_OK;
        }
        crypto_wipe(key, KEY_LEN);
    }
    if (err == ESP_OK) {
        write_control(0);
    } else {
        s_last_fail_us = esp_timer_get_time();
        ESP_LOGW(TAG, "PIN re-check failed (%lu consecutive)", (unsigned long)s_fail_count);
    }
done:
    unlock();
    return err;
}

esp_err_t msf_store_rescue_image(uint8_t **image_out, size_t *len_out)
{
    *image_out = NULL;
    lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_state == MSF_STORE_UNLOCKED && s_current >= 0) {
        size_t len = sizeof(slot_header_t) + s_hdr[s_current].ct_len + TAG_LEN;
        uint8_t *img = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        err = ESP_ERR_NO_MEM;
        if (img) {
            err = esp_partition_read(s_part, kSlotOff[s_current], img, len);
            if (err == ESP_OK) {
                *image_out = img;
                *len_out = len;
            } else {
                heap_caps_free(img);
            }
        }
    }
    unlock();
    return err;
}

void msf_store_lock(void)
{
    lock();
    crypto_wipe(s_key, KEY_LEN);
    if (s_state == MSF_STORE_UNLOCKED) s_state = MSF_STORE_LOCKED;
    unlock();
}

uint32_t msf_store_fail_count(void)
{
    return s_fail_count;
}

uint32_t msf_store_backoff_remaining_s(void)
{
    uint32_t delay = backoff_s(s_fail_count);
    if (delay == 0) return 0;
    int64_t elapsed_s = (esp_timer_get_time() - s_last_fail_us) / 1000000;
    return elapsed_s >= delay ? 0 : (uint32_t)(delay - elapsed_s);
}

esp_err_t msf_store_erase(void)
{
    if (!s_part) return ESP_ERR_INVALID_STATE;
    lock();
    crypto_wipe(s_key, KEY_LEN);
    esp_err_t err = esp_partition_erase_range(s_part, 0, kSlotOff[1] + SLOT_SIZE);
    if (err == ESP_OK) {
        s_fail_count = 0;
        read_headers();
        s_state = MSF_STORE_BLANK;
        ESP_LOGW(TAG, "store erased");
    }
    unlock();
    return err;
}

/* ---- backup file (.msfx) ------------------------------------------------ */

#define BACKUP_VERSION 1
#define BACKUP_KDF_M_MIB 2     /* same memory as the PIN (fits PSRAM easily) */
#define BACKUP_KDF_T 3         /* 3 passes: ~3 s, a backup is made rarely */

/* File header, authenticated as GCM additional data. Little-endian. */
typedef struct __attribute__((packed)) {
    char magic[4];         /* "MSFX" */
    uint8_t format_version;
    uint8_t kdf_algo;
    uint8_t kdf_t;
    uint8_t kdf_p;
    uint16_t kdf_m_mib;
    uint8_t salt[SALT_LEN];
    uint8_t nonce[NONCE_LEN];
    int64_t created;
    uint16_t count;
    uint16_t reserved;
    uint32_t ct_len;
} backup_header_t;

static bool pass_valid(const char *pass)
{
    size_t n = pass ? strlen(pass) : 0;
    return n >= MSF_BACKUP_PASS_MIN && n <= MSF_BACKUP_PASS_MAX;
}

/* Header checks shared by peek and open. */
static esp_err_t parse_header(const uint8_t *file, size_t len, backup_header_t *h)
{
    if (!file || len < sizeof(*h) + TAG_LEN) return ESP_ERR_INVALID_VERSION;
    memcpy(h, file, sizeof(*h));
    if (memcmp(h->magic, "MSFX", 4) != 0 || h->format_version != BACKUP_VERSION ||
        h->kdf_algo != KDF_ALGO_ARGON2ID || h->kdf_p != KDF_P) {
        return ESP_ERR_INVALID_VERSION;
    }
    /* Bounded before any allocation or KDF: an odd file costs nothing. */
    if (h->kdf_m_mib < 1 || h->kdf_m_mib > 6 || h->kdf_t < 1 || h->kdf_t > 10) return ESP_ERR_INVALID_VERSION;
    if (h->ct_len > MAX_CT_LEN || len != sizeof(*h) + h->ct_len + TAG_LEN) return ESP_ERR_INVALID_SIZE;
    return ESP_OK;
}

esp_err_t msf_backup_seal(const char *passphrase, const msf_account_t *records, int count, int64_t created,
                          uint8_t **file_out, size_t *len_out)
{
    if (!pass_valid(passphrase) || count < 0 || count > 0xFFFF || !file_out || !len_out) return ESP_ERR_INVALID_ARG;
    *file_out = NULL;
    uint8_t *plain = heap_caps_malloc(MAX_CT_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *file = NULL;
    uint8_t key[KEY_LEN];
    backup_header_t h;
    size_t len = 0;
    esp_err_t err = ESP_ERR_NO_MEM;
    if (!plain) goto out;

    len = serialize(records, count, plain, MAX_CT_LEN);
    err = ESP_ERR_INVALID_SIZE;
    if (len == 0) goto out;

    memset(&h, 0, sizeof(h));
    memcpy(h.magic, "MSFX", 4);
    h.format_version = BACKUP_VERSION;
    h.kdf_algo = KDF_ALGO_ARGON2ID;
    h.kdf_t = BACKUP_KDF_T;
    h.kdf_p = KDF_P;
    h.kdf_m_mib = BACKUP_KDF_M_MIB;
    h.created = created;
    h.count = (uint16_t)count;
    h.ct_len = (uint32_t)len;
    random_bytes(h.salt, SALT_LEN);
    random_bytes(h.nonce, NONCE_LEN);   /* fresh key per file: a random nonce is enough */

    err = ESP_ERR_NO_MEM;
    file = heap_caps_malloc(sizeof(h) + len + TAG_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!file) goto out;
    err = derive_key(passphrase, h.salt, h.kdf_m_mib, h.kdf_t, key);
    if (err != ESP_OK) goto out;

    memcpy(file, &h, sizeof(h));
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, KEY_LEN * 8);
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, len, h.nonce, NONCE_LEN, (const uint8_t *)&h,
                                       sizeof(h), plain, file + sizeof(h), TAG_LEN, file + sizeof(h) + len);
    }
    mbedtls_gcm_free(&gcm);
    crypto_wipe(key, KEY_LEN);
    err = rc == 0 ? ESP_OK : ESP_FAIL;
    if (err == ESP_OK) {
        *file_out = file;
        *len_out = sizeof(h) + len + TAG_LEN;
        file = NULL;
        ESP_LOGI(TAG, "backup sealed: %d account(s), %u bytes", count, (unsigned)*len_out);
    }

out:
    if (plain) {
        crypto_wipe(plain, MAX_CT_LEN);
        heap_caps_free(plain);
    }
    if (file) heap_caps_free(file);
    return err;
}

esp_err_t msf_backup_peek(const uint8_t *file, size_t len, msf_backup_info_t *info)
{
    backup_header_t h;
    esp_err_t err = parse_header(file, len, &h);
    if (err == ESP_OK && info) {
        info->created = h.created;
        info->count = h.count;
    }
    return err;
}

esp_err_t msf_backup_open(const char *passphrase, const uint8_t *file, size_t len, msf_account_t *out, int max,
                          int *count, msf_backup_info_t *info)
{
    backup_header_t h;
    esp_err_t err = parse_header(file, len, &h);
    if (err != ESP_OK) return err;
    if (!passphrase || !passphrase[0]) return ESP_ERR_INVALID_RESPONSE;
    if (h.count > max) return ESP_ERR_INVALID_SIZE;

    size_t plain_size = h.ct_len ? h.ct_len : 1;
    uint8_t *plain = heap_caps_malloc(plain_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!plain) return ESP_ERR_NO_MEM;
    uint8_t key[KEY_LEN];
    err = derive_key(passphrase, h.salt, h.kdf_m_mib, h.kdf_t, key);
    if (err == ESP_OK) {
        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, KEY_LEN * 8);
        if (rc == 0) {
            rc = mbedtls_gcm_auth_decrypt(&gcm, h.ct_len, h.nonce, NONCE_LEN, file, sizeof(h),
                                          file + sizeof(h) + h.ct_len, TAG_LEN, file + sizeof(h), plain);
        }
        mbedtls_gcm_free(&gcm);
        crypto_wipe(key, KEY_LEN);
        err = ESP_ERR_INVALID_RESPONSE;
        if (rc == 0) {
            /* Authentic, so written by us or the PC tool: a malformed or
             * over-full payload is still refused, never half-imported. */
            int n = deserialize(plain, h.ct_len, out, max);
            err = (n < 0) ? ESP_ERR_INVALID_VERSION : (n != h.count) ? ESP_ERR_INVALID_SIZE : ESP_OK;
            if (err == ESP_OK) {
                *count = n;
                if (info) {
                    info->created = h.created;
                    info->count = h.count;
                }
            } else {
                memset(out, 0, (size_t)max * sizeof(*out));
            }
        }
    }
    crypto_wipe(plain, plain_size);
    heap_caps_free(plain);
    return err;
}

void msf_backup_free(uint8_t *file)
{
    if (file) heap_caps_free(file);
}
