/* 
 Project: MySafeFob  buttons.h
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
 * @file buttons.h
 * @brief MySafeFob App — X4 Pro physical buttons.
 *   Left=GPIO0 (up), Right=GPIO7 (down). Power=GPIO3 stays exclusively
 *   owned by power_mgr / power_button_task (main.c) — never read from
 *   here in the app (unlike the factory, where it's a fallback select);
 *   BTN_3 is only reported for completeness, callers in the app's nav
 *   loop must ignore it. All active-LOW, internal pull-up. GPIO0 =
 *   strapping: never held at boot (the bootloader hook uses GPIO3, see
 *   ADR-009).
 */
#pragma once

#include "hw_config.h"

typedef enum {
    BTN_NONE = 0,
    BTN_1,      /* Left  (GPIO0) — focus prev */
    BTN_2,      /* Right (GPIO7) — focus next */
    BTN_3,      /* Power (GPIO3) — reserved for power_mgr, ignore in nav */
} button_id_t;

void buttons_init(void);

/**
 * @brief Instantaneous GPIO level read -- no debounce, never blocks (not
 *        even briefly): the caller (lv_port_indev.c's input_sampler_task)
 *        polls this every loop iteration, interleaved with touch sampling,
 *        and does its own debounce/edge detection on the sequence of
 *        results, the same pattern already used there for touch. A button
 *        held down must never stop that loop from also servicing touch.
 * @return BTN_NONE if no button is currently pressed, otherwise which one.
 */
button_id_t buttons_read_raw(void);
