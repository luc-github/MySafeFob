/* 
 Project: MySafeFob  gfx.c
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
 * @file gfx.c
 * @brief MySafeFob Factory — 1 bpp GFX: framebuffer + primitives.
 *   Ported from a direct RGB565 LCD design to a 1 bpp e-ink framebuffer.
 */
#include "gfx.h"
#include "eink.h"
#include "font8x16.h"

#include <string.h>

/* Internal DRAM framebuffer (zero PSRAM required — the probe validated this choice).
 * Bit 1 = white, bit 0 = black, MSB-first (bit 7 = leftmost pixel). */
static uint8_t s_fb[SCREEN_FB_SIZE];

static inline void put_pixel(int x, int y, uint8_t color)
{
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    /* Transpose portrait UI -> landscape fb. Validated by the 12:38 arrow
     * measurement:
     *   fb +x displays DOWNWARD (sy = fb_x)
     *   fb (0,0) displays at the TOP-RIGHT (sx = 479 - fb_y)
     * So user(ux,uy) -> fb_x = uy, fb_y = 479 - ux. Without the X mirror the
     * text would be reversed. Same as the previously validated touch mapping. */
    uint8_t *b = &s_fb[(SCREEN_WIDTH - 1 - x) * EINK_WB + (y >> 3)];
    if (color) {
        *b |= (uint8_t)(0x80 >> (y & 7));
    } else {
        *b &= (uint8_t)~(0x80 >> (y & 7));
    }
}

void gfx_init(void)
{
    gfx_clear(COLOR_WHITE);
}

uint8_t *gfx_framebuffer(void)
{
    return s_fb;
}

void gfx_clear(uint8_t color)
{
    memset(s_fb, color ? 0xFF : 0x00, sizeof(s_fb));
}

void gfx_flush(void)
{
    eink_display_fb(s_fb);
}

void gfx_flush_fast(void)
{
    eink_display_fb_fast(s_fb);
}

void gfx_draw_char(int x, int y, char c, uint8_t fg, uint8_t bg)
{
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *glyph = font8x16_get_glyph(c);
    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row * FONT_BYTES_PER_ROW];
        for (int col = 0; col < FONT_WIDTH; col++) {
            uint8_t color = (bits & (0x80 >> col)) ? fg : bg;
            /* x2 rendering: a GFX_FONT_SCALE x GFX_FONT_SCALE block per pixel. */
            for (int dy = 0; dy < GFX_FONT_SCALE; dy++) {
                for (int dx = 0; dx < GFX_FONT_SCALE; dx++) {
                    put_pixel(x + col * GFX_FONT_SCALE + dx,
                              y + row * GFX_FONT_SCALE + dy, color);
                }
            }
        }
    }
}

void gfx_draw_string(int x, int y, const char *str, uint8_t fg, uint8_t bg)
{
    while (*str) {
        if (x + GFX_FONT_W > SCREEN_WIDTH) break;
        gfx_draw_char(x, y, *str, fg, bg);
        x += GFX_FONT_W;
        str++;
    }
}

void gfx_hline(int x, int y, int w, uint8_t color)
{
    if (y < 0 || y >= SCREEN_HEIGHT || x >= SCREEN_WIDTH) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > SCREEN_WIDTH) w = SCREEN_WIDTH - x;
    for (int i = 0; i < w; i++) put_pixel(x + i, y, color);
}

void gfx_vline(int x, int y, int h, uint8_t color)
{
    if (x < 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    for (int i = 0; i < h; i++) put_pixel(x, y + i, color);
}

void gfx_rect(int x, int y, int w, int h, uint8_t color)
{
    gfx_hline(x, y, w, color);
    gfx_hline(x, y + h - 1, w, color);
    gfx_vline(x, y, h, color);
    gfx_vline(x + w - 1, y, h, color);
}

void gfx_fill_rect(int x, int y, int w, int h, uint8_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_WIDTH) w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) put_pixel(x + col, y + row, color);
    }
}
