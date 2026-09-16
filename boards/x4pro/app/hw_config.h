/**
 * @file hw_config.h
 * @brief MySafeFob App — X4 Pro hardware pin definitions (splash bring-up).
 *
 * Source of truth: docs/hardware-specs.md (bring-up validated 2026-09-13).
 * Subset of factory/main/hw_config.h: only what the splash needs
 * (rails + e-ink). The app's full BSP arrives in Phase 8c
 * (task 8.4) — DO NOT duplicate the touch/SD/RTC drivers here.
 */
#pragma once

#include "driver/gpio.h"

/* ---- Power rails (universal prerequisite, active at boot) ---- */
#define RAIL_PERIPH_PIN     GPIO_NUM_1   /* peripheral rail, HIGH = ON, held HIGH */
#define RAIL_TOUCH_PIN      GPIO_NUM_2   /* touch power-enable, ACTIVE-LOW (LOW = on) */
#define RAIL_SD_PIN         GPIO_NUM_5   /* SD power-enable, ACTIVE-LOW (LOW = on) */

/* ---- E-Ink UC8279 (SPI2, write-only, manual CS) ---- */
#define EINK_HOST           SPI2_HOST
#define EINK_SCLK           GPIO_NUM_12
#define EINK_MOSI           GPIO_NUM_11
#define EINK_CS             GPIO_NUM_13
#define EINK_DC             GPIO_NUM_18
#define EINK_RST            GPIO_NUM_14
#define EINK_BUSY           GPIO_NUM_6   /* BUSY_N: LOW = busy, HIGH = idle */
#define EINK_SPI_HZ         10000000     /* 10 MHz */

/* Panel: native 800x480 LANDSCAPE memory fb (100 bytes/line). The raw
 * stream = 90 CW hardware rotation — CORRECT ORIENTATION (probe mode 0,
 * measured 12:18). The UI works in PORTRAIT 480x800 coords: transposed at
 * write time, fb_x = uy, fb_y = 479 - ux (X mirror included — fb(0,0) shows
 * up at the TOP-RIGHT). DO NOT add a transform to the stream. */
#define EINK_W              800
#define EINK_H              480
#define EINK_WB             (EINK_W / 8)                  /* 100 bytes/line fb */
#define SCREEN_WIDTH        480                          /* UI portrait */
#define SCREEN_HEIGHT       800
#define SCREEN_FB_SIZE      (EINK_WB * EINK_H)            /* 48000 bytes */
