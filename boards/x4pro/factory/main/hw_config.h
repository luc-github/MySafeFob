/* 
 Project: MySafeFob  hw_config.h
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
 * @file hw_config.h
 * @brief MySafeFob Factory — X4 Pro hardware pin definitions (ADR-008).
 *
 * Source of truth: docs/hardware-specs.md (bring-up validated 2026-09-13).
 */
#pragma once

#include "driver/gpio.h"

/* ---- Power rails (universal prerequisite, active at boot) ---- */
#define RAIL_PERIPH_PIN     GPIO_NUM_1   /* peripheral rail, HIGH = ON, held HIGH */
#define RAIL_TOUCH_PIN      GPIO_NUM_2   /* touch power-enable, ACTIVE-LOW (LOW = on) */
#define RAIL_SD_PIN         GPIO_NUM_5   /* SD power-enable, ACTIVE-LOW, pulsed on mount */

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
 * stream = 90 CW hardware rotation — this is the device's CORRECT
 * ORIENTATION (probe measured 12:18, mode 0). The UI works in PORTRAIT
 * 480x800 coordinates: gfx transposes at write time (user(x,y) -> fb(y,x)) —
 * the transpose cancels out the panel's rotation, the rendering comes out
 * upright (arrow validation 12:38).
 * DO NOT add a transform to the stream: everything goes through put_pixel(). */
#define EINK_W              800
#define EINK_H              480
#define EINK_WB             (EINK_W / 8)                  /* 100 bytes/line fb */
#define SCREEN_WIDTH        480                          /* UI portrait */
#define SCREEN_HEIGHT       800
#define SCREEN_FB_SIZE      (EINK_WB * EINK_H)            /* 48000 bytes */

/* ---- I2C bus #0 (shared: GT911, BM8563, CW2017 — see i2c_bus.c) ---- */
#define I2C_PORT            I2C_NUM_0
#define I2C_SDA_PIN         GPIO_NUM_39
#define I2C_SCL_PIN         GPIO_NUM_38
#define I2C_FREQ_HZ         400000

/* ---- RTC BM8563 (0x51) + gauge CW2017 (0x63), same I2C bus #0 ---- */
#define RTC_I2C_ADDR        0x51
#define GAUGE_I2C_ADDR      0x63
#define CHARGE_PIN          GPIO_NUM_21   /* active-HIGH = charging/USB */

/* ---- Touch GT911 ---- */
#define TOUCH_RST_PIN       GPIO_NUM_4
#define TOUCH_INT_PIN       GPIO_NUM_10  /* LOW at reset -> address 0x5D; POR
                                            with INT low = CONFIG UPDATE mode */
/* Validated mapping (4-corner test 00:55, hardware-specs.md):
 * framebuffer coords (landscape 800x480): fb_x = raw_y, fb_y = 479 - raw_x.
 * (swapXY = true, invert_y post-swap = true) */

/* ---- Physical buttons (active-LOW, internal pull-up) ---- */
/* GPIO0 = strapping: OK at runtime, never held at boot (see hooks.c). */
#define BTN_LEFT_PIN        GPIO_NUM_0   /* role: up (BTN1) */
#define BTN_RIGHT_PIN       GPIO_NUM_7   /* role: down (BTN2); triggers recovery at boot */
#define BTN_POWER_PIN       GPIO_NUM_3   /* role: select (BTN3); does not cut power */

/* ---- SD card — native SDMMC 1-bit, slot 1 ---- */
#define SD_MOUNT_POINT      "/sdcard"
#define SD_CLK_PIN          GPIO_NUM_41
#define SD_CMD_PIN          GPIO_NUM_42
#define SD_D0_PIN           GPIO_NUM_40
/* power: RAIL_SD_PIN (GPIO5), pulsed HIGH 80ms -> LOW 120ms on mount */
