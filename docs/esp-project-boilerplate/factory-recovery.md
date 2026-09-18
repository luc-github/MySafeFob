# Pattern: bootloader-hook factory/recovery partition

**Source**: PiBot's custom bootloader hook + Factory app
(`references/Luc-Pibot-cnc-pendant-firmware/docs/Factory/
bootloader_technical_doc.md`, `factory_app_technical_doc.md`,
`main/core/commands/esp444.cpp`, one `Factory/` per board under
`boards/<board>/Factory/`).
**MySafeFob's re-implementation**: ADR-007/ADR-009/ADR-011 in
`docs/ROADMAP.md`, `docs/FACTORY.md`, `boards/x4pro/factory/`.

## The problem

A device with dual-slot OTA (or even a single app slot) still needs a
way to recover when both/the-one slot is bad and there's no PC around —
or, just as often during development, a fast, reliable "go back to a
known-good menu" that doesn't depend on the main firmware being sane.

## The generic core (copy this)

1. **A separate `factory` partition, no otadata entry of its own** — it's
   the fallback the standard ESP-IDF bootloader picks when otadata is
   empty/invalid, not a slot the OTA machinery ever targets directly.
2. **The trigger backs up otadata, erases it, resets** — never tries to
   jump to the factory partition directly from the bootloader hook (both
   projects tried and abandoned direct MMU/cache jumps — see PiBot's
   `bootloader_technical_doc.md` §"Why This Approach" for the specific
   failure modes; not worth re-discovering). Let the standard bootloader's
   own boot-selection logic do the work after otadata is empty.
3. **The factory app restores otadata from the backup on its own startup,
   first thing** — a power-off from the factory (or explicit "boot
   app0") returns to the correct OTA app, not a fixed default.
4. **Two trigger paths, sharing the exact same backup/erase code path**:
   one in the bootloader hook itself (`bootloader_after_init()` —
   available even if the main app is crashed/hung, since it runs before
   any app code), one callable from the running app (for a menu item /
   serial command / long-press — convenient, but *never* the only path,
   precisely because an app-only trigger can't save you from a dead app).
5. **The backup sector's address is a shared contract** between the
   bootloader hook, the factory app, and (if present) the app-side
   trigger — same offset, same magic-marker convention, duplicated in
   each but must stay byte-identical. Constraints for choosing it: after
   the bootloader's own flash footprint, before the partition table,
   outside every declared partition (or the IDF dangerous-write
   protection aborts), 4 KB aligned.
6. **Factory lives under its board** (`boards/<board>/factory/` or
   `Factory/`), not shared across boards — it depends on that board's own
   display/touch/SD drivers, and letting boards share a factory core risks
   one board's change breaking another's recovery path, which is exactly
   the one thing that must never break.
7. **Factory is updated only via USB/serial, never self-service in the
   field** — both projects independently arrived at this after nearly
   shipping a "factory updates itself from SD" action that would erase
   and rewrite the *very partition it's executing from* (XIP), with zero
   recovery net if interrupted mid-flight. If a factory self-update
   feature is ever tempting, read MySafeFob's `ROADMAP.md` ADR-011
   amendment (2026-09-15) for the exact reasoning it was removed after
   already being implemented.

## What's project-specific (tune, don't copy blindly)

- **Trigger gesture**: PiBot uses a dedicated button (BTN3) at power-on.
  MySafeFob uses a duration threshold on its one Power button (≥10s,
  ADR-009) — chosen because MySafeFob's board has no free non-strapping
  GPIO for a dedicated recovery button once the wake-source pin is
  accounted for. Pick whatever this project's actual button/pin budget
  allows; don't assume a spare button exists.
- **Single vs. dual OTA app slot**: PiBot keeps app0/app1 for A/B
  rollback. MySafeFob removed the second slot (ADR-011) once its factory
  could already re-flash `app0` directly from SD — worth re-deriving per
  project: A/B rollback only earns its keep if there's a real scenario
  (e.g. network OTA) where a transfer could leave a slot half-written:
  an SD-only update, verified before it's made bootable, may not need it.
- **Feedback**: PiBot has a buzzer (`beep acknowledge/confirm`). A board
  without one (MySafeFob's e-ink X4 Pro) needs a substitute — MySafeFob
  uses a splash-screen refresh timed to the press-detection edge as a
  "you can release now" signal instead. Match whatever output the board
  actually has.
- **Per-board driver catalog inside Factory**: PiBot's Factory varies
  hugely per board (RGB parallel displays vs. SPI, different touch
  chips, I2C GPIO expanders for some boards' SD chip-select). MySafeFob's
  Factory only has one board to serve so far. The *shape* (factory owns
  its board's drivers, never shares them) is the generic part; the
  specific driver set is not.

## Reference implementation to read

`docs/FACTORY.md` in this repo (already written as a from-scratch
technical doc, not a copy of PiBot's) — covers the otadata mechanism,
the bootloader hook, and MySafeFob's specific amendments in full detail.
