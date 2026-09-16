/**
 * @file buttons.h
 * @brief MySafeFob Factory — boutons physiques X4 Pro (portage PiBot buttons).
 *   Left=GPIO0 (haut), Right=GPIO7 (bas). Power=GPIO3 = select de SECOURS
 *   (le select principal est le pad Home, zone tactile GT911 dans touch.c).
 *   Tous actif-LOW, pull-up interne. GPIO0 = strapping : jamais maintenu
 *   au boot (le bootloader hook utilise GPIO7 pour le recovery).
 */
#pragma once

#include "hw_config.h"

typedef enum {
    BTN_NONE = 0,
    BTN_1,      /* Left  (GPIO0) — haut */
    BTN_2,      /* Right (GPIO7) — bas */
    BTN_3,      /* Power (GPIO3) — select */
} button_id_t;

void buttons_init(void);

/**
 * @brief Attend un appui (avec debounce), timeout possible.
 * @param timeout_ms Delai max ; 0 = bloquant.
 * @return BTN_NONE si timeout, sinon le bouton appuye.
 */
button_id_t button_wait_press(int timeout_ms);
