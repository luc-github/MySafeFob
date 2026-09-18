# Pattern: hardware bring-up methodology

**Source**: PiBot's 12+ board ports (memory index entries under "Board
Ports / Factory Rollout" — `esp32s3_8048s043c`, `_8048s050c`, `_8048s070c`,
`_hmi43v3`, `esp32_2432s028r`, `_3248s035c/r`, and more), each documented
as a standalone project memory note with its own bug list.
**MySafeFob's re-implementation**: `docs/hardware-specs.md`,
`docs/ROADMAP.md`'s ADRs (each hardware decision recorded with its
measured evidence), the 2026-09-16 power/factory post-mortem
(`docs/postmortem-power-factory-2026-09-16.md`).

## The problem

Bringing up a new board (or even re-validating an existing one after a
firmware change) produces a stream of small, easily-forgotten discoveries
— a register order that hangs the panel if reversed, a GPIO that's
secretly a strapping pin, a timing value that "should" work per the
datasheet but doesn't on this exact part revision. Without a habit of
capturing *why* a value is what it is, the next session re-discovers the
same bug, or worse, "simplifies" a value that looks arbitrary and
reintroduces it.

## The generic core (copy this)

1. **Probe first, assume nothing.** Both projects validate a board with a
   minimal, throwaway probe app (raw register pokes, pin sweeps, chip-ID
   reads) before writing the real driver — RE'd guesses from a datasheet
   or a similar chip's driver get corrected by what the actual silicon
   does. MySafeFob's `x4pro-probe` and PiBot's per-board bring-up sessions
   both follow this; skipping straight to "write the driver from the
   datasheet" is where both projects' worst bugs came from.
2. **One `hw_config.h` (or `BoardConfig`) per board, nothing shared unless
   proven identical.** Pins, timing constants, calibration values — each
   board gets its own copy. A shared header invites "this should be the
   same on every board" assumptions that datasheets and even the same
   chip family routinely violate.
3. **Every non-obvious constant gets a comment citing how it was found**
   ("measured 12:18", "4-corner test 00:55", "confirmed on hardware
   2026-09-13") — not the value's meaning (which the name already says)
   but *how confident to be in it* and *what would invalidate it*. This
   is the single habit that most differentiates both projects' hardware
   docs from a typical driver: a constant with no provenance looks
   editable; one with a citation looks load-bearing.
4. **Validate, then document — in that order, in the same session.**
   Both projects' ADR/memory-note style (context → decision → evidence →
   status) means a decision is never recorded as final until it's been
   seen working on real hardware, and the record says which.
5. **A recurring-bug checklist grows with every board.** PiBot's memory
   index is, in effect, this: wrong UART port reused across three boards,
   a PCLK value copied from a template and never re-measured, a hit-test
   region computed from the wrong constant repeated across two ports.
   Each was caught once, then *searched for explicitly* on the next
   board rather than re-discovered by accident. Keep a list; check it
   before declaring a new board's bring-up done.

## What's project-specific (tune, don't copy blindly)

- **Board count and catalog depth**: PiBot's methodology had to survive
  12+ boards with wildly different display controllers (RGB parallel vs.
  SPI), touch chips, and GPIO expanders — its bug list is proportionally
  large. MySafeFob has one board in depth today (a second stubbed,
  ADR-008) — don't manufacture a large recurring-bug list before there's
  a second real board to compare against; the *habit* of keeping one
  matters more than its current length.
- **What "probe" means concretely**: PiBot's probes are per-chip-family
  (a display-controller probe, a touch-chip probe). MySafeFob's
  `x4pro-probe` is a single combined probe for one board's whole
  peripheral set. Scale the probe granularity to how many boards/chip
  families actually need distinguishing.
- **Where the record lives**: PiBot uses per-board memory notes (this
  session's own `.claude` memory system) plus `docs/codewiki/`.
  MySafeFob uses ADRs inline in `docs/ROADMAP.md` plus dedicated docs
  (`hardware-specs.md`, the power/factory post-mortem). Either is fine —
  what matters is that *a* durable, searchable record exists per
  hardware decision, not which file format holds it.

## Reference implementation to read

`docs/ROADMAP.md`'s ADR-009 (deep sleep/wake, including its five
amendments from a single hard debugging session) is the clearest example
in this repo of the pattern working end to end: each amendment is a bug,
its root cause, the fix, and how it was confirmed — not just the final
answer with the false starts erased.
