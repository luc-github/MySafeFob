#!/usr/bin/env python3
"""Convertit resources/splash.png en bitmap 1bpp BW1 (freeink::ui::BitmapRef)
pour le splash factory (boards/x4pro/factory/main/splash_bitmap.h).

Format BW1 attendu par FreeInkUIDisplayTarget::bitmap() : row-major,
MSB-first, (width+7)/8 octets/ligne, bit=1 -> encre (noir), bit=0 -> blanc
("set-bit-is-ink", cf. FreeInkUICore.h struct BitmapRef / DisplayTarget.h).

Usage: python tools/gen_splash.py resources/splash.png \
           boards/x4pro/factory/main/splash_bitmap.h --width 480 --height 800
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
    ap.add_argument("--symbol", default="splash")
    args = ap.parse_args()

    src = Image.open(args.src).convert("RGBA")
    # Aplatit l'alpha sur fond blanc (le panneau n'a que blanc/noir).
    bg = Image.new("RGBA", src.size, (255, 255, 255, 255))
    flat = Image.alpha_composite(bg, src).convert("L")

    # Contain (aspect ratio preserve) + centrage sur canvas blanc W x H.
    scale = min(args.width / flat.width, args.height / flat.height)
    new_size = (max(1, round(flat.width * scale)), max(1, round(flat.height * scale)))
    resized = flat.resize(new_size, Image.LANCZOS)
    canvas = Image.new("L", (args.width, args.height), 255)
    off = ((args.width - new_size[0]) // 2, (args.height - new_size[1]) // 2)
    canvas.paste(resized, off)

    # Floyd-Steinberg (dither par defaut de PIL en mode '1').
    bw = canvas.convert("1")

    stride = (args.width + 7) // 8
    packed = bytearray(stride * args.height)
    px = bw.load()
    for y in range(args.height):
        for x in range(args.width):
            # PIL mode '1' : 0 = noir, 255 = blanc. BW1 : bit=1 -> encre.
            if px[x, y] == 0:
                packed[y * stride + (x >> 3)] |= 0x80 >> (x & 7)

    lines = [
        "/**",
        " * @file splash_bitmap.h",
        f" * @brief Splash factory — genere depuis {args.src.as_posix()} par",
        " *   tools/gen_splash.py. NE PAS EDITER A LA MAIN : relancer le script.",
        " */",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        f"#define SPLASH_W {args.width}",
        f"#define SPLASH_H {args.height}",
        f"#define SPLASH_STRIDE {stride}",
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
    print(f"{args.out} : {args.width}x{args.height}, {len(packed)} octets")


if __name__ == "__main__":
    main()
