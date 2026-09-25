/*
 Project: MySafeFob  ui_keyboard.h
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
 * @file ui_keyboard.h
 * @brief MySafeFob App — on-screen alphanumeric keyboard (lowercase,
 *        uppercase, digits/symbols, more symbols), used for Wi-Fi and BLE
 *        passwords. One instance at a time. Callbacks run synchronously in
 *        the LVGL task, in tap order (lv_async_call would reorder fast taps).
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus

struct ui_keyboard_cb_t {
    void (*on_char)(char c, void *ctx);
    void (*on_backspace)(void *ctx);
    void (*on_enter)(void *ctx);
    void *ctx;
};

/**
 * @brief Builds the keyboard as a full-width (screen-wide) block of 4 key
 *        rows under `parent`; the caller positions the returned container
 *        and shows/hides it. Every key is added to `group`.
 */
lv_obj_t *ui_keyboard_create(lv_obj_t *parent, lv_group_t *group, const ui_keyboard_cb_t *cb);

/** @brief Height in pixels of the block returned by ui_keyboard_create(). */
int32_t ui_keyboard_height(void);

#endif /* __cplusplus */
