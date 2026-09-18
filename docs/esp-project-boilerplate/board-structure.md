# Pattern: board-agnostic skeleton

**Source**: PiBot's `boards/<board>/` tree (12+ boards, each with its own
`Factory/`, display/touch/SD driver set, `sdkconfig.defaults`).
**MySafeFob's re-implementation**: ADR-008 in `docs/ROADMAP.md`,
`boards/x4pro/` (real, hardware-validated) + `boards/m5paper_mono/`
(documentation-only stub for a likely future board).

## The problem

A project that might ever run on more than one board needs to decide,
from the start, where the line is between "application code" and "board
code" — get it wrong early and every later board port either duplicates
the whole application or fights a leaky abstraction that assumed the
first board's hardware everywhere.

## The generic core (copy this)

1. **`boards/<name>/` owns everything hardware-dependent**: pin
   definitions, display/touch/storage drivers, its own `Factory`/recovery
   variant (see `factory-recovery.md`), its own `sdkconfig.defaults`.
   Application code (business logic, UI logic) never `#include`s a board
   header directly — it goes through a `board_config.h`-style interface
   the board provides.
2. **Board selection is a single build-time switch**
   (`-DMSF_BOARD=x4pro` in MySafeFob, an equivalent per-board CMake
   target in PiBot) that pulls in that board's `board_config.cmake`,
   which in turn adds that board's source directory and defines. Adding a
   board never means branching application code on a board enum at
   runtime — it means adding a new `boards/<name>/` directory.
3. **Duplication between boards is accepted over a shared core that could
   break multiple boards at once.** Both projects state this explicitly
   as a deliberate trade-off, not an oversight: a display driver quirk
   fixed for board A must never risk board B's already-validated
   behavior. This applies doubly to the Factory variant (see
   `factory-recovery.md`) — it's the safety net, breaking it for two
   boards at once because they shared code is the one regression that
   can't be recovered from in the field.
4. **A documentation-only stub is a legitimate first step for a future
   board** — MySafeFob's `boards/m5paper_mono/` is spec-only (pins,
   expected differences from `x4pro`) until real hardware exists,
   deliberately not code that would be untested and misleading. Worth
   doing even for a project's first *actual* second board: write down
   what's expected to differ before writing the driver, so the
   probe-first hardware-bringup habit (`hardware-bringup.md`) has
   something to confirm or correct against.

## What's project-specific (tune, don't copy blindly)

- **How much boards actually share**: PiBot's boards mostly share the
  same CNC/UI application layer behind wildly different displays; the
  boundary is almost entirely at the driver level. A project whose boards
  differ more fundamentally (e.g. one with a touchscreen, one with none)
  needs the `board_config.h` interface to expose capability flags, not
  just pin numbers — check what MySafeFob's `InputStyle`/`hasTouch`-style
  fields would need to look like before assuming a pin-only interface is
  enough.
- **Variant matrix per board**: PiBot's `boards/<board>/` each build
  several CNC-target variants from the same board tree (feature-flag
  driven). MySafeFob's each board builds exactly two variants (`app`,
  `factory`). Don't build feature-flag variant selection into a board's
  CMake before a project actually has more than one variant per board
  needing it.

## Reference implementation to read

`docs/ROADMAP.md`'s ADR-008 in this repo — short, and explicit about
which parts of the decision were "inspired by PiBot's board layout,
simplified (no variant matrix)."
