/*
  esp3d_log - Logging component for ESP3D-X

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
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// =========================================================================
// Log level definitions
// =========================================================================
#define ESP3D_LOG_LEVEL_NONE    0
#define ESP3D_LOG_LEVEL_ERROR   1
#define ESP3D_LOG_LEVEL_WARNING 2
#define ESP3D_LOG_LEVEL_DEBUG   3
#define ESP3D_LOG_LEVEL_ALL     4

// =========================================================================
// Backend definitions (only one active at compile time)
// =========================================================================
#define ESP3D_LOG_BACKEND_SERIAL 0
#define ESP3D_LOG_BACKEND_SD 1
#define ESP3D_LOG_BACKEND_UART2 2
#define ESP3D_LOG_BACKEND_TELNET 3
#define ESP3D_LOG_BACKEND_WEBSOCKET 4

// =========================================================================
// Default configuration (overridden by dev_tools.cmake defines)
// =========================================================================
#ifndef ESP3D_LOG_BACKEND
#define ESP3D_LOG_BACKEND ESP3D_LOG_BACKEND_SERIAL
#endif

#ifndef ESP3D_LOG_PREFIX
#define ESP3D_LOG_PREFIX ";"
#endif

#ifndef ESP3D_LOG_TIMESTAMP
#define ESP3D_LOG_TIMESTAMP 0
#endif

#ifndef ESP3D_LOG_BUFFER_SIZE
#define ESP3D_LOG_BUFFER_SIZE 512
#endif

// =========================================================================
// Public API
// =========================================================================

// Initialize the log system and active backend
void esp3d_log_init(void);

// Legacy aliases
#define esp3d_log_error(format, ...) esp3d_log_e(format, ##__VA_ARGS__)
#define esp3d_log_warning(format, ...) esp3d_log_w(format, ##__VA_ARGS__)

// =========================================================================
// Optional log hooks
// =========================================================================
// esp3d_log itself stays agnostic of any consumer: a hook receives the log
// level (ESP3D_LOG_LEVEL_ERROR/WARNING/DEBUG/ALL) and the plain formatted
// message (no file/line/timestamp) of every call that reaches
// esp3d_log_output. esp3d_log has no knowledge of what a hook does with it
// or which levels it cares about - that filtering is entirely the hook's
// decision (e.g. a UI-owned system message history registering itself only
// when a screen exists, and only reacting to ESP3D_LOG_LEVEL_ERROR).
#include <stdbool.h>

typedef void (*esp3d_log_hook_t)(int level, const char *message);

#define ESP3D_LOG_HOOKS_MAX 2

// Registers a hook in the first free slot. Returns false if already full or
// if hook is NULL.
bool esp3d_log_register_hook(esp3d_log_hook_t hook);

// Clears a previously registered hook (no-op if not found).
void esp3d_log_unregister_hook(esp3d_log_hook_t hook);

// =========================================================================
// Internal - only available when logging is enabled
// =========================================================================
#if ESP3D_LOG
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Extract filename from full path
const char *esp3d_log_path_to_filename(const char *path);

// Convert uint64_t to string (nanolib printf doesn't support %llu)
const char *esp3d_log_u64_to_str(uint64_t value);

// Convenience macro for uint64 formatting
#define U64_STR(x) esp3d_log_u64_to_str(x)

// Core output function - handles multiline split, prefix, timestamp, and
// dispatch to registered hooks (see esp3d_log_register_hook) with the
// call's level, after being written to the normal backend.
void esp3d_log_output(int level, const char *color, const char *file,
                      unsigned int line, const char *func, const char *format, ...)
    __attribute__((format(printf, 6, 7)));

#endif  // ESP3D_LOG

// =========================================================================
// Color definitions
// =========================================================================
#if DISABLE_COLOR_LOG
#define LOG_COLOR_NORMAL "[LOG]"
#define LOG_COLOR_ERROR "[ERR]"
#define LOG_COLOR_WARNING "[WNG]"
#define LOG_COLOR_DEBUG "[DBG]"
#define LOG_NO_COLOR ""
#else
#define LOG_COLOR_NORMAL "\e[0;36m"
#define LOG_COLOR_ERROR "\e[0;31m"
#define LOG_COLOR_WARNING "\e[1;33m"
#define LOG_COLOR_DEBUG "\e[1;95m"
#define LOG_NO_COLOR "\e[0;37m\e[0m"
#endif

// =========================================================================
// Log macros - public API (unchanged call signatures)
// =========================================================================

// --- Level ALL: esp3d_log (verbose) ---
#if ESP3D_LOG >= ESP3D_LOG_LEVEL_ALL
#define esp3d_log(format, ...)                                                       \
  esp3d_log_output(ESP3D_LOG_LEVEL_ALL, LOG_COLOR_NORMAL, __FILE__, __LINE__,        \
                   __FUNCTION__, format, ##__VA_ARGS__)
#else
#define esp3d_log(format, ...)
#endif

// --- Level DEBUG: esp3d_log_d ---
#if ESP3D_LOG >= ESP3D_LOG_LEVEL_DEBUG
#define esp3d_log_d(format, ...)                                                     \
  esp3d_log_output(ESP3D_LOG_LEVEL_DEBUG, LOG_COLOR_DEBUG, __FILE__, __LINE__,       \
                   __FUNCTION__, format, ##__VA_ARGS__)
#else
#define esp3d_log_d(format, ...)
#endif

// --- Level WARNING: esp3d_log_w ---
#if ESP3D_LOG >= ESP3D_LOG_LEVEL_WARNING
#define esp3d_log_w(format, ...)                                                     \
  esp3d_log_output(ESP3D_LOG_LEVEL_WARNING, LOG_COLOR_WARNING, __FILE__, __LINE__,   \
                   __FUNCTION__, format, ##__VA_ARGS__)
#else
#define esp3d_log_w(format, ...)
#endif

// --- Level ERROR: esp3d_log_e ---
#if ESP3D_LOG >= ESP3D_LOG_LEVEL_ERROR
#define esp3d_log_e(format, ...)                                                     \
  esp3d_log_output(ESP3D_LOG_LEVEL_ERROR, LOG_COLOR_ERROR, __FILE__, __LINE__,       \
                   __FUNCTION__, format, ##__VA_ARGS__)
#else
#define esp3d_log_e(format, ...)
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif
