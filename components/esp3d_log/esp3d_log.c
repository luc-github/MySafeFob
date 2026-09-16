/* 
 Project: MySafeFob  esp3d_log.c
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
/*
  esp3d_log - Core logging implementation for ESP3D-X

  Copyright (c) 2022 Luc Lebosse. All rights reserved.

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
#include "esp3d_log.h"

#if ESP3D_LOG

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp3d_log_backend.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if ESP3D_LOG_TIMESTAMP
#include "esp_timer.h"
#endif

// =========================================================================
// Backend reference
// =========================================================================
static const esp3d_log_backend_t *s_backend = NULL;

// =========================================================================
// Serialization: esp3d_log_output() uses static, non-reentrant buffers
// (msg_buf, header_buf, indent_buf, line_buf) shared across every task that
// logs. Without a lock, two tasks logging concurrently (e.g. the UI task and
// esp3d_serial_rx_task) race on those buffers - created lazily so early
// pre-scheduler calls (before esp3d_log_init()) still work uncontended.
static SemaphoreHandle_t s_log_mutex = NULL;

// =========================================================================
// Optional log hooks (see esp3d_log_register_hook in esp3d_log.h)
// =========================================================================
static esp3d_log_hook_t s_hooks[ESP3D_LOG_HOOKS_MAX] = {0};

bool esp3d_log_register_hook(esp3d_log_hook_t hook) {
  if (!hook) return false;
  for (int i = 0; i < ESP3D_LOG_HOOKS_MAX; i++) {
    if (s_hooks[i] == hook) return true;  // already registered
  }
  for (int i = 0; i < ESP3D_LOG_HOOKS_MAX; i++) {
    if (!s_hooks[i]) {
      s_hooks[i] = hook;
      return true;
    }
  }
  return false;  // no free slot
}

void esp3d_log_unregister_hook(esp3d_log_hook_t hook) {
  for (int i = 0; i < ESP3D_LOG_HOOKS_MAX; i++) {
    if (s_hooks[i] == hook) {
      s_hooks[i] = NULL;
      return;
    }
  }
}

// =========================================================================
// Utility: extract filename from path
// =========================================================================
const char *esp3d_log_path_to_filename(const char *path) {
  size_t i = 0;
  size_t pos = 0;
  const char *p = path;
  while (*p) {
    i++;
    if (*p == '/' || *p == '\\') {
      pos = i;
    }
    p++;
  }
  return path + pos;
}

// =========================================================================
// Utility: uint64_t to string (nanolib printf lacks %llu support)
// Uses 4 rotating buffers for safe use of multiple U64_STR() in one call
// =========================================================================
const char *esp3d_log_u64_to_str(uint64_t value) {
  static char buffers[4][21];  // max 20 digits for uint64 + null
  static int current = 0;

  char *buffer = buffers[current];
  current = (current + 1) % 4;

  if (value == 0) {
    buffer[0] = '0';
    buffer[1] = '\0';
    return buffer;
  }

  char temp[21];
  int pos = 0;

  // Extract digits in reverse order
  while (value > 0) {
    temp[pos++] = '0' + (value % 10);
    value /= 10;
  }

  // Reverse into output buffer
  for (int i = 0; i < pos; i++) {
    buffer[i] = temp[pos - 1 - i];
  }
  buffer[pos] = '\0';

  return buffer;
}

// =========================================================================
// Timestamp formatting
// =========================================================================
#if ESP3D_LOG_TIMESTAMP
// Format timestamp as [+SSSSs.MMM] (seconds.milliseconds since boot)
static int format_timestamp(char *buf, size_t buf_size) {
  int64_t us = esp_timer_get_time();
  uint32_t total_ms = (uint32_t)(us / 1000);
  uint32_t seconds = total_ms / 1000;
  uint32_t ms = total_ms % 1000;
  return snprintf(buf, buf_size, "[+%lu.%03lu] ", (unsigned long)seconds,
                  (unsigned long)ms);
}
#endif

// =========================================================================
// Internal: write a single formatted line to the backend
// =========================================================================
static void write_line(const char *color, const char *header,
                       const char *text) {
  if (!s_backend || !s_backend->write) return;

  // Static buffer for line assembly
  static char line_buf[ESP3D_LOG_BUFFER_SIZE];
  int offset = 0;
  int remaining = (int)sizeof(line_buf) - 1;

// Comment prefix (e.g. ";" to make log lines ignored by target firmware)
#define LOG_PREFIX_STR ESP3D_LOG_PREFIX
  if (LOG_PREFIX_STR[0] != '\0') {
    int n = snprintf(line_buf + offset, remaining, "%s", LOG_PREFIX_STR);
    if (n > 0) {
      offset += n;
      remaining -= n;
    }
  }

  // Timestamp
#if ESP3D_LOG_TIMESTAMP
  if (remaining > 0) {
    int n = format_timestamp(line_buf + offset, remaining);
    if (n > 0) {
      offset += n;
      remaining -= n;
    }
  }
#endif

  // Color + header + text + color reset + newline
  if (remaining > 0) {
    int n = snprintf(line_buf + offset, remaining, "%s%s%s%s\n", color, header,
                     text, LOG_NO_COLOR);
    if (n > 0) {
      offset += n;
      if (offset >= (int)sizeof(line_buf)) {
        offset = (int)sizeof(line_buf) - 1;
      }
    }
  }

  line_buf[offset] = '\0';
  s_backend->write(line_buf, offset);
}

// =========================================================================
// Core output function
// =========================================================================
// Called by the log macros. Formats the user message, splits on \n and \r,
// and outputs each sub-line with proper header or continuation indent.
void esp3d_log_output(int level, const char *color, const char *file, unsigned int line,
                      const char *func, const char *format, ...) {
  if (!s_backend) return;

  // Serialize access to the static formatting buffers below (msg_buf,
  // header_buf, indent_buf, and line_buf inside write_line()). Skip locking
  // only if called before esp3d_log_init() created the mutex (single-threaded
  // early boot). A generous timeout avoids a permanent stall if ever called
  // from a context that can't safely block (e.g. mutex already held by a
  // task that got suspended) rather than silently corrupting the buffers.
  bool locked = false;
  if (s_log_mutex) {
    locked = (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(200)) == pdTRUE);
    if (!locked) {
      // Couldn't get exclusive access to the shared buffers in time - drop
      // this line rather than risk racing another task's in-flight format.
      return;
    }
  }

  // Format the user message into a temporary buffer
  static char msg_buf[ESP3D_LOG_BUFFER_SIZE];
  va_list args;
  va_start(args, format);
  vsnprintf(msg_buf, sizeof(msg_buf), format, args);
  va_end(args);

  // Dispatch to registered hooks (plain message, no file/line/timestamp) -
  // filtering by level is entirely up to each hook, see esp3d_log.h.
  for (int i = 0; i < ESP3D_LOG_HOOKS_MAX; i++) {
    if (s_hooks[i]) {
      s_hooks[i](level, msg_buf);
    }
  }

  // Build the header: [filename:line] function():
  static char header_buf[128];
  const char *filename = esp3d_log_path_to_filename(file);
  int header_len =
      snprintf(header_buf, sizeof(header_buf), "[%s:%u] %s(): ", filename,
               line, func);
  if (header_len < 0) header_len = 0;
  if (header_len >= (int)sizeof(header_buf))
    header_len = (int)sizeof(header_buf) - 1;

  // Build continuation indent (same width as header, filled with spaces)
  static char indent_buf[128];
  if (header_len < (int)sizeof(indent_buf)) {
    memset(indent_buf, ' ', header_len);
    indent_buf[header_len] = '\0';
  } else {
    indent_buf[0] = '\0';
  }

  // Split message on \n and \r, output each sub-line
  bool first_line = true;
  char *cursor = msg_buf;

  while (*cursor) {
    // Find next line break
    char *break_pos = cursor;
    while (*break_pos && *break_pos != '\n' && *break_pos != '\r') {
      break_pos++;
    }

    // Temporarily null-terminate this segment
    char saved = *break_pos;
    *break_pos = '\0';

    // Only output non-empty segments
    if (break_pos > cursor) {
      write_line(color, first_line ? header_buf : indent_buf, cursor);
      first_line = false;
    }

    // Restore and advance past the line break(s)
    *break_pos = saved;
    if (*break_pos == '\r' && *(break_pos + 1) == '\n') {
      break_pos += 2;  // Skip \r\n pair
    } else if (*break_pos == '\n' || *break_pos == '\r') {
      break_pos += 1;  // Skip single \n or \r
    }
    cursor = break_pos;
  }

  // If the message was a single line (no splits), ensure it was output
  if (first_line) {
    write_line(color, header_buf, msg_buf);
  }

  // Flush after each log entry
  if (s_backend->flush) {
    s_backend->flush();
  }

  if (locked) {
    xSemaphoreGive(s_log_mutex);
  }
}

// =========================================================================
// Initialization
// =========================================================================
void esp3d_log_init(void) {
  if (!s_log_mutex) {
    s_log_mutex = xSemaphoreCreateMutex();
  }
#if !defined(SHOW_ESP_LOG)
  esp_log_level_set("wifi", ESP_LOG_NONE);
  esp_log_level_set("sdmmc", ESP_LOG_NONE);
  esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_NONE);
  esp_log_level_set("sdmmc_periph", ESP_LOG_NONE);
  esp_log_level_set("sdmmc_req",    ESP_LOG_NONE);
  esp_log_level_set("sdmmc_common", ESP_LOG_NONE);
  esp_log_level_set("fatfs", ESP_LOG_NONE);
  esp_log_level_set("sdspi", ESP_LOG_NONE);
  esp_log_level_set("sd_diskio", ESP_LOG_NONE);
  esp_log_level_set("vfs_fat",        ESP_LOG_NONE);
  esp_log_level_set("esp_littlefs",   ESP_LOG_NONE);
  esp_log_level_set("task_wdt",       ESP_LOG_NONE);
  esp_log_level_set("camera",         ESP_LOG_NONE);
  esp_log_level_set("sccb",           ESP_LOG_NONE);
  esp_log_level_set("ov2640",         ESP_LOG_NONE);
  esp_log_level_set("esp_eth",        ESP_LOG_NONE);
  esp_log_level_set("emac",           ESP_LOG_NONE);
  esp_log_level_set("phy",            ESP_LOG_NONE);
#endif // !defined(SHOW_ESP_LOG)
  s_backend = esp3d_log_get_backend();
  if (s_backend && s_backend->init) {
    s_backend->init();
  }
}

#else
// =========================================================================
// Logging disabled - provide stubs
// =========================================================================
void esp3d_log_init(void) {}
bool esp3d_log_register_hook(esp3d_log_hook_t hook) { return false; }
void esp3d_log_unregister_hook(esp3d_log_hook_t hook) {}

#endif  // ESP3D_LOG