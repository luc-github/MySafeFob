/* 
 Project: MySafeFob  totp_engine.h
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
 * @file totp_engine.h
 * @brief TOTP engine header — RFC 6238
 */

#pragma once

#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a Base32-encoded secret
 * @param secret_b32 Null-terminated Base32 string
 * @param out_secret Output buffer (must be >= 64 bytes)
 * @param out_len Output: length of decoded secret
 * @return ESP_OK on success
 */
esp_err_t base32_decode(const char *secret_b32, uint8_t *out_secret, size_t *out_len);

/**
 * @brief Generate HOTP code (RFC 4226)
 * @param secret Raw secret bytes
 * @param secret_len Length of secret
 * @param counter Counter value
 * @param digits Number of digits (6 or 8)
 * @param out_code Output buffer (min 9 bytes for null termination)
 * @return ESP_OK on success
 */
esp_err_t hotp_generate(const uint8_t *secret, size_t secret_len,
                        uint64_t counter, uint8_t digits, char *out_code);

/**
 * @brief Generate TOTP code (RFC 6238)
 * @param secret Raw secret bytes
 * @param secret_len Length of secret
 * @param timestamp Unix timestamp (or 0 for current time)
 * @param digits Number of digits (default 6)
 * @param period Time period in seconds (default 30)
 * @param out_code Output buffer (min 9 bytes)
 * @param out_seconds_remaining Optional: seconds until next code
 * @return ESP_OK on success
 */
esp_err_t totp_generate(const uint8_t *secret, size_t secret_len,
                        time_t timestamp, uint8_t digits, uint8_t period,
                        char *out_code, uint8_t *out_seconds_remaining);

/**
 * @brief Run all self-tests (RFC 6238 vectors + Base32 decode)
 * @return ESP_OK if all tests pass
 */
esp_err_t totp_engine_run_self_tests(void);

#ifdef __cplusplus
}
#endif
