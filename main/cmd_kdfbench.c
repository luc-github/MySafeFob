/*
 Project: MySafeFob  cmd_kdfbench.c
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
 * @file cmd_kdfbench.c
 * @brief MySafeFob — `kdfbench` console command: calibrates Argon2id
 *        (Monocypher, ADR-004) inside the real firmware, before secret_store
 *        picks its parameters. Replaces the never-flashed standalone bench
 *        (references/test_apps/x4pro-argon2-bench): same library, same
 *        inputs, but with the full app's memory use (LVGL, BLE...), which
 *        decides the largest work area that can actually be allocated.
 *
 *   kdfbench   1. free PSRAM and its largest block
 *              2. two known-answer vectors, computed on the PC with
 *                 argon2-cffi (pin "123456", salt 00..0f, p=1, 32 bytes)
 *              3. time matrix m {1,2,4,6,8} MiB x t {1,2,3,4}, one run each
 *                 (a size that does not fit is skipped)
 *
 * Runs in its own task at idle priority (seconds of CPU per run would
 * otherwise starve the idle task and trip the task watchdog); the console
 * returns at once and the results are printed as they come. Do not touch
 * the device while it runs (UI work would add to the times).
 */
#include "cmd_kdfbench.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "monocypher.h"
#include "ui_nav.h"

#define KEY_LEN 32
#define SALT_LEN 16
#define BLOCK_SIZE 1024   /* Argon2 block: nb_blocks = KiB of memory */

static const char *kPin = "123456";
static const uint8_t kSalt[SALT_LEN] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

typedef struct {
    uint32_t kib;
    uint32_t passes;
    const char *expected_hex;   /* argon2-cffi, Type.ID */
} vector_t;

static const vector_t kVectors[] = {
    {2048, 1, "898e76d4bcda52614ecfd2961847493e80246c0ea7c6c33b227e819d03a40a65"},
    {8192, 4, "728ebaf7269aad817c18b2e8e71c395ce9520ac4eee8172fa1982834ecbabb71"},
};

static volatile bool s_running;

/* Derives the key with `kib` KiB and `passes` passes; returns ms, or -1 if
 * the work area could not be allocated. */
static double derive(uint32_t kib, uint32_t passes, uint8_t key[KEY_LEN])
{
    void *work = heap_caps_malloc((size_t)kib * BLOCK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!work) return -1.0;
    crypto_argon2_config cfg = {
        .algorithm = CRYPTO_ARGON2_ID,
        .nb_blocks = kib,
        .nb_passes = passes,
        .nb_lanes = 1,
    };
    crypto_argon2_inputs in = {
        .pass = (const uint8_t *)kPin,
        .salt = kSalt,
        .pass_size = (uint32_t)strlen(kPin),
        .salt_size = SALT_LEN,
    };
    int64_t t0 = esp_timer_get_time();
    crypto_argon2(key, KEY_LEN, work, cfg, in, crypto_argon2_no_extras);
    int64_t t1 = esp_timer_get_time();
    heap_caps_free(work);
    return (double)(t1 - t0) / 1000.0;
}

static void bench_task(void *arg)
{
    (void)arg;
    printf("\n== kdfbench: Argon2id (Monocypher 4.0.2), p=1 ==\n");
    printf("PSRAM free %u KiB, largest block %u KiB; internal free %u KiB\n",
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

    printf("-- known-answer vectors --\n");
    for (size_t i = 0; i < sizeof(kVectors) / sizeof(kVectors[0]); i++) {
        uint8_t key[KEY_LEN];
        double ms = derive(kVectors[i].kib, kVectors[i].passes, key);
        if (ms < 0) {
            printf("m=%4u KiB t=%u: SKIPPED (does not fit)\n", (unsigned)kVectors[i].kib,
                   (unsigned)kVectors[i].passes);
            continue;
        }
        char hex[KEY_LEN * 2 + 1];
        for (int b = 0; b < KEY_LEN; b++) snprintf(hex + 2 * b, 3, "%02x", key[b]);
        printf("m=%4u KiB t=%u: %s %8.0f ms\n", (unsigned)kVectors[i].kib, (unsigned)kVectors[i].passes,
               strcmp(hex, kVectors[i].expected_hex) == 0 ? "MATCH   " : "MISMATCH", ms);
        vTaskDelay(1);
    }

    printf("-- time matrix (ms) --\n");
    static const uint32_t kMib[] = {1, 2, 4, 6, 8};
    static const uint32_t kPasses[] = {1, 2, 3, 4};
    printf("   m \\ t ");
    for (size_t t = 0; t < sizeof(kPasses) / sizeof(kPasses[0]); t++) printf("%9u", (unsigned)kPasses[t]);
    printf("\n");
    for (size_t m = 0; m < sizeof(kMib) / sizeof(kMib[0]); m++) {
        printf("%3u MiB  ", (unsigned)kMib[m]);
        for (size_t t = 0; t < sizeof(kPasses) / sizeof(kPasses[0]); t++) {
            uint8_t key[KEY_LEN];
            double ms = derive(kMib[m] * 1024, kPasses[t], key);
            if (ms < 0) printf("%9s", "no mem");
            else printf("%9.0f", ms);
            fflush(stdout);
            vTaskDelay(1);
        }
        printf("\n");
    }
    printf("== kdfbench done (target: 500-1000 ms per PIN attempt) ==\n");
    s_running = false;
    vTaskDelete(NULL);
}

static int cmd_kdfbench(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    board_activity_notify();
    if (s_running) {
        printf("kdfbench already running.\n");
        return 1;
    }
    s_running = true;
    /* Idle priority: shares the CPU with the idle task (no watchdog trip). */
    if (xTaskCreate(bench_task, "kdfbench", 6144, NULL, tskIDLE_PRIORITY, NULL) != pdPASS) {
        s_running = false;
        printf("Could not start the benchmark task.\n");
        return 1;
    }
    printf("kdfbench started: results follow (about a minute; do not touch the device).\n");
    return 0;
}

esp_err_t cmd_kdfbench_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "kdfbench",
        .help = "Argon2id calibration (ADR-004): free PSRAM, known-answer vectors, time matrix m x t.",
        .func = &cmd_kdfbench,
    };
    return esp_console_cmd_register(&cmd);
}
