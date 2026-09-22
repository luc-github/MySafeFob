# Touch calibration — comparison notes (MySafeFob vs. `crosspoint-reader`)

> **Status (2026-09-19)**: **RESOLVED and hardware-validated** — see §10
> for the final, working calibration. Sections 1-9 below are the
> investigation history (several superseded hypotheses along the way,
> including a mid-investigation "no fix, understand first" phase per
> user request 2026-09-17) — kept for context on *why* the final model
> looks the way it does, not as still-open questions. Read §10 first if
> you just need the current, correct behavior; read 1-9 if you need to
> understand how we got there or are debugging a regression.

## 1. Summary

Both projects drive the same physical part (GT911 @0x5D/0x14, shared I2C
bus SDA39/SCL38) with the same pin assignment (RST=GPIO4, INT=GPIO10,
power rail GPIO2 active-LOW) — confirmed identical in
`references/crosspoint-reader/freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h`
(`XTEINK_X4_PRO` profile, `.touch` field) vs. our own
`boards/x4pro/{app,factory}/main/touch.c`/`hw_config.h`. The pins are not
the problem. Three concrete differences stand out, most likely compounding:

## 2. Difference #1 — host config upload (likely root cause)

- **Our unit**: `touch_init()` tries a self-load reset dance first; on this
  physical unit it fails (`cfg version 0x00`, logged repeatedly across
  sessions — see the boot logs referenced in ADR-009's amendments), so we
  fall back to uploading a **185-byte host config table** —
  `s_cfg_480x800[]` in `touch.c`, sourced (per its own comment) from
  *"Staars/GT911_ESP32 GoodixFW.h (g911xOrig 1024x600) adapted to
  480x800 portrait"* — i.e. a **generic reference config for an unrelated
  panel**, hand-adjusted for our resolution, not X4 Pro's real factory
  config.
- **crosspoint-reader's unit**: `BoardConfig.h`'s inline comment states
  plainly: *"the GT911 SELF-LOADS its internal config on the standard
  reset dance — no host config upload needed"* (`beginGt911()` in
  `InputManager.cpp` never writes to `0x8047`/`0x8100` at all — it only
  resets, probes the I2C address, and starts polling).
- **Reading**: this looks like **OTP variance between physical units**
  (some X4 Pro boards shipped with a programmed GT911 config, ours
  apparently didn't) rather than a bug in either driver. But it means our
  unit runs on a **foreign config blob** that only approximates the real
  panel's calibration — and a GT911's config bytes don't just set axis
  min/max, they also define **virtual key zones**, sensitivity/threshold
  parameters, and orientation/mirror bits. A generic substitute config is
  a plausible explanation for "not calibrated the same way" as a whole,
  not just one specific spot.

## 3. Difference #2 — Home-key detection mechanism (concrete, testable)

This is the most actionable finding.

- **crosspoint-reader** (`InputManager.cpp`, `updateGt911Contacts()` area):
  detects the physical Home pad via a **sentinel coordinate pair** in the
  normal point record, not a status-register bit and not a coordinate
  zone guess:
  ```cpp
  const bool homeKeyDown = count == 1 && rawXWord == 0x03a0 && rawYWord == 0x1020;
  ```
  i.e. when the capacitive Home key is pressed, the GT911 reports exactly
  `(x=0x03a0, y=0x1020)` = `(928, 4128)` — values far outside any real
  touch coordinate range (0-479/0-799), which is the GT911's standard way
  of flagging a **virtual/OTP-defined key** rather than a screen touch.
  This works independent of any coordinate calibration — it's an exact
  integer match.
- **Our unit** (`touch.c`, `touch_read()`): we never check for this
  sentinel. We check `status[0] & 0x10` (a status-register key bit — per
  our own comment, *"has never come up with our uploaded config"*) OR a
  hand-recalibrated raw-coordinate **zone** (`raw_x<70 && raw_y in
  [660,720]`, itself already re-measured once already, 2026-09-15 →
  2026-09-16 amendments in `hardware-specs.md`/ADR-009). Both of our
  fallbacks are heuristics working around the fact that the "clean"
  signal (the sentinel, or the status bit tied to the real key-area
  config) isn't available to us — most likely *because* our foreign
  config blob doesn't define the same virtual-key area as the real X4 Pro
  firmware's config would.
- **Cheap, concrete experiment** (not yet done): log every raw `(x, y)`
  pair while pressing the physical Home pad on our current firmware. If
  `(0x03a0, 0x1020)` ever appears — even inconsistently — it confirms the
  GT911 silicon *can* report the key this way regardless of which config
  got loaded, and switching our `touch_read()` to check for that exact
  sentinel (in addition to, or instead of, the current zone heuristic)
  would likely be far more reliable than a coordinate-zone guess. If it
  never appears, that supports the config-content theory above (the key
  area genuinely isn't defined in the blob we uploaded).

## 4. Difference #3 — axis swap applied in software vs. baked into config

- **crosspoint-reader**: reads GT911 registers raw, then explicitly
  applies `swapXY`/`flipX`/`flipY` from `BoardConfig` in software
  (`InputManager.cpp`, e.g. `sx = swapXY ? rawY : rawX; ... if (flipY)
  point.y = (rawMaxY-rawMinY) - point.y;`). For X4 Pro:
  `swapXY=true, flipX=false, flipY=true` → final mapping is
  `screen.x = rawY`, `screen.y = 479 - rawX`.
- **Our unit**: `touch_read()` returns raw `(x, y)` **unmodified**, and
  our own comment states the 4-corner test found *"raw values already
  PORTRAIT ... raw_x = user_x, raw_y = user_y"* — i.e. no swap needed on
  our unit's raw output. This is consistent with the config-upload theory
  above: the GT911's config bytes can themselves set an "X2Y swap"/mirror
  bit in silicon — our uploaded blob most likely sets that bit
  differently than X4 Pro's real OEM config does, so our chip reports
  already-swapped coordinates while crosspoint-reader's (self-loaded,
  real OEM config) chip reports raw native-orientation coordinates that
  their software then swaps. Two different config blobs, two different
  raw-output conventions, both can be made to work — **but this means our
  "calibration" is entangled with whatever our specific uploaded blob
  happens to do**, rather than being a documented, deliberate choice the
  way crosspoint-reader's `swapXY`/`flipY` flags are.

## 5. What this does and doesn't explain

- Does plausibly explain: inconsistent/unreliable Home-pad detection,
  "feels uncalibrated" in a way that's hard to pin to one offset, and why
  our zone had to be re-measured/moved once already (2026-09-15 →
  2026-09-16) instead of being a fixed, documented hardware constant like
  crosspoint-reader's exact sentinel check.
- Does NOT yet explain (not investigated): whether raw single-finger
  touch *tracking* itself (not just the Home key) is inaccurate on our
  unit — e.g. whether tapping a known point on screen reads back at
  roughly the right raw coordinate. That would need the same kind of
  hands-on corner test we already did once (`hardware-specs.md`,
  4-corner test), repeated with attention to *consistency*, not just a
  one-time calibration snapshot.

## 6. Possible next steps (not decided, not started)

1. **Cheap**: add the sentinel check (`raw_x==0x03a0 && raw_y==0x1020`) to
   our `touch_read()` alongside the existing zone heuristic, purely as a
   diagnostic log line, to see if it ever fires on our hardware.
2. **Cheap**: log raw `(x,y)` on every touch event during normal use for a
   session, to check whether coordinates drift, jump, or stay consistent
   at a given physical point.
3. **Harder, higher payoff if OTP truly is blank on our unit**: obtain the
   real X4 Pro OEM GT911 config bytes (e.g. from a Ghidra dump of the
   stock firmware, the same technique already used elsewhere in this
   project for `BoardConfig`/frontlight/battery register recovery — see
   `references/freeink-sdk-main`'s own recovery notes) and upload *that*
   instead of the generic Goodix template — would likely fix axis
   orientation and the Home-key virtual-key area in one shot, since both
   are config-blob properties.
4. **No-hardware-change fallback**: keep the current zone heuristic but
   make it a **user-adjustable calibration setting** (UI-SPECS.md
   SETTINGS_CONTROLS, added 2026-09-17) rather than a hardcoded constant —
   acknowledges that this unit-to-unit OTP variance may mean different
   physical units need different zone coordinates, not just different
   software versions.

None of the above has been implemented — this document is the
"understand first" deliverable requested; a decision on which step (if
any) to pursue is a separate, later conversation.

## 7. Follow-up (2026-09-17) — hands-on test of `crosspoint-reader` itself

User built and ran `crosspoint-reader` on the same physical unit. Findings:

- **General touch tracking works**: row highlight-on-drag, scrolling —
  the GT911 is scanning and reporting real coordinates, contradicting a
  "scan is dead / OTP blank breaks everything" theory as the primary
  explanation.
- **But a systematic rotation-like error is present**: on
  crosspoint-reader's main menu, tapping the `Settings` row correctly
  selects it only near the **left** edge of that row; tapping further
  **right** on the same visual row instead selects a row **higher up**
  the list. Horizontal tap position affecting *vertical* selection is
  the textbook symptom of a wrong `swapXY`/`flipX`/`flipY` combination —
  i.e. crosspoint-reader's own hardcoded `XTEINK_X4_PRO` profile
  constants (§4 above) **do not match this specific physical unit's
  touch mounting orientation**, even in a mature, otherwise-working
  codebase.
- **Reframing**: this points away from "our driver/config is uniquely
  broken" and toward **unit-to-unit hardware variance in touch mounting
  orientation** (on top of the already-suspected OTP variance for the
  Home key specifically, §2). It also *vindicates* the empirical
  4-corner test our own `touch.c` was calibrated against
  (`hardware-specs.md`) over trusting any fixed SDK profile constant —
  we just watched a fixed constant get it wrong on this exact hardware.
  Our own general-touch mapping (not the Home-key detection, which
  remains a separate, still-open question — §2/§3) is therefore *more*
  likely to already be correct than this investigation initially assumed.

**Revised recommendation** (superseding part of §6 above): don't port
crosspoint-reader's `swapXY`/`flipX`/`flipY` constants — they're
demonstrably wrong for this unit. Instead, before shipping the
"direct tap anywhere" input mode (UI-SPECS.md §1.2), do a **grid-based**
tap-accuracy test (e.g. 3×3 or 5×5 known on-screen targets, not just the
4 corners) on our own firmware, specifically to rule out this same class
of non-linear/rotational error across the *full* area rather than only
at the extremes the corner test already covered. This should upgrade
SETTINGS_CONTROLS' "Touch calibration / diagnostic" screen (UI-SPECS.md
§2.12) from a passive raw-coordinate readout into an actual interactive
grid-tap check.

## 8. `TOUCH_PROBE_DEBUG` log capture (2026-09-17) — Home key resolved

User enabled `-DTOUCH_PROBE_DEBUG` (added to
`references/crosspoint-reader/platformio.ini`'s `[env:x4pro]`) and
captured a real session, pressing the physical Home pad repeatedly, then
once near the top of the screen for comparison.

**Home pad, 8 presses**: `primary=(695,477)` / `(696,476)` / `(696,477)`
/ `(694,477)` — rock-stable, always within 1-2 px of the same point.
**Elsewhere (near the top)**: `(225,3)`, `(528,5)`, `(659,20)`, `(655,41)`
— scattered, as expected for different tap locations.

The logged value is **not raw**: `InputManager.cpp:2225`'s
`touchDebugPrintf` fires on `touchPoint.x/y`, computed a few lines above
(2185-2190) *after* `swapXY`/`mapTouchAxis`/`flipX`/`flipY` — i.e. this is
already the final screen-space point, not a GT911 register value.

**Conclusion**: the Home pad's `(695,477)` sits right at the panel's max
corner (`rawMaxX=799, rawMaxY=479` in `BoardConfig`) — consistent with a
bezel-mounted pad at a fixed physical spot. It is **never** the
`(0x03a0, 0x1020)` sentinel described in §3 — across 8 presses, not once.
This resolves the open question from §3/§6: on this specific unit, the
physical Home pad is **just an ordinary touch region at a fixed
coordinate**, not a GT911 virtual-key/sentinel feature. crosspoint-
reader's `homeKeyDown` sentinel check (`InputManager.cpp:1728`) most
likely never fires here either — user's earlier report ("je ne sais pas
où appuyer pour valider un menu" in crosspoint-reader) is now explained:
its confirm path depends on a signal this unit's controller never sends.

**This validates, rather than undermines, MySafeFob's own approach**:
`touch.c`'s coordinate-zone check for the Home pad (`raw_x<70 && raw_y in
[660,720]`, our own uploaded-config raw range — not directly comparable
in magnitude to crosspoint's mapped `(695,477)` since the two firmwares
run different GT911 config blobs, §2/§4) is conceptually the *correct*
model for this hardware, not a workaround to eventually replace with a
"cleaner" sentinel-based check — that cleaner mechanism doesn't appear to
exist on this unit at all. The remaining open item is narrower than
originally framed: not "find the right detection mechanism," but
"confirm our own zone's numbers are still accurate" (already done once,
2026-09-15/16 per ADR-009's amendments) and give it a proper on-device
recalibration control (UI-SPECS.md §2.12's Home-zone re-tap control,
already speced) rather than a hardcoded constant, since it's now clear
this is a physical-position fact about one specific unit rather than
something a "correct" driver would compute from first principles.

The rotation-like error from §7 (general list-row selection, not the Home
pad) remains open and unrelated to this finding — still a reason for
caution before shipping "direct tap anywhere" without the grid test.

## 9. First real hardware test of direct-tap-anywhere (2026-09-18)

`ui_nav.cpp`'s first real multi-widget screens (ADR-014) exercised direct
tap (`InputSnapshot.touchPressed`/`touchReleased`/`touchX`/`touchY`, wired
for the first time this session — previously only the Home-zone `.confirm`
boolean was ever set) for real. Result:

- A direct tap on a **centrally placed** button (Home screen's large
  "Sleep now") **worked correctly**. This confirms `touch.c`'s raw
  `(x,y)` and the UI's logical coordinate space agree everywhere in the
  middle of the panel — no coordinate-system bug, our own driver's
  mapping is sound for ordinary taps.
- A direct tap on a **small button placed right at the panel edge**
  (the fixed-zone "Settings" button, originally `Rect{10,10,120,34}` —
  10 px from both the top and left edges) **never registered**, on every
  attempt. Left/Right+Home-pad selection of that same button worked every
  time, ruling out a logic bug in the action dispatch.
- **Conclusion**: likely a capacitive dead zone near the physical bezel,
  not a coordinate or calibration bug — consistent with a common
  characteristic of capacitive touch panels (reduced/no sensitivity in
  the last few px next to the frame). **Fix applied** (not just
  diagnosed, since it was a one-line UI layout change, not a driver
  change): moved both edge-adjacent buttons (`Settings`, `< Back`) inward
  to `x=24`/`y=20` (from `x=10`/`y=10`), giving `ensureMinTouchRect`'s
  44 px minimum touch expansion room to grow into on every side instead
  of being clipped by the screen edge. Not yet re-validated on hardware.
- **Still open**: how wide the dead zone actually is (untested — 24 px
  margin is a guess, not measured) and whether it's uniform on all four
  edges or specific to the top-left corner tested here. Worth
  characterizing properly once the touch diagnostic screen
  (UI-SPECS.md §2.12, already shipped as a live raw-coordinate readout)
  gets used for exactly this — tap progressively closer to each edge and
  note where the raw reading stops updating/registering.

## 10. Top-edge dead zone — now actually measured (2026-09-21, LVGL migration testing)

§9's open question ("how wide is the dead zone, exactly") got a rough
off-the-cuff estimate first ("0 to about 70" — not a measurement,
corrected in an earlier revision of this section), then a real one: the
Touch calibration screen (`build_touch_diag()`, `ui_nav.cpp`) got a
permanent column of labeled tick marks at known absolute y values (10,
50, 90, 130, ... every 40px), independent of any flex/theme layout —
same technique as the 5-cross test screen that found the I1
palette-offset bug, but kept as a real tool instead of thrown away.
Tapping down the column against the live raw-coordinate readout: the
y=10 and y=50 ticks never register; **y=90 is the first one that
does**, with the actual reported logical y landing around **87**.

So: the dead zone's upper edge is somewhere in **(50, 90)**, closer to
90 — good enough to design margins against, though the exact boundary
within that 40px gap is still not pinned down (the tick spacing is the
limiting factor, not the touch reading itself). Only the top edge has
been characterized this way; the other three are still unmeasured.

`kHeaderTopMargin` (`ui_nav.cpp`) raised 80 -> 100 for headroom above
this real measurement.

`boards/x4pro/app/ui_nav.cpp`'s header bar top margin was raised
40px -> 80px anyway, as a defensive margin against the rough estimate,
not as a value derived from a real measurement. Revisit once the
diagnostic above actually happens.

## 10. RESOLVED (2026-09-18/19) — the axes are swapped, not just miscalibrated

Everything above (§1-9) was investigated under the assumption that
`raw_x` tracks the display's horizontal position and `raw_y` tracks its
vertical position — the same assumption `touch.c`'s original
`pt.x = raw_x; pt.y = raw_y;` passthrough made. That assumption was
**wrong**. A guided crosshair test (5 fixed on-screen targets — 4
corners + center, drawn via `draw_crosshair()` on the touch diagnostic
screen, `ui_nav.cpp`) gave the first unambiguous evidence:

- **Top-left and bottom-left** crosshairs (same logical X, different
  logical Y) produced nearly identical `raw_y` (~20-30) despite very
  different `raw_x`.
- **Top-left and top-right** crosshairs (same logical Y, different
  logical X) produced nearly identical `raw_x` (~410) despite very
  different `raw_y`.

I.e. **`raw_y` tracks the display's horizontal position, and `raw_x`
tracks its vertical position** — a genuine axis swap, not a scale or
offset error on the axis each was assumed to represent. This matches
`FreeInkUICore.h`'s own `touchToLogical()` for `LandscapeClockwise`
touch orientation (`lx = 1-ny; ly = nx`) — the transform
`DisplayTarget::touchOrientationFor(Portrait)` already names as correct
for this exact render orientation, which nothing in this codebase had
ever actually called. §4 above ("axis swap... baked into config") was
on the right track conceptually but concluded the wrong thing (that our
unit's raw output needed *no* software swap) from a corner test that
happened to only vary one axis's extremes convincingly enough by
coincidence, not a real disproof.

### Final formulas (`boards/x4pro/app/touch.c`)

Two-point linear fits, calibrated directly against measured crosshair
positions (not derived from `touchToLogical()`'s normalized-coordinate
math, though they implement the same underlying transform):

```c
/* X from raw_y: (raw_y=25 -> x=20) .. (raw_y=690 -> x=460) */
pt.x = touch_lerp(raw_y, TOUCH_X_RAWY_LO, TOUCH_X_LOGICAL_LO,
                   TOUCH_X_RAWY_HI, TOUCH_X_LOGICAL_HI, EINK_H - 1);

/* Y from raw_x: NOT a single line -- see touch_lerp_y() below */
pt.y = touch_lerp_y(raw_x);
```

- **X is linear end to end** — a single `touch_lerp()` call. A center
  crosshair's ~20-30px residual is consistent with plain tap
  imprecision (an unmarked center is harder to hit than a corner): the
  two X half-ranges have nearly identical slopes (0.650 vs 0.668).
- **Y is NOT linear end to end** — the two Y half-ranges have very
  different slopes (-2.564 top-to-center vs -0.870 center-to-bottom), a
  real, repeatable nonlinearity confirmed across two separate test
  rounds. `touch_lerp_y()` therefore picks between two line segments
  meeting at the measured center point
  (`raw_x=415→y=240`, `raw_x=334→y=440`, `raw_x=104→y=640`) instead of
  one line for the whole range.
- `EINK_H`/`EINK_W` are reused for their *logical* meaning here, not
  their panel-dimension one — `DisplayTarget`'s Portrait rotation
  swaps panel 800×480 into logical 480×800, so `EINK_H` (480, panel
  width) is the logical *width*, and `EINK_W` (800, panel height) is
  the logical *height*. Deliberately not renamed, to avoid drift from
  the panel-dimension constants they actually are.
- The Home-key zone check (`raw_x<70 && raw_y in [660,720]`, §8 above)
  is **unaffected** by any of this — it thresholds the same raw
  registers directly, independent of how the main touch grid's axes
  get reinterpreted for `pt.x`/`pt.y`, and has been reliable throughout
  the whole investigation.

### Hardware validation (2026-09-19)

- **Crosshairs**: all 4 corners landed within ~13px of their true
  position on the first retest after the axis swap; a follow-up retest
  (after `touch_lerp_y()`'s two-segment fix) brought the center's Y
  error down from ~100px to ~8px. X's own center residual (~20-30px)
  never fully resolved and is attributed to tap imprecision, not a
  remaining model error (see the slope comparison above).
- **Real navigation**: a hardware log showed three consecutive direct
  taps correctly driving real screen transitions — Settings
  (`Home -> Settings`), "Controls & Calibration"
  (`Settings -> SettingsControls`), and "Touch diagnostic"
  (`SettingsControls -> TouchDiag`) — each landing inside its target
  widget's `Rect` and firing the right `ActionId`. This is the
  strongest evidence so far: not just crosshairs, but the actual
  product experience (tap-to-navigate) working correctly.

### Still open / not done

- **Dead-zone width** (§9): still not formally characterized — the
  24px button margin fix was a practical mitigation, not a measurement.
- **X-axis linearity**: validated only at the two extremes plus one
  imprecise center tap; never re-verified with a second, more careful
  center-only retest (the user chose to move forward once corners and Y
  were solid, per the "good enough, ship it" judgment call — not
  revisited since).
- **A related but separate debugging obstacle**: while chasing this,
  standard `ESP_LOG*` calls from `ui_nav.cpp`/`touch.c` were found to
  never reach the serial monitor (100% reproducible, not a race —
  confirmed via a raw `printf()` at the exact same call site, which
  always worked). Root cause never found (`CONFIG_LOG_VERSION=1`, no
  `LOG_LOCAL_LEVEL` override, no `esp_log_set_vprintf()` hook anywhere
  in the codebase — all checked and ruled out). Worked around, not
  fixed: both files now `#undef`/`#define` `ESP_LOGE`/`W`/`I` to a
  printf-based equivalent, scoped to those two translation units only
  (`docs/ROADMAP.md`, ADR-014's amendments). Revisit if it turns out to
  affect other files/tasks later — nothing else in the project has
  reported the same symptom so far.
