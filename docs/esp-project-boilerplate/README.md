# ESP32 Project Boilerplate — generic patterns, extracted

> **Status**: started from scratch 2026-09-18 (a previous version of this
> document, if it ever existed, is gone — not found in any accessible
> memory or repository at the time of writing). Lives inside MySafeFob's
> `docs/` for now; the intent is to extract it into its own standalone
> repository once the content is mature enough to stand alone.

## Why this exists

Two ESP32 firmware projects — **PiBot** (`references/Luc-Pibot-cnc-pendant-
firmware/`, a mature, complex CNC pendant: multi-board, multi-CNC-target
via feature flags, LVGL UI, WiFi/BT/serial transports) and **MySafeFob**
(this repo: a single-purpose air-gapped TOTP/password device, simpler
in scope but still genuinely multi-hardware/multi-target — `-DMSF_BOARD=
x4pro`, a second board already stubbed in ADR-008, and its own factory/
recovery variant per board) — have independently converged on the same
handful of structural patterns, because they solve the same underlying
problems: how do you recover a bricked device without a PC, how do you
bring up a new board without losing the lessons from the last one, how do
you flash/build a dozen board variants without a dozen ad-hoc scripts, how
do you persist a growing pile of small settings without rewriting the same
NVS boilerplate every time.

MySafeFob's own `docs/ROADMAP.md` already documents each of these as an
ADR, phrased as "ported from an earlier reference design" (PiBot,
deliberately anonymized there per this repo's own doc-cleanup pass) —
but each one was extracted **once, for MySafeFob specifically**, tuned to
its own constraints (single board today, no LVGL, FreeInkUI instead,
simpler settings surface). This document goes one level up: for each
pattern, what's the **generic, project-agnostic core** (the part every
future ESP32 project should probably start with), versus what's
**project-specific tuning** (PiBot's multi-board/multi-CNC-target
complexity, or MySafeFob's air-gapped/e-ink constraints) that shouldn't be
copied blindly.

The goal is a real, reusable skeleton — not a rewrite of either project,
and not a framework to depend on, just a **documented starting point** so
the next ESP32 project (and anyone reading either codebase) recognizes the
same shapes instead of re-deriving them, or worse, re-making the same
mistakes both projects already paid to learn.

## Patterns extracted so far

| Pattern | Doc | Generic core | Project-specific tuning left out |
|---|---|---|---|
| Settings persistence | [settings.md](settings.md) | X-macro table (id/key/type/default) + generic typed get/set + mutex | PiBot's INI-file update mapping, IP-address type, multi-firmware-target defs |
| Factory/recovery | [factory-recovery.md](factory-recovery.md) | Bootloader hook backs up otadata → boots factory → factory restores on exit; factory lives under its board, updated only via USB/serial | PiBot's per-board Factory variance (touch/SD/display drivers differ every board); MySafeFob's single-app-slot simplification (ADR-011) |
| Hardware bring-up | [hardware-bringup.md](hardware-bringup.md) | Probe-first methodology, `hw_config.h` per board, validate-then-document, a recurring-bug checklist | PiBot's 12+ board catalog of board-specific bugs; MySafeFob's single-board depth |
| Build/flash tooling | [build-flash-tooling.md](build-flash-tooling.md) | Interactive-with-memory CLI, JSON flash map as source of truth, postbuild copies into `installer/` | PiBot's per-board variant matrix; its web-installer consumption of the same JSON |
| Board-agnostic skeleton | [board-structure.md](board-structure.md) | `boards/<name>/` owns everything hardware-specific incl. its own Factory; app code stays board-independent behind a config header | PiBot's much larger per-board driver catalog (display controllers, touch chips, expanders) |

Meta-pattern, applies across all the rows above:

| Pattern | Doc | Summary |
|---|---|---|
| Importing a feature/module from a reference project | [importing-features.md](importing-features.md) | Three buckets — copy verbatim (hardware drivers), re-derive the pattern (settings, factory), or skip with a documented reason — decided *before* writing code, not mid-copy-paste |

## How to use this when starting a new project

1. Read the pattern docs above in order — each says what to copy verbatim,
   what to adapt, and what to deliberately leave out for a simpler project.
2. Don't import code from here. Nothing in this directory is meant to be
   `#include`d or vendored — it's a description of a shape, re-implemented
   fresh each time (exactly like MySafeFob did for `settings_store.c`
   rather than vendoring PiBot's `ESP3DSettings`).
3. When a new project's version of one of these patterns teaches something
   the others didn't know yet, update the relevant doc here — this is
   meant to accumulate, not freeze at 2026-09-18.
