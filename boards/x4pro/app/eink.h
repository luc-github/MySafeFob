/**
 * @file eink.h
 * @brief MySafeFob Factory — E-Ink UC8279 driver (X4 Pro).
 *
 * Extracted from the validated probe test_apps/x4pro-probe/main/eink_test.c
 * (UC8279 sequences measured 2026-09-13 — docs/hardware-specs.md).
 *
 * Critical constraints (DO NOT modify without hardware revalidation):
 *  - PSR 0x37 (REG=1) at init; between PON and DRF a FULL rewrite of the
 *    registers with PSR 0x17 (REG=0, MTP scan). NEVER 0x37 at DRF
 *    (full GC hangs: BUSY LOW > 20s, grey screen).
 *  - "Raw" stream mode: gates 0..119 white (pad), 480 fb lines in direct
 *    byte order as-is, white pad up to 600 gates.
 *    => 90 CW hardware rotation, no software transform.
 *  - BUSY active-LOW; DRF timeout 20s; SPI chunk <= 16 KB (S3 DMA).
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Init SPI + GPIO + reset + UC8279 init sequence (PSR 0x37).
 */
esp_err_t eink_init(void);

/**
 * @brief Displays the full framebuffer (1 bpp, 0xFF = white, 0x00 = black,
 *        MSB-first, 100 bytes/line x 480) then a FULL GC refresh.
 *
 * A full refresh takes ~2-4s (blocking). Do not call in a fast loop.
 */
esp_err_t eink_display_fb(const uint8_t *fb);

/**
 * @brief Powers off the controller (POF + wait idle).
 */
esp_err_t eink_power_off(void);
