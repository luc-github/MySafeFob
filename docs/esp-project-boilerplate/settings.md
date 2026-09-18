# Pattern: persisted settings (NVS-backed)

**Source**: PiBot's `ESP3DSettings` (`main/core/esp3d_settings.{h,cpp}`,
`main/core/includes/esp3d_settings_defs.inc`, doc:
`references/Luc-Pibot-cnc-pendant-firmware/docs/codewiki/settings.md`).
**MySafeFob's re-implementation**: `boards/x4pro/app/settings_store.{c,h}`
+ `settings_defs.inc` (2026-09-18, `docs/ROADMAP.md` ADR-012 amendment).

## The problem

Any device with more than 2-3 user preferences ends up needing: a way to
read a value with a sane default before it's ever been set, a way to
write it back to flash, and — the part that's easy to skip until it bites
— safety against two tasks touching NVS at the same time. Hand-writing a
getter/setter pair per setting works for the first 2 settings and then
becomes the thing you're afraid to touch because every one is a
copy-pasted NVS call with a slightly different key string.

## The generic core (copy this)

1. **One X-macro table, one line per setting.** A `.inc` file (not a
   standalone translation unit — included multiple times, no include
   guard) listing `SETTINGS_DEF(id, nvs_key, type, default)` for every
   setting. This is the single place a setting's identity lives.
2. **Generate an internal id enum from the same table**, via the same
   `#include` with a different `SETTINGS_DEF` expansion — the id is
   never a raw string at call sites, so a typo in a key only breaks the
   one line that defines it, not every call site.
3. **Generate a lookup table (key/type/default) from the same `.inc`**
   a third time, with a third `SETTINGS_DEF` expansion producing
   designated initializers indexed by the enum.
4. **Two generic, typed functions** (`get_u32`/`set_u32`, or per-type if a
   project needs more than bool/u32) that take the id, look up the
   descriptor, and do the actual `nvs_get_*`/`nvs_set_*` + `nvs_commit` —
   every accessor is a thin wrapper over these two, never a new
   hand-written NVS call.
5. **A mutex around every access**, held for the whole read-or-write
   call. NVS itself is internally thread-safe per-call, but that doesn't
   protect a read-modify-write sequence spread across two calls, and any
   project with more than one task (which is every real project) will
   eventually have two tasks touching settings.
6. **Named, typed public accessors** (`settings_store_get_power_short_
   confirm()`, not `settings_get_u32(SETTINGS_ID_PowerShortConfirm)`) as
   the only thing other files call — keeps call sites readable and type-
   safe, keeps the id enum and the generic engine private to the .c file.

## What PiBot adds that's project-specific (leave out for a simple project)

- **Schema versioning + `reset()`** (`checkSchemaVersion()`, a version
  byte written alongside the settings): worth adding once a project has
  enough settings that a layout change becomes plausible — premature for
  a first pass with a handful of settings (MySafeFob deliberately skipped
  this for now, see ADR-012's amendment).
- **INI-file mapping** (`ini_section`/`ini_key` in the descriptor,
  `findSettingByIni()`): specific to PiBot's SD-based settings-import
  update mechanism. Only relevant if a project has an equivalent bulk
  import/export format.
- **IP-address type, string-as-float type, bitmask/bitfield types**:
  PiBot needs these for WiFi/CNC-transport settings. A project's type set
  should match what it actually stores — MySafeFob only needed bool/u32
  so far; add types when a real setting needs one, not speculatively.
- **Multi-firmware-target default tables** (`esp3d_target_settings_defs.inc`
  per CNC backend): purely a consequence of PiBot supporting multiple CNC
  firmwares behind one binary. Not a generic settings-module concern at
  all — don't let it influence a new project's table shape.

## Reference implementation to read

`boards/x4pro/app/settings_defs.inc` + `settings_store.c` in this repo —
small enough (2 settings today) to read end to end in a few minutes and
see the whole pattern without PiBot's additional layers.
