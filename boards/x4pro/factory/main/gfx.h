/**
 * @file gfx.h
 * @brief MySafeFob Factory — GFX 1 bpp pour e-paper (portage PiBot gfx.h).
 *
 * Difference majeure vs PiBot : le PiBot ecrivait directement au LCD
 * (ili9341_flush par zone). Ici les primitives ecrivent dans un framebuffer
 * 1 bpp en RAM (800x480/8 = 48 Ko, DRAM interne) et gfx_flush() pousse le
 * framebuffer ENTIER vers le panel e-ink (full refresh ~2-4 s).
 * Regle d'usage e-ink : dessiner tout l'ecran en RAM, puis UN SEUL flush.
 */
#pragma once

#include <stdint.h>
#include <string.h>
#include "hw_config.h"
#include "font8x16.h"

/* 1 bpp : 1 = blanc (pixel relaxe), 0 = noir (pixel charge) */
#define COLOR_BLACK     0
#define COLOR_WHITE     1

/* Police x2 (demande utilisateur 2026-09-14 : 8x16 trop petite sur le
 * 3.7" portrait). Glyphe rendu en bloc 2x2 pixels. */
#define GFX_FONT_SCALE  2
#define GFX_FONT_W      (FONT_WIDTH * GFX_FONT_SCALE)    /* 16 px */
#define GFX_FONT_H      (FONT_HEIGHT * GFX_FONT_SCALE)   /* 32 px */

/**
 * @brief Init gfx (vide le framebuffer). L'init e-ink se fait avant.
 */
void gfx_init(void);

/**
 * @brief Remplit le framebuffer d'une couleur (n'affiche pas).
 */
void gfx_clear(uint8_t color);

/**
 * @brief Pousse le framebuffer complet vers le panel (full refresh, bloquant
 *        ~2-4 s). A appeler une seule fois par ecran.
 */
void gfx_flush(void);

/**
 * @brief Variante refresh rapide DU (~0,5-1 s, sans flash d'inversion) pour
 *        la navigation. Retombe en full GC quand necessaire (1er affichage
 *        ou budget de ghosts epuise — voir eink_display_fb_fast).
 */
void gfx_flush_fast(void);

/**
 * @brief Framebuffer partage (paysage 800x480, meme convention 1bpp que
 *        FreeInkUIDisplayTarget — 1=blanc, MSB-first). Expose pour
 *        battery_icon.cpp : dessine directement dedans avec DisplayTarget
 *        (memes coords logiques portrait que gfx_*, meme rotation
 *        interne), gfx_flush()/gfx_flush_fast() poussent le resultat
 *        combine. Ne pas modifier la taille/convention sans mettre a jour
 *        les deux cotes.
 */
uint8_t *gfx_framebuffer(void);

void gfx_draw_char(int x, int y, char c, uint8_t fg, uint8_t bg);
void gfx_draw_string(int x, int y, const char *str, uint8_t fg, uint8_t bg);
void gfx_hline(int x, int y, int w, uint8_t color);
void gfx_vline(int x, int y, int h, uint8_t color);
void gfx_rect(int x, int y, int w, int h, uint8_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint8_t color);

/* Raccourcis d'aide au centrage */
#define GFX_TEXT_WIDTH(s)   ((int)strlen(s) * GFX_FONT_W)
#define GFX_CENTER_X(s)     ((SCREEN_WIDTH - GFX_TEXT_WIDTH(s)) / 2)
