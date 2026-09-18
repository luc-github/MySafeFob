# Pattern: build/flash tooling

**Source**: PiBot's `installer/flash_mgr.py` + its per-board `build_one.py`-
style build scripts (multi-board, multi-CNC-target).
**MySafeFob's re-implementation**: `tools/flash_scripts/flash_mgr.py`,
`tools/build_scripts/build_mgr.py`, `boards/x4pro/cmake/postbuild.cmake`
(+ `boards/x4pro/factory/cmake/`).

## The problem

A project with more than one buildable variant (app vs. factory, or
multiple boards) accumulates ad-hoc build/flash commands fast — a README
full of `idf.py -p COMx -D...` invocations that drift out of date the
moment a partition offset changes, and a flashing procedure that only the
person who last touched it remembers correctly.

## The generic core (copy this)

1. **A JSON flash map is the single source of truth**, generated fresh
   from each build's *actual* partition table — never hand-maintained
   offsets. PiBot's `flash_mgr.py` docstring is explicit about why: a
   board with a differently-sized OTA slot silently breaks any hardcoded
   offset table, so the script always prefers a parsed
   `partitions_<N>mb.bin` over its own fallback constants. Same principle
   in MySafeFob's `flash_mgr.py`.
2. **The bootloader's flash offset is chip-dependent, look it up, don't
   assume `0x1000`.** A specific, easy-to-get-wrong trap both projects
   documented independently: classic ESP32 flashes its bootloader at
   `0x1000`; the S3 (and most later chips) at `0x0`. Flashing an S3 at
   `0x1000` produces a boot loop with an "invalid header" error that
   looks like corruption, not an offset mistake — worth a comment at the
   exact line that picks the offset, in every project, forever.
3. **A postbuild step copies build artifacts into a dedicated `installer/`
   tree**, one subfolder per buildable variant, alongside its JSON flash
   map — never flash directly out of `build/`. This is what lets the
   flash script (and a possible future web installer) stay agnostic to
   *how* something was built, only *what's* in `installer/<variant>/`.
4. **The flashing CLI is interactive with memory**: run with no
   arguments, it lists variants/ports and remembers the last choice
   (Enter reruns it); every flag is also scriptable directly for CI or
   muscle-memory use. Removes the "which exact command was it" friction
   without removing the ability to script it.
5. **Factory artifacts (bootloader hook, factory binary) are copied, not
   rebuilt, by the app's own postbuild step** — and the copy step must
   explicitly verify freshness or a stale bootloader silently ships
   unnoticed (see `factory-recovery.md`'s pitfall list, and MySafeFob's
   `FACTORY.md` §3.4 for the exact bug this caused once already).

## What's project-specific (tune, don't copy blindly)

- **Variant matrix size**: PiBot's `flash_mgr.py` handles a genuine
  many-board, many-CNC-target matrix. MySafeFob has exactly two variants
  per board today (`<board>_app`, `<board>_factory`). Don't build
  matrix-selection UI for a project that doesn't have a matrix yet — the
  JSON-flash-map-as-source-of-truth principle scales down to two variants
  fine without the extra machinery PiBot needs for a dozen.
- **Web installer consumption**: PiBot's JSON map is also read by a web
  flashing tool (ESP Web Tools style). MySafeFob's is currently only
  consumed by its own `flash_mgr.py` — the format is deliberately kept
  compatible with that possible future use (per MySafeFob's own
  `README.md`), but nothing about a web installer should be built until
  actually needed.
- **Build orchestration depth**: PiBot's per-board build scripts handle
  a much larger `sanity_check.cmake`-style feature-flag validation matrix
  (WiFi/BT mutual exclusion, socket client/server exclusivity). A simpler
  project's `build_mgr.py` equivalent doesn't need that validation layer
  until it actually has mutually-exclusive build options to protect
  against.

## Reference implementation to read

`tools/flash_scripts/flash_mgr.py` in this repo, specifically its header
docstring and the `_BOOTLOADER_OFFSET_BY_CHIP` table — both directly
inherited from PiBot's version, already trimmed to what a
single/few-board project needs.
