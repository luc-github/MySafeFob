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
