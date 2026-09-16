/**
 * @file touch.h
 * @brief MySafeFob Factory — X4 Pro GT911 touch driver (polling).
 *
 * Port/adaptation of PiBot's touch.h (FT6336U) to GT911, with the
 * sequences validated on the x4pro-probe probe:
 *  - POR reset dance (RST=GPIO4, INT=GPIO10, rail GPIO2 active-low)
 *  - CONFIG UPLOAD MANDATORY on every boot (OTP blank from the factory on
 *    this batch: 0x8047 reads 0x00, the panel doesn't scan without host config)
 *  - mapping validated by the 4-corner test: fb_x = raw_y, fb_y = 479 - raw_x
 *
 * Systematic reads of >= 2 bytes (IDF 5.4 I2C driver quirk:
 * 1-byte reads get NACKed).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool pressed;       /* true if a finger is down */
    bool home;          /* true if the point is within the Home pad zone
                         * (validated measurement 01:34: raw rx<70, ry 380-580) */
    int16_t x;          /* landscape 800x480 framebuffer coords */
    int16_t y;
} touch_point_t;

/**
 * @brief Init rails + GT911 POR dance + I2C probe + config upload if needed.
 * @return true if the controller responds and scans.
 */
bool touch_init(void);

/**
 * @brief Reads the current touch state (polling, non-blocking).
 *        NEVER reset the chip between two reads (2026-09-12 bug:
 *        re-dancing on every poll prevented scanning).
 */
touch_point_t touch_read(void);
