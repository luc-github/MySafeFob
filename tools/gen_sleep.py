#!/usr/bin/env python3
"""Converts resources/sleep.png into a packed 1bpp monochrome bitmap
(boards/x4pro/app/sleep_bitmap.h), blitted straight into eink.c's native
framebuffer by splash.cpp's board_sleep_screen_show() -- same format and
same portrait->landscape transpose convention as splash_bitmap.h/gen_splash.py
(row-major, MSB-first, (width+7)/8 bytes/row, bit=1 -> ink/black).

Usage: python tools/gen_sleep.py resources/sleep.png \
           boards/x4pro/app/sleep_bitmap.h --width 480 --height 800
"""
import argparse
from pathlib import Path

from PIL import Image


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("src", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--width", type=int, default=480)
    ap.add_argument("--height", type=int, default=800)
    ap.add_argument("--symbol", default="sleep")
    args = ap.parse_args()

    src = Image.open(args.src).convert("RGBA")
    # Flatten alpha onto a white background (the panel only has black/white).
    bg = Image.new("RGBA", src.size, (255, 255, 255, 255))
    flat = Image.alpha_composite(bg, src).convert("L")

    # Contain (aspect ratio preserved) + center on a white W x H canvas.
    scale = min(args.width / flat.width, args.height / flat.height)
    new_size = (max(1, round(flat.width * scale)), max(1, round(flat.height * scale)))
    resized = flat.resize(new_size, Image.LANCZOS)
    canvas = Image.new("L", (args.width, args.height), 255)
    off = ((args.width - new_size[0]) // 2, (args.height - new_size[1]) // 2)
    canvas.paste(resized, off)

    # Floyd-Steinberg (PIL's default dither when converting to mode '1').
    bw = canvas.convert("1")

    stride = (args.width + 7) // 8
    packed = bytearray(stride * args.height)
    px = bw.load()
    for y in range(args.height):
        for x in range(args.width):
            # PIL mode '1': 0 = black, 255 = white. Packed format: bit=1 -> ink.
            if px[x, y] == 0:
                packed[y * stride + (x >> 3)] |= 0x80 >> (x & 7)

    lines = [
        "/**",
        " * @file sleep_bitmap.h",
        f" * @brief Deep sleep screen bitmap -- generated from {args.src.as_posix()} by",
        " *   tools/gen_sleep.py. DO NOT EDIT BY HAND: re-run the script instead.",
        " */",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        f"#define SLEEP_W {args.width}",
        f"#define SLEEP_H {args.height}",
        f"#define SLEEP_STRIDE {stride}",
        "",
        f"static const uint8_t {args.symbol}_bits[{len(packed)}] = {{",
    ]
    for i in range(0, len(packed), 16):
        chunk = ", ".join(f"0x{b:02X}" for b in packed[i : i + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines), encoding="utf-8")
    print(f"{args.out} : {args.width}x{args.height}, {len(packed)} bytes")


if __name__ == "__main__":
    main()
