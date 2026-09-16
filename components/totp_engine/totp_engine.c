/* 
 Project: MySafeFob  totp_engine.c
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
 * @file totp_engine.c
 * @brief RFC 4226 HOTP + RFC 6238 TOTP implementation
 *
 * Uses mbedtls for HMAC-SHA1. No external dependencies.
 * Tested against RFC 6238 Appendix B vectors.
 */

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "totp_engine.h"
#include "mbedtls/md.h"
#include "esp_log.h"

static const char *TAG = "totp_engine";

/* ==========================================================================
 * Base32 decode
 * ========================================================================== */

/* value = decoded + 1, 0 = invalid character (designated initializer, C99) */
static const uint8_t base32_dec[256] = {
    ['A'] = 1,  ['B'] = 2,  ['C'] = 3,  ['D'] = 4,  ['E'] = 5,  ['F'] = 6,
    ['G'] = 7,  ['H'] = 8,  ['I'] = 9,  ['J'] = 10, ['K'] = 11, ['L'] = 12,
    ['M'] = 13, ['N'] = 14, ['O'] = 15, ['P'] = 16, ['Q'] = 17, ['R'] = 18,
    ['S'] = 19, ['T'] = 20, ['U'] = 21, ['V'] = 22, ['W'] = 23, ['X'] = 24,
    ['Y'] = 25, ['Z'] = 26,
    ['2'] = 27, ['3'] = 28, ['4'] = 29, ['5'] = 30, ['6'] = 31, ['7'] = 32,
    ['a'] = 1,  ['b'] = 2,  ['c'] = 3,  ['d'] = 4,  ['e'] = 5,  ['f'] = 6,
    ['g'] = 7,  ['h'] = 8,  ['i'] = 9,  ['j'] = 10, ['k'] = 11, ['l'] = 12,
    ['m'] = 13, ['n'] = 14, ['o'] = 15, ['p'] = 16, ['q'] = 17, ['r'] = 18,
    ['s'] = 19, ['t'] = 20, ['u'] = 21, ['v'] = 22, ['w'] = 23, ['x'] = 24,
    ['y'] = 25, ['z'] = 26,
};

esp_err_t base32_decode(const char *secret_b32, uint8_t *out_secret, size_t *out_len)
{
    if (!secret_b32 || !out_secret || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t in_len = strlen(secret_b32);
    size_t out_pos = 0;
    uint32_t buffer = 0;
    int bits = 0;

    for (size_t i = 0; i < in_len; i++) {
        char c = secret_b32[i];
        if (c == '=') {
            break;  /* Padding */
        }
        uint8_t val = base32_dec[(uint8_t)c];
        if (val == 0) {
            return ESP_ERR_INVALID_ARG;  /* Invalid character */
        }
        buffer = (buffer << 5) | (val - 1);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out_secret[out_pos++] = (buffer >> bits) & 0xFF;
        }
    }

    *out_len = out_pos;
    return ESP_OK;
}

/* ==========================================================================
 * HOTP / TOTP core
 * ========================================================================== */

esp_err_t hotp_generate(const uint8_t *secret, size_t secret_len,
                        uint64_t counter, uint8_t digits, char *out_code)
{
    if (!secret || !out_code || digits < 6 || digits > 8) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 1. Counter as 8-byte big-endian */
    uint8_t counter_bytes[8];
    for (int i = 7; i >= 0; i--) {
        counter_bytes[i] = counter & 0xFF;
        counter >>= 8;
    }

    /* 2. HMAC-SHA1 via mbedtls */
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (!md_info) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t mac[20];
    int ret = mbedtls_md_hmac(md_info, secret, secret_len,
                              counter_bytes, sizeof(counter_bytes), mac);
    if (ret != 0) {
        return ESP_FAIL;
    }

    /* 3. Dynamic truncation (RFC 4226 section 5.3) */
    uint8_t offset = mac[19] & 0x0F;
    uint32_t code = ((uint32_t)mac[offset] << 24) |
                    ((uint32_t)mac[offset + 1] << 16) |
                    ((uint32_t)mac[offset + 2] << 8) |
                    ((uint32_t)mac[offset + 3]);
    code &= 0x7FFFFFFF;

    /* 4. Modulo */
    uint32_t mod = 1;
    for (uint8_t i = 0; i < digits; i++) {
        mod *= 10;
    }
    code %= mod;

    /* 5. Format with leading zeros (manual: avoids -Wformat-truncation
     *    since GCC cannot prove code < 10^digits at compile time) */
    for (int i = digits - 1; i >= 0; i--) {
        out_code[i] = (char)('0' + (code % 10));
        code /= 10;
    }
    out_code[digits] = '\0';

    return ESP_OK;
}

esp_err_t totp_generate(const uint8_t *secret, size_t secret_len,
                        time_t timestamp, uint8_t digits, uint8_t period,
                        char *out_code, uint8_t *out_seconds_remaining)
{
    if (!secret || !out_code || period == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (timestamp == 0) {
        timestamp = time(NULL);
    }

    uint64_t counter = (uint64_t)(timestamp / period);

    if (out_seconds_remaining) {
        *out_seconds_remaining = period - (timestamp % period);
    }

    return hotp_generate(secret, secret_len, counter, digits, out_code);
}

/* ==========================================================================
 * Self-test: RFC 6238 Appendix B vectors
 * ========================================================================== */

static esp_err_t totp_self_test(void)
{
    /* Test secret: "12345678901234567890" (20 bytes) */
    const uint8_t secret[20] = {
        0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
        0x39, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
        0x37, 0x38, 0x39, 0x30
    };

    struct {
        time_t ts;
        const char *expected;
    } vectors[] = {
        { 59,           "94287082" },
        { 1111111109,   "07081804" },
        { 1111111111,   "14050471" },
        { 1234567890,   "89005924" },
        { 2000000000,   "69279037" },
        { 20000000000,  "65353130" },
    };

    ESP_LOGI(TAG, "Running RFC 6238 self-test...");
    int passed = 0;
    int total = sizeof(vectors) / sizeof(vectors[0]);

    for (int i = 0; i < total; i++) {
        char code[9];
        esp_err_t err = totp_generate(secret, sizeof(secret), vectors[i].ts, 8, 30, code, NULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Vector %d: generation failed", i);
            continue;
        }
        bool ok = (strcmp(code, vectors[i].expected) == 0);
        ESP_LOGI(TAG, "  T=%-11lld | Code=%s | Expected=%s | %s",
                 (long long)vectors[i].ts, code, vectors[i].expected, ok ? "PASS" : "FAIL");
        if (ok) passed++;
    }

    ESP_LOGI(TAG, "Self-test: %d/%d passed", passed, total);
    return (passed == total) ? ESP_OK : ESP_FAIL;
}

/* ==========================================================================
 * Base32 self-test
 * ========================================================================== */

static esp_err_t base32_self_test(void)
{
    struct {
        const char *input;
        const uint8_t *expected;
        size_t expected_len;
    } tests[] = {
        { "JBSWY3DPEHPK3PXP",
          (const uint8_t[]) {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x21, 0xde, 0xad, 0xbe, 0xef},
          10 },
        { "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ",
          (const uint8_t[]) {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
                             0x39, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
                             0x37, 0x38, 0x39, 0x30},
          20 },
    };

    ESP_LOGI(TAG, "Running Base32 self-test...");
    int passed = 0;
    int total = sizeof(tests) / sizeof(tests[0]);

    for (int i = 0; i < total; i++) {
        uint8_t decoded[64];
        size_t len;
        esp_err_t err = base32_decode(tests[i].input, decoded, &len);
        bool ok = (err == ESP_OK && len == tests[i].expected_len &&
                   memcmp(decoded, tests[i].expected, len) == 0);
        ESP_LOGI(TAG, "  \"%s\" -> %s", tests[i].input, ok ? "PASS" : "FAIL");
        if (ok) passed++;
    }

    ESP_LOGI(TAG, "Base32 test: %d/%d passed", passed, total);
    return (passed == total) ? ESP_OK : ESP_FAIL;
}

/* ==========================================================================
 * Public self-test entry point
 * ========================================================================== */

esp_err_t totp_engine_run_self_tests(void)
{
    esp_err_t err1 = base32_self_test();
    esp_err_t err2 = totp_self_test();
    return (err1 == ESP_OK && err2 == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/* ==========================================================================
 * Module init: run all self-tests at startup
 * ========================================================================== */

__attribute__((constructor))
static void totp_engine_init(void)
{
    ESP_LOGI(TAG, "TOTP engine initializing...");

    if (totp_engine_run_self_tests() == ESP_OK) {
        ESP_LOGI(TAG, "All self-tests PASSED — engine ready");
    } else {
        ESP_LOGE(TAG, "Self-tests FAILED — check implementation");
    }
}
