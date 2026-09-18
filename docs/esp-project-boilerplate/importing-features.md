# Pattern: importing a feature/module from a reference project

**Source**: not one PiBot module — this is the meta-pattern behind every
other doc in this directory, made explicit after doing it repeatedly in
one MySafeFob session (`touch.c`/`buttons.c`/`i2c_bus.c`/`battery.c`
copied from the factory; `settings_store.c` re-derived from
`ESP3DSettings`; the factory/bootloader-hook mechanism re-derived from
PiBot's; `flash_mgr.py` ported and trimmed). Also see this session's saved feedback memory, "check PiBot reference at
each roadmap step" — this doc is the "how" for that habit's "when".

## The problem

"Should I copy this, rewrite it, or skip it?" gets answered inconsistently
if it's re-decided from scratch every time a reference project's feature
looks relevant. A wrong call in either direction is expensive: copying
too much drags in coupling to the reference project's unrelated
complexity (build flags, types, features this project doesn't have);
copying too little means silently re-deriving a bug the reference project
already paid to fix.

## The decision, made explicit

For each candidate feature/module found in a reference project, decide
which of three buckets it falls into — **before** writing any code:

### 1. Copy verbatim (hardware drivers, validated sequences)

Use when: the code is hardware-specific, already validated on real
silicon, and the target board is the same or close enough that the
sequence should transfer unchanged (register order, timing, POR dances,
calibration constants).

**How**: copy the file(s) as-is into the new project's own tree (not a
symlink/submodule — see [board-structure.md](board-structure.md)'s
"duplication over shared core" principle, same reasoning applies across
projects, not just across boards in one project). Translate any non-English comments
(if the target project has an English-only policy). Replace the
project's own copyright/license header with the new project's. Strip
nothing else — a "why this constant" comment citing a measurement is
exactly the kind of context that must survive the copy (see
`hardware-bringup.md`).

**Example in this repo**: `boards/x4pro/{app,factory}/touch.c`/
`buttons.c`/`i2c_bus.c`/`battery.c` — copied from
`references/Luc-Pibot-cnc-pendant-firmware`-adjacent probe work and the
project's own factory, hardware-validated, unmodified beyond translation
and the license header.

### 2. Re-derive the pattern, don't vendor the code

Use when: the reference's actual implementation is coupled to
project-specific complexity (its own type system, its own UI framework,
its own multi-target build matrix) that would drag in more than the
target project needs — but the underlying *approach* (the data
structure, the state machine, the sequencing) is still worth reusing.

**How**: read the reference's implementation and docs fully, identify the
generic core versus the project-specific tuning (exactly the split every
other doc in this directory makes explicit), then write a new,
project-specific implementation from scratch that follows the same
shape. Document the split in `docs/esp-project-boilerplate/` if it's
likely to recur in a third project; document the specific decision as an
ADR either way (project rule: no code without a spec).

**Example in this repo**: `settings_store.c` (X-macro table + mutex,
`settings.md`) and the factory/bootloader-hook mechanism
(`factory-recovery.md`) — both re-implemented from scratch after reading
PiBot's version, neither vendored.

### 3. Skip — note why, don't half-implement

Use when: the feature solves a problem the target project doesn't have
(a multi-CNC-target abstraction for a project with one fixed function,
an IP-address settings type for a project with no network settings), or
the target's constraints make the reference's approach actively wrong
(an icon library sized for a color LCD's LVGL theme vs. a 1bpp e-ink
panel's font-driven UI).

**How**: say so explicitly, in whatever doc is closest to the decision
(an ADR, a design doc) — "not adopted, here's why" is worth one sentence
and prevents the same evaluation being redone next session. Don't import
a stripped-down half-version "just in case" — that's the worst of both
buckets above (still coupled to a decision that was actively rejected,
without even the benefit of the full feature).

## What ties the three together

In every case: **read first, decide the bucket, then write code** — never
copy-paste-then-figure-out-what-to-keep. Attribution and licensing follow
the bucket: verbatim copies keep the same license (both PiBot and
MySafeFob are LGPL-2.1+, same author, so this is usually mechanical here)
and get the new project's copyright header; re-derived patterns don't
need the original's license at all since no code crossed over, but a doc
pointer back to the source (like this directory) keeps the lineage
visible for whoever reads the new project next.
