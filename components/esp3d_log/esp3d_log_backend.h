/*
  esp3d_log backend interface

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

/**
 * Log backend identifiers (compile-time selection via ESP3D_LOG_BACKEND).
 * Canonical definitions live in esp3d_log.h (ESP3D_LOG_BACKEND_SERIAL/SD/
 * UART2/TELNET/WEBSOCKET) - not repeated here to avoid a second name for the
 * same value (see esp3d_log.h for the up-to-date implemented/future status).
 */

/**
 * Backend function pointer type
 *
 * Each backend implements this signature:
 *   - data: null-terminated string to output
 *   - len:  length of data (excluding null terminator)
 *
 * The function should output the data to its destination.
 * It must be safe to call from any task context.
 */
typedef void (*esp3d_log_backend_fn)(const char *data, int len);

/**
 * Backend lifecycle functions
 * Each backend must implement these three functions.
 */

/** Initialize the backend (open file, configure UART, etc.) */
typedef void (*esp3d_log_backend_init_fn)(void);

/** Flush any buffered data */
typedef void (*esp3d_log_backend_flush_fn)(void);

/** Deinitialize the backend (close file, release resources, etc.) */
typedef void (*esp3d_log_backend_deinit_fn)(void);

/**
 * Backend descriptor
 * Groups all backend operations in a single structure.
 */
typedef struct {
    esp3d_log_backend_init_fn   init;
    esp3d_log_backend_fn        write;
    esp3d_log_backend_flush_fn  flush;
    esp3d_log_backend_deinit_fn deinit;
} esp3d_log_backend_t;

/**
 * Get the active backend descriptor.
 * Implemented by the selected backend source file (linked at compile time).
 */
const esp3d_log_backend_t *esp3d_log_get_backend(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
