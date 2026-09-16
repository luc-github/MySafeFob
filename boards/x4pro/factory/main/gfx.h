/**
 * @file gfx.h
 * @brief MySafeFob Factory — 1 bpp GFX for e-paper (port of PiBot gfx.h).
 *
 * Major difference vs PiBot: PiBot wrote directly to the LCD
 * (ili9341_flush by zone). Here the primitives write into a 1 bpp
 * framebuffer in RAM (800x480/8 = 48 KB, internal DRAM) and gfx_flush()
 * pushes the ENTIRE framebuffer to the e-ink panel (full refresh ~2-4s).
 * E-ink usage rule: draw the whole screen in RAM, then a SINGLE flush.
 */
#pragma once

#include <stdint.h>
#include <string.h>
#include "hw_config.h"
#include "font8x16.h"

/* 1 bpp: 1 = white (relaxed pixel), 0 = black (charged pixel) */
#define COLOR_BLACK     0
#define COLOR_WHITE     1

/* x2 font (user request 2026-09-14: 8x16 too small on the
 * 3.7" portrait). Glyph rendered as a 2x2 pixel block. */
#define GFX_FONT_SCALE  2
#define GFX_FONT_W      (FONT_WIDTH * GFX_FONT_SCALE)    /* 16 px */
#define GFX_FONT_H      (FONT_HEIGHT * GFX_FONT_SCALE)   /* 32 px */

/**
 * @brief Init gfx (clears the framebuffer). E-ink init happens before this.
 */
void gfx_init(void);

/**
 * @brief Fills the framebuffer with a color (does not display it).
 */
void gfx_clear(uint8_t color);

/**
 * @brief Pushes the full framebuffer to the panel (full refresh, blocking
 *        ~2-4s). Call once per screen.
 */
void gfx_flush(void);

/**
 * @brief Fast DU refresh variant (~0.5-1s, no inverting flash) for
 *        navigation. Falls back to full GC when needed (first display
 *        or ghost budget exhausted — see eink_display_fb_fast).
 */
void gfx_flush_fast(void);

/**
 * @brief Shared framebuffer (landscape 800x480, same 1bpp convention as
 *        FreeInkUIDisplayTarget — 1=white, MSB-first). Exposed for
 *        battery_icon.cpp: draws directly into it with DisplayTarget
 *        (same logical portrait coords as gfx_*, same internal
 *        rotation), gfx_flush()/gfx_flush_fast() push the combined
 *        result. Do not change the size/convention without updating
 *        both sides.
 */
uint8_t *gfx_framebuffer(void);

void gfx_draw_char(int x, int y, char c, uint8_t fg, uint8_t bg);
void gfx_draw_string(int x, int y, const char *str, uint8_t fg, uint8_t bg);
void gfx_hline(int x, int y, int w, uint8_t color);
void gfx_vline(int x, int y, int h, uint8_t color);
void gfx_rect(int x, int y, int w, int h, uint8_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint8_t color);

/* Centering helper shortcuts */
#define GFX_TEXT_WIDTH(s)   ((int)strlen(s) * GFX_FONT_W)
#define GFX_CENTER_X(s)     ((SCREEN_WIDTH - GFX_TEXT_WIDTH(s)) / 2)
