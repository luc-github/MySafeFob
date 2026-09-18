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
 * @brief MySafeFob App — reTerminal Sticky (dev target) hardware pin
 * definitions.
 *
 * NOT bring-up validated on this repo's hardware yet. Pins below come
 * from a third-party reference (MIT), see boards/sticky_dev/README.md
 * for source and status. Treat as a starting point for probing, not
 * as ground truth — update this file once each pin is physically
 * confirmed, the same way docs/hardware-specs.md was for x4pro.
 */
#pragma once

#include "driver/gpio.h"

/* ---- AI / power button (single button, active-LOW assumed — confirm) --- */
#define BTN_AI_PIN          GPIO_NUM_4

/* ---- External-power detect ---- */
#define EXT_POWER_DETECT_PIN GPIO_NUM_9

/* ---- E-Ink SSD1677 (SPI, pins 13-18 per reference — role per-pin TBD) --- */
#define EINK_SCLK           GPIO_NUM_13  /* TBD: confirm role against datasheet */
#define EINK_MOSI           GPIO_NUM_14  /* TBD: confirm role against datasheet */
#define EINK_CS              GPIO_NUM_15  /* TBD: confirm role against datasheet */
#define EINK_DC              GPIO_NUM_16  /* TBD: confirm role against datasheet */
#define EINK_RST             GPIO_NUM_17  /* TBD: confirm role against datasheet */
#define EINK_BUSY            GPIO_NUM_18  /* TBD: confirm role against datasheet */

#define EINK_W               800
#define EINK_H               480

/* ---- I2C bus (GT911 touch — same controller as x4pro, different pins) -- */
#define I2C_PORT             I2C_NUM_0
#define I2C_SDA_PIN          GPIO_NUM_2
#define I2C_SCL_PIN          GPIO_NUM_3
#define I2C_FREQ_HZ          400000

/* ---- Touch GT911 ---- */
#define TOUCH_ENABLE_PIN     GPIO_NUM_42
#define TOUCH_INT_PIN        GPIO_NUM_21
#define TOUCH_RST_PIN        GPIO_NUM_41

/* ---- Battery gauge BQ27220 (separate I2C bus per reference) ---- */
#define GAUGE_I2C_SDA_PIN    GPIO_NUM_0
#define GAUGE_I2C_SCL_PIN    GPIO_NUM_1
#define CHARGER_ENABLE_PIN   GPIO_NUM_39

/* ---- Buzzer (LEDC PWM, deferred — not needed for dev-board bring-up) --- */
#define BUZZER_PIN           GPIO_NUM_48

/* ---- Power hold / lock (role TBD — confirm against datasheet) ---- */
#define POWER_HOLD_PIN       GPIO_NUM_45
#define POWER_LOCK_PIN       GPIO_NUM_46
