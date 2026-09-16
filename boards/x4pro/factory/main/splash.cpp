/* 
 Project: MySafeFob  splash.cpp
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
 * @file splash.cpp
 * @brief MySafeFob Factory — splash statique via FreeInkUI::DisplayTarget.
 *
 * Seul fichier de la factory qui touche a FreeInkUI (copie figee sous
 * components/freeinkui/, cf. ADR-010 amende) — perimetre volontairement
 * reduit a ce seul .cpp pour que le reste de la factory reste en C pur.
 * Buffer et refresh independants de gfx.c/eink.c (pas de dependance
 * croisee) : eink_display_fb() est le seul point de contact avec le driver.
 */
#include "splash.h"

extern "C" {
#include "eink.h"
#include "hw_config.h"
}

#include "splash_bitmap.h"

#include <cstring>

#include <FreeInkUIDisplayTarget.h>

using namespace freeink::ui;

static uint8_t s_splash_fb[SCREEN_FB_SIZE];

void splash_show(void)
{
    memset(s_splash_fb, 0xFF, sizeof(s_splash_fb));

    DisplayTarget target(s_splash_fb, EINK_W, EINK_H, EINK_WB, Orientation::Portrait);

    BitmapRef bmp;
    bmp.data = splash_bits;
    bmp.width = SPLASH_W;
    bmp.height = SPLASH_H;
    bmp.format = BitmapFormat::BW1;
    bmp.progmem = false;

    const Rect full{0, 0, target.logicalWidth(), target.logicalHeight()};
    target.bitmap(full, bmp, BitmapMode::Contain);

    eink_display_fb(s_splash_fb);
}
