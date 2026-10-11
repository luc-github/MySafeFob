#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_fonts.py — regenerates the app's LVGL fonts (boards/x4pro/app/fonts/).

The app does not use LVGL's built-in Montserrat 24/32: it uses its own
msf_font_24 / msf_font_32, generated with exactly the options of the built-in
ones (LVGL scripts/built_in_font/built_in_font_gen.py: Montserrat-Medium,
0x20-0x7F + 0xB0 + 0x2022, the LV_SYMBOL_* Font Awesome glyphs, 4 bpp,
uncompressed) plus the extra Font Awesome icons listed in EXTRA_SYMBOLS
and writes their UTF-8 macros to boards/x4pro/app/msf_symbols.h (one source
of truth: to add an icon, add a line to EXTRA_SYMBOLS and run this script).
Same approach as the PiBot firmware: the generated files are committed, this
script is only run when a glyph is added (then rebuild).

The Font Awesome file is LVGL's FA5 (Solid+Brands+Regular merged): FA5 glyph
names and code points (FA6 renamed many: circle-info = info-circle...), and
one glyph per code point, the solid one when both exist. Outline ("regular")
icons whose code point is solid there come from Font Awesome Free 5.15.4's
own fa-regular-400 instead (REGULAR_SYMBOLS, resources/fonts/; SIL OFL 1.1,
see README credits). A code point is taken from one file only.

Uncompressed on purpose: splash.cpp draws the sleep-screen text straight from
the font tables (fmt_txt PLAIN bitmaps), without LVGL.

Needs Node.js; lv_font_conv is fetched by npx (pinned version).
Usage:  python tools/gen_fonts.py
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LVGL_FONTS = os.path.join(ROOT, "managed_components", "lvgl__lvgl", "scripts", "built_in_font")
OUT_DIR = os.path.join(ROOT, "boards", "x4pro", "app", "fonts")
LV_FONT_CONV = "lv_font_conv@1.5.2"

SIZES = [24, 32]
TEXT_RANGE = "0x20-0x7F,0xB0,0x2022"
# LVGL's built-in symbol list (built_in_font_gen.py), keeps every LV_SYMBOL_*.
LVGL_SYMBOLS = ("61441,61448,61451,61452,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,"
                "61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,"
                "61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,"
                "61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,"
                "63650")
# Extra Font Awesome 5 icons: code point -> (macro suffix, FA5 name).
EXTRA_SYMBOLS = {
    0xF004: ("HEART", "heart: symbol keypad 8"),
    0xF005: ("STAR", "star: symbol keypad 0"),
    0xF002: ("SEARCH", "search (magnifying glass): scan buttons"),
    0xF007: ("USER", "user: login"),
    0xF017: ("CLOCK", "clock: Time, TOTP"),
    0xF023: ("LOCK", "lock"),
    0xF042: ("ADJUST", "adjust: frontlight color"),
    0xF059: ("QUESTION", "question-circle: confirmation title (not critical)"),
    0xF05B: ("CROSSHAIRS", "crosshairs: touch calibration"),
    0xF05A: ("INFO", "info-circle: About"),
    0xF084: ("KEY", "key: Security, PIN change, password"),
    0xF0EB: ("LIGHTBULB", "lightbulb: Display, frontlight"),
    0xF14A: ("CHECK_SQUARE", "check-square (solid): checked box"),
    0xF12D: ("ERASER", "eraser: Clear"),
    0xF118: ("SMILE", "smile: symbol keypad 2"),
    0xF185: ("SUN", "sun: brighter"),
    0xF186: ("MOON", "moon: dimmer, auto-sleep"),
    0xF1CD: ("LIFE_RING", "life-ring: recovery codes"),
    0xF1DE: ("SLIDERS", "sliders-h: Controls"),
    0xF249: ("NOTE", "sticky-note: note"),
    0xF292: ("HASHTAG", "hashtag: account PIN"),
    0xF2DC: ("SNOWFLAKE", "snowflake: symbol keypad 7"),
    0xF2FE: ("POO", "poo: symbol keypad 3"),
    0xF2C2: ("ID_CARD", "id-card: account name"),
    0xF2EA: ("ROTATE_LEFT", "undo-alt (FA6 rotate-left): restart the calibration"),
    0xF3C1: ("UNLOCK", "lock-open"),
    0xF3CD: ("MOBILE", "mobile-alt: Device"),
    0xF3E5: ("BACK", "reply: Back (not NEW_LINE, the keyboard's Enter)"),
    0xF443: ("CHESS_PAWN", "chess-pawn: symbol keypad 9"),
    0xF4FF: ("USER_EDIT", "user-edit: owner info"),
    0xF521: ("CROWN", "crown: symbol keypad 5"),
    0xF56E: ("FILE_EXPORT", "file-export: SD export"),
    0xF56F: ("FILE_IMPORT", "file-import: SD import"),
}
# Outline icons from Font Awesome Free 5.15.4 Regular (same FA5 grid as the
# solid ones above, so they line up): code point -> (macro suffix, name).
REGULAR_SYMBOLS = {
    0xF0C8: ("SQUARE", "square (regular, outline): unchecked box"),
}
# resources/fonts/ also keeps fa-solid-900 / fa-brands-400, should a future
# icon need the split files.
FA_REGULAR = os.path.join(ROOT, "resources", "fonts", "fa-regular-400.woff")
SYMBOLS_H = os.path.join(ROOT, "boards", "x4pro", "app", "msf_symbols.h")
LICENSE_HEADER = """/*
 Project: MySafeFob  msf_symbols.h
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
"""


def write_symbols_h():
    lines = [LICENSE_HEADER,
             "/**\n * @file msf_symbols.h\n * @brief MySafeFob App — extra Font Awesome 5 icons, beyond LVGL's\n"
             " *        LV_SYMBOL_*, as UTF-8 strings. GENERATED by tools/gen_fonts.py\n"
             " *        (EXTRA_SYMBOLS, REGULAR_SYMBOLS), do not edit. Only usable with the app's fonts\n"
             " *        msf_font_24 / msf_font_32.\n */\n",
             "#pragma once\n\n"]
    for cp, (macro, desc) in sorted({**EXTRA_SYMBOLS, **REGULAR_SYMBOLS}.items()):
        utf8 = "".join(f"\\x{b:02X}" for b in chr(cp).encode("utf-8"))
        lines.append(f'#define MSF_SYMBOL_{macro:<13} "{utf8}"   /* U+{cp:04X} {desc} */\n')
    with open(SYMBOLS_H, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(lines))
    print(f"-> {os.path.relpath(SYMBOLS_H, ROOT)}")


def generate(size):
    out = os.path.join(OUT_DIR, f"msf_font_{size}.c")
    symbols = LVGL_SYMBOLS + "," + ",".join(str(cp) for cp in EXTRA_SYMBOLS)
    cmd = ["npx", "-y", LV_FONT_CONV, "--no-compress", "--no-prefilter", "--bpp", "4", "--size", str(size),
           "--font", os.path.join(LVGL_FONTS, "Montserrat-Medium.ttf"), "-r", TEXT_RANGE,
           "--font", os.path.join(LVGL_FONTS, "FontAwesome5-Solid+Brands+Regular.woff"), "-r", symbols,
           "--font", FA_REGULAR, "-r", ",".join(str(cp) for cp in REGULAR_SYMBOLS),
           "--format", "lvgl", "--force-fast-kern-format", "--lv-include", "lvgl.h", "-o", out]
    # The font's C name comes from the output file name: msf_font_<size>.
    print(" ".join(cmd))
    subprocess.run(cmd, check=True, shell=(os.name == "nt"))
    with open(out, encoding="utf-8") as f:
        text = f.read()
    # No machine-specific paths in the committed file's "Opts:" comment.
    text = (text.replace(LVGL_FONTS + os.sep, "").replace(OUT_DIR + os.sep, "")
            .replace(os.path.dirname(FA_REGULAR) + os.sep, ""))
    # lv_font_conv 1.5.2 writes LVGL 7/8 code: LV_VERSION_CHECK() no longer
    # exists in LVGL 9 (the guards fall to the v7 branch: non-const font,
    # "conflicting type qualifiers"), and LVGL 9 has no glyph cache field.
    # Rewrite to the exact form of LVGL 9's own built-in fonts.
    fixes = [
        ("#if LV_VERSION_CHECK(8, 0, 0)\n/*Store all the custom data of the font*/\n"
         "static  lv_font_fmt_txt_glyph_cache_t cache;\nstatic const",
         "#if LVGL_VERSION_MAJOR >= 8\n/*Store all the custom data of the font*/\nstatic const"),
        ("#if LV_VERSION_CHECK(8, 0, 0)\n    .cache = &cache\n#endif\n", ""),
        ("#if LV_VERSION_CHECK(8, 0, 0)\nconst lv_font_t", "#if LVGL_VERSION_MAJOR >= 8\nconst lv_font_t"),
    ]
    for old, new in fixes:
        if old not in text:
            sys.exit(f"unexpected lv_font_conv output, cannot apply the LVGL 9 fix: {old!r}")
        text = text.replace(old, new)
    header = (f"/* GENERATED by tools/gen_fonts.py, do not edit: Montserrat-Medium {size}px + LVGL symbols"
              f" + {len(EXTRA_SYMBOLS) + len(REGULAR_SYMBOLS)} extra Font Awesome icons (see msf_symbols.h). */\n")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(header + text)
    print(f"-> {os.path.relpath(out, ROOT)}")


def main():
    if not os.path.isdir(LVGL_FONTS):
        sys.exit(f"LVGL font sources not found: {LVGL_FONTS} (build once so the component manager fetches LVGL)")
    os.makedirs(OUT_DIR, exist_ok=True)
    for size in SIZES:
        generate(size)
    write_symbols_h()


if __name__ == "__main__":
    main()
