# UI-SPECS.md — MySafeFob (MSF): screen-by-screen UI specification

> **Status**: draft, awaiting review (Phase 6.2 deliverable, written
> retroactively during task 8.4 rather than Phase 6 — no screen implementation
> beyond the provisional nav-loop placeholder existed before this document).
> Reference: `docs/FEATURES.md` (features/use cases), `docs/INTERFACES.md`
> §4.4 (`ui_mgr` screen enum), `docs/ROADMAP.md` ADR-013 (nav loop) and
> ADR-017 (the app UI is LVGL since 2026-09-21).
>
> **Note (2026-09-26)**: sections written before 2026-09-21 name FreeInkUI
> components (`key-grid.h`, `option-dialog.h`, `qwerty-keyboard.h`,
> `StepperRowProps`, `draw_*()`, `InteractionBuffer`…). They are historical:
> ADR-017 moved the app to LVGL, and the equivalents are our own widgets in
> `ui_widgets.*`/`ui_keyboard.*` and LVGL focus groups. The screen layouts
> and behaviors described stay valid unless amended.

---

## 1. Conventions

### 1.1 Canvas

- **480×800 portrait**, 1 bpp (black/white only, no gray — no animation,
  no transparency: FEATURES.md §6 non-goals).
- Minimum text size ≈ 20-24 px equivalent (FEATURES.md §7 — 8×16 confirmed
  unreadable on this panel; the existing splash/sleep/ready screens already
  use a ≥16×32 bitmap font as the floor).
- Full refresh (`eink_display_fb`, ~2-4 s) only on: screen *type* change
  (e.g. leaving TOTP_LIST for TOTP_CODE) and every `EINK_FAST_BUDGET`
  fast draws (ghost purge, already handled inside `eink_display_fb_fast`).
  Everything else (focus move, a digit typed, a list scroll) uses the fast
  DU refresh. **Two enforcement layers, both real** (2026-09-18, clarified
  after a user question — previously this paragraph described intent that
  wasn't actually wired up at the call site): the ghost-purge budget lives
  inside `eink_display_fb_fast()` itself (driver level, `eink.c` — falls
  back to a full refresh automatically when the previous frame is unknown
  or the budget is exhausted); the screen-type-change trigger lives in
  `ui_nav.cpp`'s `switch_screen()`, which sets a `s_force_full_refresh`
  flag consumed at the next flush — before this, only the very first draw
  of the whole task ever got an explicit full refresh, and every screen
  switch after that silently rode on the driver's budget fallback instead
  of the documented rule.

### 1.2 Input model (ADR-013 amended: **both** input styles, hardware-
validated 2026-09-19)

- **Left/Right physical buttons**: move the focus highlight one step
  within the current screen's focusable set (wraps circularly — same
  `InteractionBuffer::moveFocus` already validated in `ui_nav.cpp`).
  **Default focus on screen entry is widget 0** (`switch_screen()`,
  2026-09-19 fix) — every screen's own `draw_*()` registers its primary
  action (the header's Settings/Back slot, §1.3) as the first
  interaction each frame, so index 0 is never a disabled row or
  something further down. Earlier builds reset focus to "none" on every
  screen change, which meant the first Left/Right press just "woke up"
  the focus instead of doing anything visible.
- **Touch-Home pad** (fixed zone, `touch.c`): confirms/activates whatever
  currently has focus — works everywhere, independent of screen content,
  by focus index, never by tap position (so its touch-active zone
  extending past the visible display, into the bezel-mounted physical
  key's own area, is harmless — see `docs/touch-calibration-notes.md`
  §10). This is the accessible/fallback path and the one already proven
  on hardware; every screen in this document must remain fully operable
  with *only* Left/Right + Home (no functionality may require direct
  tap).
- **Direct tap** on a list row / button / key: moves focus to it **and**
  activates it in the same gesture (one tap = select+confirm) — faster
  for longer lists (TOTP/password entries). **Hardware-validated
  2026-09-19**: after `docs/touch-calibration-notes.md` §10's axis-swap
  fix, direct taps correctly drove three consecutive real screen
  transitions (Settings, Controls & Calibration, Touch diagnostic), each
  landing inside its target widget and firing the right action. `touch.c`
  exposes both the calibrated logical coordinate (`pt.x`/`pt.y`, what
  actually gets hit-tested) and the raw GT911 register value
  (`pt.raw_x`/`pt.raw_y`, calibration-only, never used for hit-testing)
  on every `touch_point_t` — the touch diagnostic screen (§2.12) shows
  both side by side.
- **Confirm-press flash — every activation, regardless of input method**
  (2026-09-18/19 fix): whatever fires an action (a tap release, a
  Left/Right+Home confirm, or a Power short-press confirm) first redraws
  the activated widget inverted (`StateActive`) on the *current* screen,
  flushes that frame, and holds for ~500ms — only then does the action
  actually run (`board_ui_nav_task`'s main loop, `s_flash_action`/
  `flash_state()`). A touch tap naturally showed this while the finger
  stayed down; a confirm-driven activation used to fire and act in the
  same instant with no equivalent feedback, which made navigation feel
  inconsistent depending on how a button was pressed. Scoped to only the
  final action in a coalesced input burst (`ui_nav.cpp`'s queue
  draining) — intermediate actions in a fast burst still skip the flash
  and the flush entirely, on purpose, to keep rapid Left/Right presses
  from each costing a full e-ink refresh.
- **Power button**: long-hold thresholds (sleep, factory, ADR-009) are a
  hard invariant, never touched by a setting. A short press-and-release
  may **optionally** act as an extra confirm pulse, equivalent to
  touch-Home — gated behind the "Power short press = Select" toggle
  (§2.12, SETTINGS_CONTROLS, off disables it back to pure-power
  behavior; wired and hardware-validated). Power is **never** repurposed
  as Back or any screen-specific action, on or off.
- **Back**: no physical button is free for it (Left/Right = focus, Power =
  reserved). Every screen except HOME renders a tappable `< Back` button
  in the **fixed zone's primary action slot** (§1.3 — not a separate
  contextual-zone header row; that changed 2026-09-19, see below) —
  reachable by Left/Right focus like any other widget, or by direct tap.

### 1.3 Chrome — two zones per screen (amended 2026-09-19, hardware-
validated layout): a **fixed header** (identical structure, but not
identical *content*, on every screen) and a **contextual** zone below it
(the actual screen content, changes per §2).

```
┌──────────────────────────────────────┐
│  Controls                      [ico] │  <- FIXED header, row 1:
│                                  87%  │     screen title (left) +
│                                       │     battery icon/% (right)
════════════════════════════════════════     (divider line)
│ [    < Back    ]                     │  <- FIXED header, row 2:
│                                       │     PRIMARY ACTION SLOT --
│ ── (screen content) ─────────────────│     "Settings" button on HOME,
│                                       │     "< Back" everywhere else,
│                                       │     always the same Rect
├──────────────────────────────────────┤
│  vX.Y  Sep 19 2026 14:03             │  <- FOOTER: version + build
└──────────────────────────────────────┘     timestamp (esp_app_desc_t)
```

**Fixed header — always present** (except UNLOCK/SPLASH/SLEEP, which have
no chrome at all — nothing to configure before authenticating, nothing to
navigate away from mid-splash/mid-sleep). Two rows, both hardware-
validated 2026-09-19:

- **Row 1 — title + battery** (top of the panel, `draw_fixed_zone()`):
  - **Screen title** (left, non-interactive text): "Settings" on HOME
    (there is no dedicated title text there — the primary action slot
    below *is* the Settings button) and on SETTINGS itself; each
    sub-screen shows its own name instead — "Controls", "About", "Touch
    diagnostic" (`header_title_for(Screen)`). Never repeated anywhere
    else on screen (see the primary action slot below).
  - **Battery icon + `"NN%"`** (right): `battery_read()`
    (`boards/x4pro/app/battery.c`, the factory's validated CW2017
    driver) read on every redraw. A **Lucide status icon**
    (`battery-charging`/`-full`/`-medium`/`-low`/`-warning`, thresholds:
    charging overrides all, then ≥80/≥50/≥20/below), 48×48
    (`tools/gen_icons.py --sizes 24,48`), plus `"NN%"` text (`"--"` on
    an I2C read failure) in the **default font/size** — matching the
    rest of the screen's text, not scaled up with the icon. Date/time
    is not shown here yet — no RTC-backed wall clock wired into the UI
    layer (`time_svc`); that slot stays reserved.
  - A horizontal divider line separates row 1 from row 2.
- **Row 2 — the primary action slot**, a single fixed `Rect` (currently
  `{24,76,130,36}`) that is *always* either:
  - a **"Settings" button** (HOME only) — tappable, opens SETTINGS
    (§2.11); also the first stop in the Left/Right focus cycle (default
    focus on entry, §1.2), so Settings never requires a working touch
    panel; or
  - a **"< Back" button** (every other screen) — same `Rect`, drawn by
    `draw_back_and_title()` (name kept for now; it no longer draws a
    title — see below). Whichever one occupies the slot, it's the
    default-focused widget on screen entry.
  - This slot is the *only* thing that changed position during
    development: earlier drafts placed Back inside the contextual zone
    (its own header row, next to a per-screen title); Back now lives in
    the fixed header instead, in exactly the spot Settings occupied one
    screen up — one consistent primary-action position instead of it
    jumping around depending on which screen you're on. A title text
    used to sit centered next to Back there too; removed entirely
    (2026-09-19) since it duplicated row 1's title and Back's
    focused/active highlight could visually run into it.
- **Alert button (HOME only, ADR-018, 2026-09-26)** — replaces the earlier
  plan of putting the time-sync alert inside TIME_SYNC, where nobody sees
  it without going there: an icon button `LV_SYMBOL_WARNING`, same size
  and style as the Settings icon button, in row 2 **on the right**, same
  vertical position as Settings (left). Created only while at least one
  alert is active (`alerts.c`); otherwise absent, not merely hidden, so it
  never adds an empty stop to the focus cycle. Focus order: Settings,
  alert button, content. Tap/confirm opens ALERTS (§2.21).

  ```
  ┌──────────────────────────────────────┐
  │                                 87%  │
  ════════════════════════════════════════
  │ [ ⚙ ]                         [ ⚠ ] │
  │ ── (HOME content) ───────────────────│
  ```

**Footer** (`draw_footer()`, 2026-09-18): a divider line plus the running
firmware's version and build date/time (`esp_app_get_description()` —
`version` falls back to `git describe --always --dirty` since no
`PROJECT_VER` is set), fixed at the bottom of every screen with the same
chrome. Exists to make "which build is actually running" obvious at a
glance during hardware testing.

**Contextual zone**: everything screen-specific below the fixed header —
just the screen's own content now (title and Back moved into the fixed
header, above). Detailed per-screen in §2. Add/delete of TOTP/password/
recovery entries is **never** done inline in a list's contextual zone —
always via the dedicated utility screens (ADD_EDIT_ENTRY, §2.5/§2.8),
reached through each list's `+ Add` row.

#### 1.3.1 Settings navigation (single "return-to" slot, not a screen stack)

Settings is reachable from HOME via the fixed header's "Settings" button
(§1.3), and its Back must return to *that* originating screen (user
decision) rather than unconditionally to HOME. This needs exactly **one**
remembered screen id — set when the Settings button is tapped, consumed
and cleared when SETTINGS's top-level Back fires — not a
general navigation history stack. This stays compatible with
`INTERFACES.md` §5 invariant #4 ("the UI never keeps two live screens
simultaneously"): only one screen is ever *live*; the return-to slot is
just an id, not a second screen instance. `INTERFACES.md` §4.4's `ui_mgr`
contract gains one field for this (`ui_mgr` open point, not yet in that
document — flagged in §3 below). Back presses *inside* Settings' own
sub-screens (TIME_SYNC, SETTINGS_BACKUP, SETTINGS_ABOUT, §2.15/2.16/2.20)
still just pop one level back to the SETTINGS menu itself, same as any
other flow.

### 1.4 Screen inventory (matches `INTERFACES.md` §4.4's `screen_t` enum)

| Screen | Feature | Reachable from |
|---|---|---|
| SPLASH | boot | — (already implemented, `splash.cpp`) |
| UNLOCK | F-03 | boot / wake / auto-lock timeout |
| HOME | F-07 | UNLOCK success |
| TOTP_LIST | F-01 | HOME |
| TOTP_CODE | F-01 | TOTP_LIST |
| ADD_EDIT_ENTRY (TOTP variant) | F-01 | TOTP_LIST |
| PWD_LIST | F-05 | HOME |
| PWD_VIEW | F-05 | PWD_LIST |
| ADD_EDIT_ENTRY (password variant) | F-05 | PWD_LIST |
| RCV_LIST | F-05b | HOME |
| RCV_CODE | F-05b | RCV_LIST |
| SETTINGS | — | **anywhere** (the fixed header's Settings button, §1.3) |
| SETTINGS_CONTROLS | — (2026-09-17) | SETTINGS |
| SETTINGS_SECURITY | F-03/ADR-012 | SETTINGS |
| SETTINGS_DISPLAY | F-16 | SETTINGS |
| TIME_SYNC | F-02 | SETTINGS |
| SETTINGS_BACKUP | F-06b | SETTINGS |
| SETTINGS_OWNER_INFO | F-19 | SETTINGS |
| SETTINGS_ABOUT | F-20 (status) | SETTINGS |
| ALERTS | F-02 + future alerts (ADR-018) | HOME's alert button (§1.3), only while an alert is active |
| SLEEP | F-19 | anywhere (Power long-press / idle timeout / "Sleep now") — already implemented, `splash.cpp` |

`UPDATE_SD` from `INTERFACES.md`'s draft enum is **dropped**: F-06/ADR-011
already settled that firmware updates happen only through the factory,
never the running app — there is no app-side screen for it. `STATUS` is
folded into `SETTINGS_ABOUT` below rather than kept as a separate top-level
screen.

---

## 2. Screens

### 2.1 UNLOCK (F-03)

```
┌──────────────────────────────────────┐
│              MySafeFob               │
│ ──────────────────────────────────── │
│                                       │
│              ● ● ● ○ ○ ○              │  <- 6 dots, filled as digits are
│                                       │     entered, never the digits
│                                       │     themselves (F-03 anti-trace)
│   ┌───┐ ┌───┐ ┌───┐                   │
│   │ 5 │ │ 1 │ │ 8 │   <- NON-sequential
│   └───┘ └───┘ └───┘      layout, re-shuffled
│   ┌───┐ ┌───┐ ┌───┐      **every unlock attempt**
│   │ 2 │ │ 9 │ │ 0 │      (F-03: counters fingerprint-
│   └───┘ └───┘ └───┘      smudge analysis of the screen)
│   ┌───┐ ┌───┐ ┌───┐
│   │ 7 │ │ 4 │ │ 6 │
│   └───┘ └───┘ └───┘
│           ┌──────┐
│           │ DEL  │
│           └──────┘
├──────────────────────────────────────┤
│ Attempt 1/N · back-off after failures │
└──────────────────────────────────────┘
```

- No chrome header (nothing to go "Back" to before unlocking).
- Digit grid (originally planned on FreeInkUI's `key-grid.h`; now an LVGL
  keypad derived from Settings > Security's test keypad,
  `ui_screen_security.cpp`); re-randomized on every
  screen entry (new unlock attempt, or after a wrong PIN) — reads
  `esp_random()`, never a fixed layout.
- Left/Right cycles focus through the 3×3 grid + DEL, row-major; Home/tap
  presses the focused key. No explicit "confirm PIN" key: the 6th digit
  auto-submits (matches F-03 "fixed length 6").
- On success → HOME. On failure → dots clear, grid re-shuffles, attempt
  counter increments, exponential back-off delay shown/enforced
  (`INTERFACES.md` §1.3) before the next attempt is accepted.
- Wake-from-sleep and the idle-timeout auto-lock (F-03 "automatic lock
  after inactivity") both land here directly — no splash re-shown (matches
  ADR-009's existing "direct jump to UNLOCK" wake behavior).

### 2.2 HOME (F-07)

```
┌──────────────────────────────────────┐
│              MySafeFob          [i]  │
│ ──────────────────────────────────── │
│                                       │
│   ▸ TOTP Codes                       │
│                                       │
│   ▸ Passwords                        │
│                                       │
│   ▸ Recovery Codes                   │
│                                       │
│   ▸ Settings                         │
│                                       │
│   ▸ Sleep now                        │
│                                       │
├──────────────────────────────────────┤
│  L/R focus   tap/Home confirm        │
└──────────────────────────────────────┘
```

- No `< Back` (top of the navigation tree — matches F-07 "boots directly
  into the unlock screen" then here, no further "up").
- Direct, permanent replacement for the current placeholder's "About"/
  "Sleep now" (`ui_nav.cpp`) — "Sleep now" is kept as a HOME item (same
  trampoline via `power_mgr_claim_terminal_action()`), "About" moves into
  SETTINGS_ABOUT.
- 5 top-level entries — comfortably within `InteractionBuffer<8>`'s
  current capacity, no widening needed yet.

### 2.3 TOTP_LIST (F-01)

```
┌──────────────────────────────────────┐
│ < Back         TOTP Codes            │
│ ──────────────────────────────────── │
│  GitHub                          ›   │
│  Google                          ›   │
│  AWS                             ›   │
│  ...                                 │
│                                       │
│                          [+ Add]     │
├──────────────────────────────────────┤
│  L/R focus   tap/Home confirm        │
└──────────────────────────────────────┘
```

- `list.h` component, one row per account (label only — the secret itself
  never renders here). Rows are directly tappable (jump+confirm) or
  reachable via Left/Right.
- Selecting a row → TOTP_CODE for that account. `+ Add` (its own focusable
  row, end of the list) → ADD_EDIT_ENTRY (TOTP, empty).
- Empty state (no accounts yet): single centered line, "No accounts yet —
  tap + Add", `+ Add` still reachable.
- F-11 (search/filter, should-have) and F-12 (categories/tags,
  should-have) are explicitly deferred — this screen is a flat list for
  v1.0, per FEATURES.md §4.

### 2.4 TOTP_CODE (F-01)

```
┌──────────────────────────────────────┐
│ < Back          GitHub          Edit │
│ ──────────────────────────────────── │
│                                       │
│                                       │
│            1 2 3   4 5 6             │  <- large digits, ≥ the
│                                       │     32px-equivalent floor
│                                       │
│         ████████████░░░░░░░          │  <- validity countdown bar
│                                       │     (30/60s period, F-01)
│                                       │
├──────────────────────────────────────┤
│  L/R: prev/next account   tap: back  │
└──────────────────────────────────────┘
```

- Read-then-type screen (F-01): no copy-paste exists on this device by
  design (air-gapped, FEATURES.md §6).
- **After the countdown reaches zero**: the code is NOT regenerated
  automatically (ADR-009: "TOTP codes computed on demand at wake-up — no
  background task, no tick" still holds while awake too, to avoid a
  redraw loop). Instead the digits gray out/get an overline marker and a
  "Refresh" affordance appears — matches F-01 "the code stays displayed
  after expiry ... until the next display".
- Left/Right on THIS screen is repurposed to move to the prev/next account
  in the list (skip back to TOTP_LIST just to switch accounts would be
  slower) — a deliberate, documented exception to "Left/Right = generic
  focus move" for this one high-frequency screen. Home/tap still means
  "back to TOTP_LIST" here since there's nothing else on-screen to focus.
- `Edit` (header, right) → ADD_EDIT_ENTRY (TOTP, prefilled) for this
  account.

### 2.5 ADD_EDIT_ENTRY — TOTP variant (F-01)

```
┌──────────────────────────────────────┐
│ < Back        Add TOTP account       │
│ ──────────────────────────────────── │
│  Label                               │
│  [ GitHub________________ ]          │
│                                       │
│  Secret (Base32)                     │
│  [ JBSWY3DPEHPK3PXP_______ ]         │
│                                       │
│  Digits: [6] 8      Period: [30]s 60s│
│                                       │
│           ┌────────┐ ┌────────┐      │
│           │ Delete │ │  Save  │      │
│           └────────┘ └────────┘      │
├──────────────────────────────────────┤
│  L/R focus   tap/Home confirm        │
└──────────────────────────────────────┘
```

- Tapping `Label` or `Secret` opens a full-screen text-entry keyboard
  (§2.19, `qwerty-keyboard.h`) — this screen itself has no inline typing.
- `Digits`/`Period` are 2-way toggles (`toggle.h`/`radio-group.h`), not
  free text — only the values TOTP actually supports (6/8 digits, 30/60 s
  period, per F-01's stated fields).
- `Delete` only shown when editing an existing entry (not on "Add"),
  and always behind a confirmation (§2.18 pattern) — irreversible.
- No QR/camera import (FEATURES.md §6 explicit non-goal) — Base32 is
  always typed manually.
- `Save` validates the Base32 string decodes cleanly (reuses
  `totp_engine`'s existing decode path) before writing to `secret_store`;
  a decode failure keeps the screen open with an inline error, no popup.

### 2.6 PWD_LIST (F-05)

Same layout/interaction as TOTP_LIST (§2.3): flat list of labels, `+ Add`
row, tap or Left/Right+confirm to open PWD_VIEW. No secret content ever
appears in this list — only labels.

### 2.7 PWD_VIEW (F-05)

```
┌──────────────────────────────────────┐
│ < Back           GitHub         Edit │
│ ──────────────────────────────────── │
│  Username                            │
│  octocat                             │
│                                       │
│  Password                            │
│  ●●●●●●●●●●●●        [ Reveal ]      │
│                                       │
│  Notes                                │
│  (empty)                             │
│                                       │
├──────────────────────────────────────┤
│  auto-clears to HOME after 30s idle  │
└──────────────────────────────────────┘
```

- Password is masked (`●`) by default; `Reveal` (tap/Home-confirm) shows
  it in clear, character-spaced for easy manual typing (F-05: "readable
  font, ability to reveal character by character").
- **F-05 auto-clear**: an idle timer *specific to this screen* (distinct
  from, and shorter than, the ADR-012 45s device-sleep timer) returns to
  HOME and re-masks the password if no input occurs — default 30 s,
  because e-ink retains the image at zero power: a revealed password left
  on an unattended, sleeping device would otherwise stay physically
  visible indefinitely. This timer is reset by ANY input on this screen,
  same signal source as `board_activity_notify()` but screen-scoped.
- No copy button (air-gapped, F-05/§6).

### 2.8 ADD_EDIT_ENTRY — password variant (F-05)

Same shape as the TOTP variant (§2.5): `Label`/`Username`/`Password`/
`Notes` text fields (full-screen keyboard on tap), `Delete` (edit only,
confirmed), `Save`. `F-08` (password generator, should-have) would add a
`Generate` button next to the `Password` field — not required for v1.0.

### 2.9 RCV_LIST (F-05b)

Same list pattern as §2.3/2.6, one row per **service** that has recovery
codes on file (not one row per code — codes are inside RCV_CODE).

### 2.10 RCV_CODE (F-05b)

```
┌──────────────────────────────────────┐
│ < Back           GitHub              │
│ ──────────────────────────────────── │
│  1. A1B2-C3D4-E5F6         [unused]  │
│  2. G7H8-I9J0-K1L2      ▬▬▬ used ▬▬▬ │  <- struck-through, kept visible
│  3. M3N4-O5P6-Q7R8         [unused]  │     (F-05b: "traceability")
│  ...                                 │
├──────────────────────────────────────┤
│  L/R focus   tap/Home: mark as used  │
└──────────────────────────────────────┘
```

- Confirming a code marks it used immediately (no separate confirm step —
  low risk, easily distinguishable from a destructive action, and the
  whole point is "I am using this code right now").
- No `+ Add`/`Edit`/`Delete` here: recovery codes are provisioned once
  (at 2FA activation on the actual service) and entered as a batch — that
  entry flow belongs to ADD_EDIT_ENTRY's scope conceptually but is
  deliberately **not detailed in this v1.0 pass** (F-05b's UI needs, per
  FEATURES.md, only "display one code at a time, mark it used" — batch
  entry is an open point, flagged in §3 below).

### 2.11 SETTINGS (menu)

```
┌──────────────────────────────────────┐
│ < Back          Settings             │
│ ──────────────────────────────────── │
│   ▸ Controls & Calibration           │
│   ▸ Security (PIN, auto-lock)        │
│   ▸ Display                          │
│   ▸ Time & Sync                      │
│   ▸ Backup (SD)                      │
│   ▸ Owner info (sleep screen)        │
│   ▸ About                            │
├──────────────────────────────────────┤
│  L/R focus   tap/Home confirm        │
└──────────────────────────────────────┘
```

Reached from **the fixed header's Settings button, on HOME** (§1.3/§1.3.1) — its
own `< Back` returns to whichever screen Settings was tapped from, not
unconditionally to HOME (the one deliberate exception to "Back always
goes up one level in the current flow", per the user's 2026-09-16 design
review). All 7 rows below fold in every settings-shaped item mentioned
across FEATURES.md, so nothing is left dangling as an unplaced "open
point" the way the previous draft of this section left idle-lock delay
and frontlight.

> **Amendment (2026-09-26) — actual menu today**: Controls, Display, Time,
> Security, Touch Calibration, About (`ui_screen_settings.cpp`). Touch
> Calibration got its own row (ADR-014 amendments). **Still missing**:
> Backup (SD) and Owner info — ROADMAP 8.0 P11. Security currently holds
> only the PIN keypad UI test (nothing stored).

### 2.12 SETTINGS_CONTROLS (added 2026-09-17, see
`docs/touch-calibration-notes.md`)

> **Status (2026-09-19)**: built and hardware-validated, but simpler
> than this section originally speced — see the callouts below each
> mockup for what actually shipped vs. what's still just a design idea.
> The header title ("Controls") lives in the fixed header now, not next
> to `< Back` — see §1.3's 2026-09-19 chrome update; the mockups below
> predate that and still show the old layout.

```
┌──────────────────────────────────────┐
│ < Back     Controls & Calibration    │
│ ──────────────────────────────────── │
│  Power short press = Select  [ On ]  │
│                                       │
│   ▸ Touch calibration / diagnostic   │
├──────────────────────────────────────┤
│  L/R focus   tap/Home confirm        │
└──────────────────────────────────────┘
```

- **Power short press = Select** (default **On**): a quick press-and-
  release of Power (under `MSF_POWER_LONG_MS`, 1.5 s — today this range
  does *nothing* in `power_button_task`, `main.c`, so this is a genuinely
  free gesture, not a repurposing of an existing one) acts as a second,
  always-available "confirm" input, equivalent to touch-Home. Power's
  long-press thresholds (sleep at 1.5 s, factory at 10 s, ADR-009) are
  **unchanged** — this only fills in the previously-unused "pressed and
  released quickly" case. Precedented on this exact hardware:
  `references/crosspoint-reader` uses a short Power click as its Confirm
  action on the X4 Pro (with a double-click guard window reserved for a
  frontlight toggle, which MySafeFob doesn't have yet — no such guard
  needed here).
  - **Intended lifecycle of this toggle** (user design intent,
    2026-09-17): ships **On** by default, since it gives a reliable
    confirm path independent of the touch panel while touch calibration
    is still uncertain (`touch-calibration-notes.md`). Once a user has
    verified their unit's touch works reliably (via the calibration
    screen below, or just in daily use), they can switch this **Off** to
    make Power a pure power control again with no UI role at all — purely
    a preference once touch is trusted, not a fallback-only feature.
  - **Built 2026-09-17, hardware-validated**: `board_ui_nav_power_confirm()`
    (`ui_nav.h`/`.cpp`) is exactly the bridge described above —
    `power_button_task` (`main.c`) calls it on a qualifying short
    release, gated by `settings_store_get_power_short_confirm()`. Power
    still never becomes a *screen-level* actor: it's a single confirm
    pulse, not GPIO3 joining the nav loop's own input reads. The toggle
    itself persists across reboots (`settings_store.c`, table-driven NVS
    engine, ADR-012's settings-refactor amendment).
- **Touch calibration / diagnostic** — revised 2026-09-17 after hands-on
  testing of `references/crosspoint-reader` on this same unit found a
  rotation-like error in *its* fixed touch profile (`touch-calibration-
  notes.md` §7: horizontal tap position on a menu row there selects the
  wrong row vertically). That result argues against trusting any fixed
  swap/flip constant (ours or a borrowed SDK profile) and *for* an
  interactive, on-device grid test — this screen is upgraded from a
  passive raw-coordinate readout to an active check:

  ```
  ┌──────────────────────────────────────┐
  │ < Back      Touch calibration        │
  │ ──────────────────────────────────── │
  │  Tap each target as it appears       │
  │  Target 4 of 9                       │
  │                                       │
  │   ·        ·        ·                │
  │                                       │
  │   ·        ✛        ·   <- current   │
  │                                       │        target
  │   ·        ·        ·                │
  │                                       │
  │  raw: x=612 y=201  (last tap)        │
  ├──────────────────────────────────────┤
  │  tap the target — no L/R/Home here   │
  └──────────────────────────────────────┘
  ```

  > **What actually shipped (2026-09-18/19) — simpler than this
  > mockup**: 5 crosshairs (4 corners + center, `draw_crosshair()`), not
  > a guided 9-target sequence with pass/fail automation. No automatic
  > pass/fail summary — the screen shows the **last tap's raw AND
  > logical coordinates side by side** (`s_last_touch.raw_x/raw_y` vs.
  > `.x/.y`) and a human reads them off directly. This turned out to be
  > enough: it's exactly what found the axis-swap bug and validated its
  > fix (`docs/touch-calibration-notes.md` §10) through several rounds
  > of real hardware testing. The guided-sequence/auto-pass-fail version
  > below remains a real enhancement idea, not something ruled out —
  > just not needed to get calibration working.

  - **3×3 grid** (9 targets, corners + edge-midpoints + center) rather
    than the 4-corner-only test `hardware-specs.md` already ran once —
    specifically to catch a non-linear/rotational error *between* the
    corners, the exact class of bug just observed in crosspoint-reader's
    own fixed profile on this hardware.
  - Each tap logs raw `(x,y)` next to the known expected screen position;
    at the end, a pass/fail summary flags any target whose raw reading is
    wildly inconsistent with a simple linear (even if unknown-orientation)
    mapping from its neighbors — the same "does a horizontal tap change
    the vertical result" pattern that flagged crosspoint-reader's bug.
  - Deliberately **does not** attempt to fix/reprogram the GT911's config
    from here — `touch-calibration-notes.md` §2's config-blob theory means
    a real fix (if one is even needed after this test) belongs at the
    `touch.c` driver level, not the UI. This screen's job is diagnosis:
    confirm whether our *existing* empirical mapping (already validated
    once via the 4-corner test, `hardware-specs.md`) holds up across the
    full area, before "direct tap anywhere" (§1.2) ships as a supported
    input mode.
  - Separately, the physical Home-pad zone
    (`raw_x<70 && raw_y in [660,720]`, `touch.c`) was speced to get its
    own re-tap-to-recenter control on this same screen (tap the physical
    Home pad 3× when prompted, average the raw readings, store as the
    new zone) — **not built**; the zone is still a hardcoded constant.
    Still a valid open point (`touch-calibration-notes.md` §2/§3), just
    not the one that turned out to matter for the axis-swap bug.

### 2.13 SETTINGS_SECURITY (F-03, ADR-012)

```
┌──────────────────────────────────────┐
│ < Back           Security            │
│ ──────────────────────────────────── │
│   ▸ Change PIN                       │
│                                       │
│  Auto-lock (PIN) after      [ 45 ]s  │
│  Auto-sleep after inactivity [45 ]s  │
├──────────────────────────────────────┤
│  L/R focus   tap/Home confirm        │
└──────────────────────────────────────┘
```

- **Change PIN**: current PIN (full UNLOCK-style pad, §2.1) → new PIN
  entered twice (mismatch = inline error, re-enter) → re-encrypts the
  keystore under the new key (`secret_store`'s existing
  Argon2id-derive-then-re-seal path, no new crypto primitive needed).
- **Two separate delay fields** — resolves this doc's previous open
  point: F-03's PIN re-lock and ADR-012's device-sleep are **kept as two
  independent timers**. "Auto-lock (PIN)" has no backing implementation
  yet at all (F-03's re-lock-to-UNLOCK-while-still-awake behavior was
  never built, needs `secret_store`) — this screen is still the spec for
  that missing piece, and the whole SETTINGS_SECURITY screen stays
  unbuilt until then.

> **Amendment (2026-09-19, ADR-016)**: "Auto-sleep after inactivity" was
> **built ahead of the rest of this screen**, in `SETTINGS_CONTROLS`
> instead of here — it has no dependency on `secret_store`/PIN (unlike
> every other row above), and finishing it now avoided leaving the
> already-working ADR-012 timeout REPL-only. Uses `stepper-row.h`
> (`StepperRowProps`), not the slider-row/capsule-slider pair originally
> guessed above — a plain -/+ stepper matches the "[ 45 ]s" bracket
> mockup exactly, no draggable bar needed. Step ±15s, range
> `[0,300]`, "Off" shown at 0. Will move into this screen once
> `secret_store` exists and "Change PIN"/"Auto-lock (PIN)" are real,
> so the screen ships as a whole rather than partially disabled.

### 2.14 SETTINGS_DISPLAY (F-16, should-have)

```
┌──────────────────────────────────────┐
│ < Back           Display             │
│ ──────────────────────────────────── │
│  Frontlight                  [ Off ] │
│  Color              ( Warm | Cool )  │
│  Intensity          ░░░░████░░░░░░   │
│  Auto-off after            [ 30 ]s   │
├──────────────────────────────────────┤
│  L/R focus   tap/Home confirm        │
└──────────────────────────────────────┘
```

Whole screen is F-16 (should-have) — grayed out/hidden entirely until the
frontlight driver is wired into the UI layer (the GPIO8/GPIO9 warm/cool
PWM driver itself is already validated at the hardware level per
`hardware-specs.md`; only the UI binding is missing). Matches
FEATURES.md's stated defaults: off by default, 30 s auto-off.

> **Amendment (2026-09-19, ADR-016) — built**: `frontlight.c`/`.h`
> (native LEDC driver, 2 channels) + this screen, live in
> `boards/x4pro/app/`. Intensity uses `slider-row.h` (buttons-only,
> `sliderAction` left unset — the capsule is a visual bar, not
> draggable, keeping this a 0-code-added-per-drag-math feature).
> Auto-off after Xs only cuts the physical output; it does not clear the
> persisted "Frontlight" on/off preference (`ui_nav.cpp`'s
> `s_frontlight_lit` tracks the runtime state separately) — the next
> explicit toggle relights it.
>
> **Amendment (2026-09-20, ADR-016) — first hardware round, 3 changes**:
> **Color reversed from the discrete Warm/Cool choice above to a
> continuous 0-100 warm↔cool mix** (`frontlight_apply()` drives both LEDC
> channels at once, linear crossfade) — a plain toggle felt wrong for
> what reads as a color-temperature control once tested by hand. Surfaced
> as 3 stops (Warm/Neutral/Cool, `stepper-row.h`) rather than a full
> slider — an in-between position has no meaningful numeric target
> without a color-temperature readout this device doesn't have.
> **Auto-off changed from a linear ±5s stepper (min 5s, couldn't reach 0)
> to 4 discrete presets**: Off/30s/1min/2min — the fine steps had no real
> meaning below ~30s. **Color and Intensity are now greyed
> (`.enabled` tied to the Frontlight toggle)** while the light is off.
> Intensity's control also enlarged (taller bar, bigger +/- targets)
> after user comparison against `crosspoint-reader`'s equivalent.
> Frontlight on/off/color/intensity/auto-off end-to-end and the
> greyed-while-off state are hardware-validated as of this round; the
> continuous color blend, discrete auto-off presets, and enlarged
> Intensity control are new since and not yet retested.
>
> **Amendment (2026-09-20, continued) — Intensity switched from a slider
> to a stepper**: the capsule bar was never wired to drag/tap
> (`sliderAction` left unset from the start — see this file's first
> ADR-016 amendment) — an undraggable slider misrepresented itself as
> interactive when only its +/- buttons ever did anything, and the "NN%"
> value text already communicates the level without a bar. Now a
> `StepperRowProps` like the other three controls on this screen. All
> four (Color, Intensity, Auto-off here; the idle-timeout stepper on
> Controls & Calibration) share one `apply_big_stepper_style()` helper:
> a visibly bordered button box (`sliderRowStepStyles()`, previously only
> applied automatically inside `slider-row.h`) and a bigger, thicker
> hand-drawn +/- glyph — `stepperRow`'s own default had neither, reading
> as smaller/less clearly tappable than Intensity's old slider buttons.
> Known cosmetic gap carried over from the color-step fix: the vendored
> `StepperRowProps` doesn't propagate `.row.enabled` to its own buttons,
> so Intensity's +/- also stay visually "live" while Frontlight is off —
> guarded in `handle_action()` instead (frozen vendored copy, ADR-010
> pt.3). Not yet hardware-validated.
>
> **Amendment (2026-09-20, continued) — "Auto-off after" removed, merged
> into the device's own idle-sleep timeout**: user request — a
> light-only inactivity timer alongside `SETTINGS_CONTROLS`' own
> "Auto-sleep after inactivity" was two settings for one job, since going
> to sleep already turns the frontlight off (`frontlight_off()`,
> `splash.cpp`). `FrontlightAutoOffS` and its whole UI row/action/runtime
> check removed; the idle-sleep timeout is now the single inactivity
> timer governing both the device sleeping and the light going out (as a
> side effect of that same sleep). SETTINGS_DISPLAY is now Frontlight +
> Color + Intensity only.

### 2.15 TIME_SYNC (F-02, ADR-001/ADR-006)

```
┌──────────────────────────────────────┐
│ < Back         Time & Sync           │
│ ──────────────────────────────────── │
│  Last sync: 12 days ago              │
│  Drift estimate: ±20 ppm             │
│                                       │
│   ▸ Sync via Serial (USB)            │
│   ▸ Sync via Bluetooth (CTS)         │
│   ▸ Sync via Wi-Fi                   │
│   ▸ Enter time manually              │
├──────────────────────────────────────┤
│  L/R focus   tap/Home confirm        │
└──────────────────────────────────────┘
```

- Health line always visible; turns into a visible alert style (inverted
  block, matching the sleep/ready screens' "ASLEEP"/"READY" convention)
  once age > 45 days (F-02's stated threshold).
- **Serial/BLE** (ADR-006, "trusted terminal"): a single "syncing..."
  screen with a spinner-free progress indicator (static text updates, no
  animation) — success returns here with an updated "Last sync" line;
  failure shows an inline error, no popup.
- **Wi-Fi**: leads to an SSID/password entry (2 text fields, same
  full-screen keyboard as §2.5/2.8) then the same "syncing..." screen.
  Per ADR-001/ADR-006: credentials are used once and never persisted —
  this screen's fields are never pre-filled from a previous sync, by
  design.
- **Manual entry**: date + HH:MM fields with the "minute-boundary" trick
  (FEATURES.md F-02): the user sets HH:MM, then confirms exactly when
  their reference clock reaches :00 — the UI's role here is just to make
  that confirm tap unambiguous (a single large "Confirm at :00" button,
  nothing else focusable in that moment to avoid a mis-tap).

> **Amendment (2026-09-24/26) — as built**: one screen "Time" with three
> tabs, **Wi-Fi / BLE / Manual** (`ui_screen_time.cpp`, details in
> `time-sync-design.md`), instead of the menu of four rows above. Manual
> entry is a 12-digit keypad (date + time) plus a UTC offset stepper, not
> the "Confirm at :00" button. Differences still to close:
> - **Serial (USB)** has no tab: it will be a console command, `settime`
>   (ROADMAP 8.0 P4) — the PC is the one driving it, no screen needed.
> - **Health line** ("Last sync… / drift…") is not on this screen: last
>   sync and drift per day are in Settings > About, and the "too old"
>   alert is the HOME alert button + ALERTS screen (§1.3, §2.21, ADR-018).
> - **BLE confirmation** before applying the received time (ROADMAP 8.0 P5).

### 2.16 SETTINGS_BACKUP (F-06b)

```
┌──────────────────────────────────────┐
│ < Back          Backup (SD)          │
│ ──────────────────────────────────── │
│   ▸ Export to SD                     │
│   ▸ Import from SD                   │
├──────────────────────────────────────┤
│  L/R focus   tap/Home confirm        │
└──────────────────────────────────────┘
```

- **Export**: prompts for the dedicated export passphrase (or "use device
  PIN" toggle, ADR-005) via the full-screen keyboard, then a progress
  screen (SD mount → write `x4pro-export-vN.bin` → unmount), then a
  success/failure summary (filename shown).
- **Import**: file picker is unnecessary (F-06b's format is a single,
  fixed, dated filename convention) — shows the most recent
  `x4pro-export-*.bin` found on the card with its timestamp, asks for the
  export passphrase, then an explicit **destructive-action confirmation**
  (§2.18 pattern: current keystore will be overwritten) before writing.

### 2.17 SETTINGS_OWNER_INFO (F-19)

```
┌──────────────────────────────────────┐
│ < Back         Owner info            │
│ ──────────────────────────────────── │
│  Show contact info on sleep screen   │
│                             [ Off ]  │
│                                       │
│  Phone / email                       │
│  [ ____________________________ ]   │
├──────────────────────────────────────┤
│  L/R focus   tap/Home confirm        │
└──────────────────────────────────────┘
```

Storage needs a **string setting type**, which `settings_store` does not
have yet (BOOL/U32 only) — ROADMAP 8.0 P3.

Directly implements F-19's "optional owner contact info ... configurable
in Settings, disabled by default". The text field is only reachable/
editable when the toggle above is On — this info is shown on the sleep
screen (F-19), which is visible to anyone who finds a lost/stolen device,
so it must stay opt-in rather than defaulting to "on" with a blank field.
Feeds `board_sleep_screen_show()` (`splash.cpp`) — currently a hardcoded
absence of this line; this screen is the missing UI for it.

### 2.18 Destructive-action confirmation (shared pattern, not a top-level
screen)

```
┌──────────────────────────────────────┐
│              Delete entry?           │
│                                       │
│         "GitHub" will be removed.    │
│              This cannot be undone.  │
│                                       │
│        ┌────────┐  ┌────────┐        │
│        │ Cancel │  │ Delete │        │
│        └────────┘  └────────┘        │
└──────────────────────────────────────┘
```

Reused for: TOTP/password delete (§2.5/2.8), keystore-import overwrite
(§2.16). Built as one shared LVGL modal in `ui_widgets.*` (ADR-017; the
FreeInkUI `option-dialog.h` originally named here no longer exists in the
app). Default focus on `Cancel` — a destructive action must never be the
path of least resistance through Left/Right+Home alone.

### 2.19 Full-screen text entry (shared pattern, not a top-level screen)

```
┌──────────────────────────────────────┐
│ < Cancel                      Done   │
│ ──────────────────────────────────── │
│  [ GitHub_______________________ ]   │
│                                       │
│  ┌─┬─┬─┬─┬─┬─┬─┬─┬─┬─┐               │
│  │Q│W│E│R│T│Y│U│I│O│P│               │
│  ├─┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┐              │
│  │A │S│D│F│G│H│J│K│L│⌫│              │
│  ├──┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬─┴──┐            │
│  │ Z │X│C│V│B│N│M│  space │            │
│  └───┴─┴─┴─┴─┴─┴─┴────────┘            │
├──────────────────────────────────────┤
│  L/R focus   tap/Home: press key     │
└──────────────────────────────────────┘
```

- **Built 2026-09-24** as `ui_keyboard.cpp` (LVGL, 4 letter rows 7/7/6/6 +
  control row, layers lower/upper/digits+symbols, `time-sync-design.md`
  §3) — not the classic 10-key QWERTY drawn above. Used by the Wi-Fi
  password today. **Still to add** (ROADMAP 8.0 P7): a Base32 mode (A-Z and
  2-7 only, for TOTP secrets) and a numeric mode. Used by every text field
  in this document (TOTP label/secret, password fields, Wi-Fi SSID/pass,
  export passphrase). One shared implementation, not one per screen.
- Left/Right cycling a full QWERTY key-by-key is slow but must remain
  fully usable per §1.2's invariant — direct tap is expected to be the
  primary path here in practice.
- `Done` commits the field's value and returns to the calling screen;
  `Cancel` discards the edit for that field only (not the whole parent
  screen's other fields, if any).

### 2.20 SETTINGS_ABOUT (F-20, folds in the old placeholder's "About")

```
┌──────────────────────────────────────┐
│ < Back            About              │
│ ──────────────────────────────────── │
│  MySafeFob (MSF)                     │
│  Board: x4pro     IDF: v5.5.5        │
│  Firmware: <git rev>                 │
│                                       │
│  Battery: --  (F-16, not wired yet)  │
├──────────────────────────────────────┤
│  L/R focus   tap/Home confirm        │
└──────────────────────────────────────┘
```

Static info screen, same content as the current REPL `about` command
(F-20 — the REPL stays permanent and independent; this is just the
on-screen equivalent for when USB isn't connected).

### 2.21 ALERTS (ADR-018, added 2026-09-26)

```
┌──────────────────────────────────────┐
│  Alerts                         87%  │
════════════════════════════════════════
│ [ < Back ]                           │
│ ──────────────────────────────────── │
│  ⚠ Time sync is old                  │
│  Last sync: 2026-06-20 (Wi-Fi),      │
│  98 days ago. The clock may have     │
│  drifted; TOTP codes can be refused  │
│  if it is off by more than ~15 s.    │
│  Sync the time over Wi-Fi or BLE.    │
│              [ Set the time ]        │
│ ──────────────────────────────────── │
│  (next alert, same layout)           │
└──────────────────────────────────────┘
```

- Reached only from HOME's alert button (§1.3); Back returns to HOME.
- One block per active alert, in registry order (`alerts.c`): title line
  (with `LV_SYMBOL_WARNING`), explanation text built at screen build time
  (dates, ages, values), optional action button to the screen that fixes
  it (here Settings > Time). A divider between blocks; the content
  scrolls when it does not fit (Left/Right moves through the action
  buttons).
- If every alert has cleared by the time the screen is built: a single
  line "No active alerts".
- Generic by design: any future alert or message (RTC lost power,
  battery low, calibration missing…) is a new registry entry, not a new
  screen. Alerts are condition-based (they disappear when the condition
  is gone); acknowledge/dismiss is not part of v1.
- First alerts: *Time never synchronised* and *Time sync is old*
  (threshold `TimeSyncMaxAgeS`, default 90 days, console `setting`
  command).

---

## 3. Open points (flagged, not blocking this document's review)

- [ ] **Direct-tap hardware validation** (§1.2): confirm GT911 tap
      accuracy across the full 480×800 area, not just the calibrated Home
      zone, before relying on it in any screen above. See
      `docs/touch-calibration-notes.md` (2026-09-17) for a first
      comparison against `references/crosspoint-reader`'s working touch
      on the same hardware — likely explanation is a foreign/generic
      GT911 config blob rather than a coordinate-math bug, still
      unconfirmed.
- [x] ~~Power-short-press confirm bridge~~ — **done**: `board_ui_nav_power_confirm()`
      exists and the toggle is wired and hardware-validated (§1.2). Original note:
      needs a new
      function (e.g. `board_ui_nav_power_confirm()`) so
      `power_button_task` (`main.c`, owns GPIO3) can inject a confirm
      pulse into `ui_nav.cpp`'s `InteractionBuffer` on a qualifying short
      release — not built yet, SETTINGS_CONTROLS' toggle is currently
      spec-only.
- [ ] **Recovery-code batch entry** (§2.10): FEATURES.md only specifies
      *display*/*mark-used*; how a service's initial batch of codes gets
      typed in during 2FA activation isn't designed yet.
- [x] ~~PIN re-lock delay vs. ADR-012's 45s device-sleep timer~~ —
      **resolved** by the 2026-09-16 chrome/Settings review: kept as two
      independent timers, both surfaced and adjustable in
      SETTINGS_SECURITY (§2.13). Still needs the actual runtime-value
      plumbing (`ui_nav.cpp`'s `IDLE_TIMEOUT_MS` is a compile-time
      constant today) and the PIN re-lock behavior itself (never
      implemented at all yet) before that screen is real.
- [ ] **Alerts (ADR-018)**: alert button + ALERTS screen + `alerts.c`
      registry + `TimeSyncMaxAgeS` setting + `setting` console command —
      ROADMAP 8.0 P1/P2, not built yet.
- [ ] **`ui_mgr`'s "return-to" slot** (§1.3.1): `INTERFACES.md` §4.4 needs
      a small addition — one remembered screen id, set when Settings is tapped
      and consumed by SETTINGS's top-level Back — not yet in that
      document's `ui_show(screen_t)` contract.
- [x] ~~Frontlight~~ — **done 2026-09-19/20** (ADR-016, §2.14
      amendments): Frontlight + Color + Intensity; auto-off merged into
      the idle-sleep timeout.
- [x] ~~Battery indicator~~ — **done 2026-09-18**: text-only `NN%` in the
      fixed zone (§1.3), `boards/x4pro/app/battery.c`. No icon (no
      vendored bitmap assets, see §1.3's note) and unread on the `--`
      failure path is not yet distinguished from "not implemented" in the
      UI — acceptable for now, not hardware-validated yet.

---

*Document to be reviewed like FEATURES.md/INTERFACES.md before
implementation starts on any screen beyond the existing placeholder
(`ui_nav.cpp`) and the already-implemented SPLASH/SLEEP screens.*
