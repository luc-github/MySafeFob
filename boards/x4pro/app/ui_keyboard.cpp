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
 * @brief MySafeFob App — on-screen keyboard, see ui_keyboard.h.
 *        Full mode: four character rows (7/7/6/6 keys, the last one followed
 *        by the backspace key) plus a bottom control row: fewer keys per row
 *        than a classic 10-key QWERTY, so keys are wider and the gaps
 *        between them larger (touch precision). Base32 and numeric modes
 *        are fixed layouts (no layers) on the same 5-row grid.
 */
#include "ui_keyboard.h"
#include "ui_widgets.h"

#include <cstring>

static constexpr int kCharRows = 4;
static constexpr int kMaxRowKeys = 7;
static constexpr int kRowKeyCount[kCharRows] = {7, 7, 6, 6};
static constexpr int kRowCount = kCharRows + 1; /* + bottom control row */

static constexpr int32_t kKeyW = 54;
/* 56, not 50 (2026-10-09): make_button()'s min height (kMinTouchTarget)
 * always won over the 50 set here, so the keys were 56px all along. The
 * touch mapping was validated on that real 56px grid. */
static constexpr int32_t kKeyH = 56;
static constexpr int32_t kColGap = 10;
/* Full row width (7 keys of kKeyW), used to size the wider fixed-layout keys. */
static constexpr int32_t kRowWidth = kMaxRowKeys * kKeyW + (kMaxRowKeys - 1) * kColGap;
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

/* Per-instance state, owned by the keyboard container (freed on its
 * LV_EVENT_DELETE) and passed to every key callback as event user data. */
struct KeyboardState {
    ui_keyboard_cb_t cb;
    int layer;
    lv_obj_t *char_keys[kCharRows][kMaxRowKeys];
    lv_obj_t *shift_label;
    lv_obj_t *layer_label;
};

/* Fixed layouts (Base32, numeric), one string per row: '\b' = backspace,
 * '\n' = enter, any other byte = that character. */
static const char *const kBase32Rows[kRowCount] = {"QWERTYU", "IOPASDF", "GHJKLZ", "XCVBNM\b", "234567\n"};
static const char *const kNumericRows[kRowCount] = {"123", "456", "789", "0\b", "\n"};

int32_t ui_keyboard_height(void)
{
    return kRowCount * kKeyH + (kRowCount - 1) * kRowGap + 2 * kFocusOutlineSlack;
}

static void apply_layer(KeyboardState *kb)
{
    if (!kb->shift_label) return;   /* fixed layout: no layers */
    for (int r = 0; r < kCharRows; r++) {
        const char *chars = kLayers[kb->layer][r];
        for (int c = 0; c < kRowKeyCount[r]; c++) {
            lv_obj_t *key = kb->char_keys[r][c];
            char text[2] = {chars[c], '\0'};
            lv_label_set_text(lv_obj_get_child(key, 0), text);
            lv_obj_set_user_data(key, reinterpret_cast<void *>(static_cast<intptr_t>(chars[c])));
        }
    }
    lv_label_set_text(kb->shift_label, kb->layer == kLayerUpper ? "abc" : "ABC");
    lv_label_set_text(kb->layer_label, kLayerKeyText[kb->layer]);
}

static void key_cb(lv_event_t *e)
{
    KeyboardState *kb = static_cast<KeyboardState *>(lv_event_get_user_data(e));
    lv_obj_t *key = static_cast<lv_obj_t *>(lv_event_get_target(e));
    int code = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(key)));
    switch (code) {
    case kKeyShift:
        kb->layer = (kb->layer == kLayerLower) ? kLayerUpper : kLayerLower;
        apply_layer(kb);
        break;
    case kKeyLayer:
        kb->layer = (kb->layer == kLayerSym1) ? kLayerSym2 : (kb->layer == kLayerSym2) ? kLayerLower : kLayerSym1;
        apply_layer(kb);
        break;
    case kKeyBackspace:
        if (kb->cb.on_backspace) kb->cb.on_backspace(kb->cb.ctx);
        break;
    case kKeyEnter:
        if (kb->cb.on_enter) kb->cb.on_enter(kb->cb.ctx);
        break;
    default:
        if (kb->cb.on_char) kb->cb.on_char(static_cast<char>(code), kb->cb.ctx);
        /* One-shot shift: back to lowercase after a capital. */
        if (kb->layer == kLayerUpper) {
            kb->layer = kLayerLower;
            apply_layer(kb);
        }
        break;
    }
}

static void keyboard_delete_cb(lv_event_t *e)
{
    delete static_cast<KeyboardState *>(lv_event_get_user_data(e));
}

static lv_obj_t *add_key(KeyboardState *kb, lv_obj_t *row, lv_group_t *group, const char *text, int code,
                         int32_t width, const lv_font_t *font)
{
    lv_obj_t *key = make_button(row, text);
    lv_obj_set_style_min_width(key, 0, 0);
    lv_obj_set_size(key, width, kKeyH);
    lv_obj_set_style_pad_all(key, 0, 0);
    lv_obj_set_ext_click_area(key, 0);
    if (font) lv_obj_set_style_text_font(lv_obj_get_child(key, 0), font, 0);
    lv_obj_set_user_data(key, reinterpret_cast<void *>(static_cast<intptr_t>(code)));
    lv_obj_add_event_cb(key, key_cb, LV_EVENT_CLICKED, kb);
    add_to_group(key, group);
    return key;
}

static void row_ext_draw_cb(lv_event_t *e)
{
    lv_event_set_ext_draw_size(e, kFocusOutlineSlack);
}

static lv_obj_t *make_key_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, kColGap, 0);
    /* Let a key's focus ring (7px past the key, add_to_group()) draw past
     * the row's own box (2026-10-09 bug report: "the key does not show
     * focus"). LVGL clips children to their parent and this row wraps its
     * keys exactly, so only thin side slivers of the ring were left.
     * Padding the row instead would make the keyboard taller and move the
     * key positions validated against the touch mapping (touch.c); the
     * 10px row gap is enough room for the ring.
     * OVERFLOW_VISIBLE alone is not enough: LVGL only lets children draw up
     * to the row's own ext draw size past its box (lv_refr.c,
     * lv_obj_pos.c), which is 0 for a plain container -- the outer side of
     * the first and last key's ring stayed clipped. row_ext_draw_cb()
     * declares room for the ring. */
    lv_obj_add_flag(row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_event_cb(row, row_ext_draw_cb, LV_EVENT_REFR_EXT_DRAW_SIZE, nullptr);
    lv_obj_refresh_ext_draw_size(row);
    return row;
}

/* Base32 / numeric: one key per byte of the row strings; Base32 keeps the
 * full keyboard's key width (same columns), numeric widens its few keys to
 * fill the row. */
static void build_fixed(KeyboardState *kb, lv_obj_t *cont, lv_group_t *group, const char *const *rows, bool fill)
{
    for (int r = 0; r < kRowCount; r++) {
        lv_obj_t *row = make_key_row(cont);
        int n = static_cast<int>(strlen(rows[r]));
        int32_t width = fill ? (kRowWidth - (n - 1) * kColGap) / n : kKeyW;
        for (int c = 0; c < n; c++) {
            char ch = rows[r][c];
            if (ch == '\b') {
                add_key(kb, row, group, LV_SYMBOL_BACKSPACE, kKeyBackspace, width, &lv_font_montserrat_32);
            } else if (ch == '\n') {
                add_key(kb, row, group, LV_SYMBOL_NEW_LINE, kKeyEnter, width, &lv_font_montserrat_32);
            } else {
                char text[2] = {ch, '\0'};
                add_key(kb, row, group, text, ch, width, fill ? &lv_font_montserrat_32 : nullptr);
            }
        }
    }
}

lv_obj_t *ui_keyboard_create(lv_obj_t *parent, lv_group_t *group, const ui_keyboard_cb_t *cb,
                             ui_keyboard_mode_t mode)
{
    KeyboardState *kb = new KeyboardState();
    kb->cb = *cb;
    kb->layer = kLayerLower;

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cont, kFocusOutlineSlack, 0);
    lv_obj_set_style_pad_row(cont, kRowGap, 0);
    lv_obj_add_event_cb(cont, keyboard_delete_cb, LV_EVENT_DELETE, kb);

    if (mode != UI_KEYBOARD_FULL) {
        build_fixed(kb, cont, group, mode == UI_KEYBOARD_BASE32 ? kBase32Rows : kNumericRows,
                    mode == UI_KEYBOARD_NUMERIC);
        return cont;
    }

    for (int r = 0; r < kCharRows; r++) {
        lv_obj_t *row = make_key_row(cont);
        for (int c = 0; c < kRowKeyCount[r]; c++) {
            kb->char_keys[r][c] = add_key(kb, row, group, "a", 'a', kKeyW, nullptr);
        }
        if (r == kCharRows - 1) {
            add_key(kb, row, group, LV_SYMBOL_BACKSPACE, kKeyBackspace, kKeyW, &lv_font_montserrat_32);
        }
    }

    lv_obj_t *ctrl = make_key_row(cont);
    lv_obj_t *shift = add_key(kb, ctrl, group, "ABC", kKeyShift, kCtrlKeyW, nullptr);
    kb->shift_label = lv_obj_get_child(shift, 0);
    lv_obj_t *layer = add_key(kb, ctrl, group, "123", kKeyLayer, kCtrlKeyW, nullptr);
    kb->layer_label = lv_obj_get_child(layer, 0);
    add_key(kb, ctrl, group, "space", kKeySpace, kSpaceKeyW, nullptr);
    add_key(kb, ctrl, group, LV_SYMBOL_NEW_LINE, kKeyEnter, kCtrlKeyW, &lv_font_montserrat_32);

    apply_layer(kb);
    return cont;
}
