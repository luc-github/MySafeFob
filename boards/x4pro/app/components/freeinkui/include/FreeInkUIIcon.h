#pragma once

// LOCAL FIX (2026-09-18, MySafeFob): the two lines below had a trailing
// backslash, which GCC's -Wcomment (escalated to error by this project's
// -Werror=all) flags as an accidental multi-line // comment. Only
// touched because this header was never actually #include'd before
// wiring in the battery status icons (ADR-014) — if this component is
// ever re-vendored from upstream freeink-sdk, re-check whether upstream
// fixed the same thing.
//
// FreeInk SDK — freeink::Icon → FreeInkUI BitmapRef bridge (opt-in).
//
// The Icons library (libs/assets/Icons) generates crisp 1-bpp icons at any
// size from Lucide SVGs via tools/gen_icons.py:
//
//   gen_icons.py --manifest icons.txt --svgdir libs/assets/Icons/lucide/icons
//                --sizes 16,22,24,48 --out src/icons_gen.h
//
// This header adapts those structs to the BitmapRef every FreeInkUI component
// takes (Mask1 = the Icon convention, bit 0 = draw). Opt-in like
// FreeInkUIGfxRenderer.h: only compilable in firmwares that also add the
// Icons library to lib_deps.

#include <Icon.h>

#include "FreeInkUI.h"

namespace freeink {
namespace ui {

inline BitmapRef bitmapFromIcon(const Icon& icon) {
  BitmapRef ref;
  ref.data = icon.bits;
  ref.width = icon.w;
  ref.height = icon.h;
  ref.format = BitmapFormat::Mask1;
  return ref;
}

}  // namespace ui
}  // namespace freeink
