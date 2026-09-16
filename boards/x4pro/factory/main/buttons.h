/**
 * @file buttons.h
 * @brief MySafeFob Factory — X4 Pro physical buttons (port of PiBot buttons).
 *   Left=GPIO0 (up), Right=GPIO7 (down). Power=GPIO3 = FALLBACK select
 *   (the primary select is the Home pad, GT911 touch zone in touch.c).
 *   All active-LOW, internal pull-up. GPIO0 = strapping: never held
 *   at boot (the bootloader hook uses GPIO7 for recovery).
 */
#pragma once

#include "hw_config.h"

typedef enum {
    BTN_NONE = 0,
    BTN_1,      /* Left  (GPIO0) — up */
    BTN_2,      /* Right (GPIO7) — down */
    BTN_3,      /* Power (GPIO3) — select */
} button_id_t;

void buttons_init(void);

/**
 * @brief Waits for a press (with debounce), timeout possible.
 * @param timeout_ms Max delay; 0 = blocking.
 * @return BTN_NONE on timeout, otherwise the pressed button.
 */
button_id_t button_wait_press(int timeout_ms);
