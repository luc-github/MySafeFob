/* 
 Project: MySafeFob  esp3d_log_serial.c
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
  esp3d_log serial backend

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
#if ESP3D_LOG || ESP3D_X_BENCHMARK

#include "esp3d_log_backend.h"

#include <stdio.h>

#include "esp_log.h"

/* Serial backend: write log data to stdout via esp_log_write */
static void serial_init(void) {
    // Suppress all default ESP-IDF logs except our tag
    esp_log_level_set("*", ESP_LOG_NONE);
    esp_log_level_set("[ESP3D-X]", ESP_LOG_ERROR);
}

static void serial_write(const char *data, int len) {
    // Use esp_log_write with ERROR level to ensure output is not filtered
    esp_log_write(ESP_LOG_ERROR, "[ESP3D-X]", "%.*s", len, data);
}

static void serial_flush(void) {
    fflush(stdout);
}

static void serial_deinit(void) {
    // Nothing to clean up for serial
}

static const esp3d_log_backend_t serial_backend = {
    .init   = serial_init,
    .write  = serial_write,
    .flush  = serial_flush,
    .deinit = serial_deinit,
};

const esp3d_log_backend_t *esp3d_log_get_backend(void) {
    return &serial_backend;
}

#endif  // ESP3D_LOG || ESP3D_X_BENCHMARK
