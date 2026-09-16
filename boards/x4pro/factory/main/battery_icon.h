/* 
 Project: MySafeFob  battery_icon.h
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
 * @file battery_icon.h
 * @brief MySafeFob Factory — icone batterie (FreeInkUI batteryIndicator,
 *        composant seul, sans Frame/StyleSet/ThemeTokens). Dessine dans le
 *        framebuffer partage de gfx.c (gfx_framebuffer()) : n'affiche rien
 *        elle-meme, gfx_flush()/gfx_flush_fast() poussent le resultat.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Dessine le glyphe batterie (rect logique x,y,w,h, coords portrait
 *        identiques a gfx_*) dans le framebuffer partage. Pas de texte
 *        (le pourcentage reste dessine via gfx_draw_string, meme police
 *        que le reste du menu — evite de melanger deux polices).
 */
void battery_icon_draw(int x, int y, int w, int h, uint8_t percent, bool charging);

#ifdef __cplusplus
}
#endif
