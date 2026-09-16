/**
 * @file touch.h
 * @brief MySafeFob Factory — driver touch GT911 X4 Pro (polling).
 *
 * Portage/adaptation de touch.h PiBot (FT6336U) vers GT911, avec les
 * sequences validees du probe x4pro-probe :
 *  - dance POR sous reset (RST=GPIO4, INT=GPIO10, rail GPIO2 active-low)
 *  - UPLOAD CONFIG OBLIGATOIRE a chaque boot (OTP vide d'usine sur ce batch :
 *    0x8047 lit 0x00, le panel ne scanne pas sans config hote)
 *  - mapping valide test 4 coins : fb_x = raw_y, fb_y = 479 - raw_x
 *
 * Lecture >= 2 octets systematique (anomalie driver I2C IDF 5.4 :
 * les lectures d'1 octet NACKent).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool pressed;       /* true si un doigt est pose */
    bool home;          /* true si le point est dans la zone du pad Home
                         * (mesure validee 01:34 : brut rx<70, ry 380-580) */
    int16_t x;          /* coords framebuffer paysage 800x480 */
    int16_t y;
} touch_point_t;

/**
 * @brief Init rails + dance POR GT911 + probe I2C + upload config si besoin.
 * @return true si le controleur repond et scanne.
 */
bool touch_init(void);

/**
 * @brief Lit l'etat tactile courant (polling, non bloquant).
 *        Ne JAMAIS re-resetter le chip entre deux lectures (bug 2026-09-12 :
 *        re-dancer a chaque poll empechait le scan).
 */
touch_point_t touch_read(void);
