/*
 Project: MySafeFob  ui_keyboard.cpp
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
 * @file ui_keyboard.cpp
 * @brief MySafeFob App — alphanumeric on-screen keyboard, see ui_keyboard.h.
 *        Four character rows (7/7/6/6 keys, the last one followed by the
 *        backspace key) plus a bottom control row: fewer keys per row than a
 *        classic 10-key QWERTY, so keys are wider and the gaps between them
 *        larger (touch precision).
 */
#include "ui_keyboard.h"
#include "ui_widgets.h"

#include <cstring>

static constexpr int kCharRows = 4;
static constexpr int kMaxRowKeys = 7;
static constexpr int kRowKeyCount[kCharRows] = {7, 7, 6, 6};
static constexpr int kRowCount = kCharRows + 1; /* + bottom control row */

static constexpr int32_t kKeyW = 54;
static constexpr int32_t kKeyH = 50;
static constexpr int32_t kColGap = 10;
static constexpr int32_t kRowGap = 10;
static constexpr int32_t kCtrlKeyW = 80;
static constexpr int32_t kSpaceKeyW = 170;

/* Special key codes carried in user_data (real characters are > 0). */
static constexpr int kKeyShift = -1;
static constexpr int kKeyLayer = -2;
static constexpr int kKeyBackspace = -3;
static constexpr int kKeyEnter = -4;
static constexpr int kKeySpace = ' ';

enum { kLayerLower, kLayerUpper, kLayerSym1, kLayerSym2, kLayerCount };

/* 26 characters per layer, filling the rows 7/7/6/6 in reading order. */
static const char *const kLayers[kLayerCount][kCharRows] = {
    {"qwertyu", "iopasdf", "ghjklz", "xcvbnm"},
    {"QWERTYU", "IOPASDF", "GHJKLZ", "XCVBNM"},
    {"1234567", "890@#$&", "*-_+=!", "?.,;:/"},
    {"()[]{}<", ">|^`~%$", "'\"\\:;#", "&*@!?_"},
};
static const char *const kLayerKeyText[kLayerCount] = {"123", "123", "#+=", "abc"};

static ui_keyboard_cb_t s_cb;
static int s_layer;
static lv_obj_t *s_char_keys[kCharRows][kMaxRowKeys];
static lv_obj_t *s_shift_label;
static lv_obj_t *s_layer_label;

int32_t ui_keyboard_height(void)
{
    return kRowCount * kKeyH + (kRowCount - 1) * kRowGap + 2 * kFocusOutlineSlack;
}

static void apply_layer(void)
{
    for (int r = 0; r < kCharRows; r++) {
        const char *chars = kLayers[s_layer][r];
        for (int c = 0; c < kRowKeyCount[r]; c++) {
            lv_obj_t *key = s_char_keys[r][c];
            char text[2] = {chars[c], '\0'};
            lv_label_set_text(lv_obj_get_child(key, 0), text);
            lv_obj_set_user_data(key, reinterpret_cast<void *>(static_cast<intptr_t>(chars[c])));
        }
    }
    lv_label_set_text(s_shift_label, s_layer == kLayerUpper ? "abc" : "ABC");
    lv_label_set_text(s_layer_label, kLayerKeyText[s_layer]);
}

static void key_cb(lv_event_t *e)
{
    lv_obj_t *key = static_cast<lv_obj_t *>(lv_event_get_target(e));
    int code = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(key)));
    switch (code) {
    case kKeyShift:
        s_layer = (s_layer == kLayerLower) ? kLayerUpper : kLayerLower;
        apply_layer();
        break;
    case kKeyLayer:
        s_layer = (s_layer == kLayerSym1) ? kLayerSym2 : (s_layer == kLayerSym2) ? kLayerLower : kLayerSym1;
        apply_layer();
        break;
    case kKeyBackspace:
        if (s_cb.on_backspace) s_cb.on_backspace(s_cb.ctx);
        break;
    case kKeyEnter:
        if (s_cb.on_enter) s_cb.on_enter(s_cb.ctx);
        break;
    default:
        if (s_cb.on_char) s_cb.on_char(static_cast<char>(code), s_cb.ctx);
        /* One-shot shift: back to lowercase after a capital. */
        if (s_layer == kLayerUpper) {
            s_layer = kLayerLower;
            apply_layer();
        }
        break;
    }
}

static lv_obj_t *add_key(lv_obj_t *row, lv_group_t *group, const char *text, int code, int32_t width,
                         const lv_font_t *font)
{
    lv_obj_t *key = make_button(row, text);
    lv_obj_set_style_min_width(key, 0, 0);
    lv_obj_set_size(key, width, kKeyH);
    lv_obj_set_style_pad_all(key, 0, 0);
    lv_obj_set_ext_click_area(key, 0);
    if (font) lv_obj_set_style_text_font(lv_obj_get_child(key, 0), font, 0);
    lv_obj_set_user_data(key, reinterpret_cast<void *>(static_cast<intptr_t>(code)));
    lv_obj_add_event_cb(key, key_cb, LV_EVENT_CLICKED, nullptr);
    add_to_group(key, group);
    return key;
}

static lv_obj_t *make_key_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, kColGap, 0);
    return row;
}

lv_obj_t *ui_keyboard_create(lv_obj_t *parent, lv_group_t *group, const ui_keyboard_cb_t *cb)
{
    s_cb = *cb;
    s_layer = kLayerLower;
    memset(s_char_keys, 0, sizeof(s_char_keys));

    lv_obj_t *kb = lv_obj_create(parent);
    lv_obj_remove_style_all(kb);
    lv_obj_set_size(kb, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(kb, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(kb, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(kb, kFocusOutlineSlack, 0);
    lv_obj_set_style_pad_row(kb, kRowGap, 0);

    for (int r = 0; r < kCharRows; r++) {
        lv_obj_t *row = make_key_row(kb);
        for (int c = 0; c < kRowKeyCount[r]; c++) {
            s_char_keys[r][c] = add_key(row, group, "a", 'a', kKeyW, nullptr);
        }
        if (r == kCharRows - 1) {
            add_key(row, group, LV_SYMBOL_BACKSPACE, kKeyBackspace, kKeyW, &lv_font_montserrat_32);
        }
    }

    lv_obj_t *ctrl = make_key_row(kb);
    lv_obj_t *shift = add_key(ctrl, group, "ABC", kKeyShift, kCtrlKeyW, nullptr);
    s_shift_label = lv_obj_get_child(shift, 0);
    lv_obj_t *layer = add_key(ctrl, group, "123", kKeyLayer, kCtrlKeyW, nullptr);
    s_layer_label = lv_obj_get_child(layer, 0);
    add_key(ctrl, group, "space", kKeySpace, kSpaceKeyW, nullptr);
    add_key(ctrl, group, LV_SYMBOL_NEW_LINE, kKeyEnter, kCtrlKeyW, &lv_font_montserrat_32);

    apply_layer();
    return kb;
}
