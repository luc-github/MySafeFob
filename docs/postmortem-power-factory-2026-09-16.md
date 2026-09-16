# Post-mortem: Power button / factory switch (2026-09-16)

> Single session, ~24h of effective work. Final goal reached and validated
> on hardware with logs to back it up. This document captures the *why* —
> seven distinct bugs, independent of one another, stacked on a mechanism
> that looked trivial on paper. The exact technical details (code, values,
> diffs) live in `docs/ROADMAP.md` (ADR-009 and its amendments) and in the
> comments of the files cited; this document tells the story of the chain
> of events and what should be learned from it.

## 1. The goal, and why it looked simple

Target design, decided along the way (ADR-009 amendment):

- **Power held < 10 s**: sleep ↔ wake (classic "power button" behavior).
- **Power held ≥ 10 s**: switch to the `factory` partition, **regardless
  of the starting state** — app awake, device asleep, or even
  crashed/frozen app.

On paper: a GPIO, a timer, two thresholds. Nothing that would justify a
full day. In practice, this mechanism crosses four radically different
layers of the firmware (RTC/deep-sleep hardware, bootloader ROM,
application flash driver, FreeRTOS scheduling), and **each one** hid an
independent bug. None of the seven was visible until the previous ones
had already been fixed — they were masking each other.

## 2. The starting point: an abandoned design

The initial mechanism (inherited from the earlier reference design, ADR-007) used a
**Power + Right** combo: both buttons held together triggered the
factory switch via the bootloader hook (`hooks.c`, at the time wired to
GPIO7/Right).

Tested empirically on hardware (Test A: Power alone → systematic wake;
Test B: Power+Right together, held 10 s, released → **no reaction at
all, ever, regardless of duration**): the combo prevents the hardware
wake itself from triggering. Root cause never identified with certainty
(hypothesis: electrical interference between two RTC_IO pads held low
simultaneously during the wake latch) — but regardless of the exact
cause, the fact itself is irrefutable and cannot be worked around in
software. **Decision**: total abandonment of the combo, redesign toward
Power alone with duration thresholds. This redesign is what opened the
Pandora's box.

## 3. The seven bugs, in the order they were discovered

Each bug produced a symptom on hardware, and each symptom was first
misdiagnosed at least once before the real cause emerged — often because
the next bug was masking the effect of the previous fix.

### Bug 1 — immediate wake right after entering sleep (fixed before the redesign)

**Symptom**: "I go to sleep and wake up immediately."
**Cause**: `esp_deep_sleep_start()` was called while Power was still
physically pressed down (it's the gesture that triggered entering
sleep) — so the EXT1 condition (`ANY_LOW`) is already true at the very
moment sleep begins, causing an almost instant bounce.
**Fix**: explicitly wait for the button to be released before arming
EXT1 and sleeping (`power_mgr_shutdown()`), with a symmetrical guard on
the wake side (`seen_release` in `power_button_task()`) to prevent a
residual press at wake time from immediately re-arming a new long
press.
**Files**: `components/power_mgr/power_mgr.c`, `main/main.c`.

### Bug 2 — unreliable GPIO3 read right after an EXT1 wake

**Symptom** (after moving the hook from GPIO7/Right to GPIO3/Power):
"can't come out of sleep."
**Cause**: `esp_sleep_enable_ext1_wakeup()` routes the GPIO3 pad through
the RTC_IO domain for the duration of sleep; this configuration persists
across the wake. Reading GPIO3 as raw digital (`gpio_ll_get_level`, the
method used without issue when the hook read GPIO7 — never a wake pin)
at this point returns a frozen value, independent of the button's actual
state.
**Two attempts**:
  1. *Rejected*: never read GPIO3 on a deep sleep wake again, move the
     detection to the app side. Functionally correct but rejected on
     user feedback — see §4.
  2. *Kept*: `rtcio_ll_function_select(GPIO3, RTCIO_LL_FUNC_DIGITAL)`
     (low-level HAL header, with no driver/FreeRTOS dependency, hence
     usable in bootloader context) — the register-level equivalent of
     `rtc_gpio_deinit()` — explicitly hands control back to digital
     before any read.
**File**: `boards/x4pro/factory/bootloader_components/custom_bootloader/hooks.c`.

### Bug 3 — bootloader watchdog too short for the chosen threshold

**Symptom**: after fixing bug 2, an apparently identical symptom ("same
thing, once asleep it doesn't wake up"), then the discovery that even a
12 s hold wasn't enough to reach factory either.
**Cause**: `bootloader_init()` arms the RTC watchdog with a fixed
timeout of 9000 ms (`CONFIG_BOOTLOADER_WDT_TIME_MS`) **before** the hook
runs. Our confirmation loop deliberately blocked up to 10,000 ms without
ever feeding this watchdog — it fired at 9 s, resetting the chip
*before* reaching the threshold, which restarted the bootloader (which
in turn re-armed the same watchdog for a new 9 s max cycle). As long as
the user held the button, the 10 s threshold was **structurally
unreachable** — a silent reset loop indistinguishable, from the outside,
from a total freeze (the screen never refreshes at this stage).
**Fix**: explicitly feed the RTC watchdog (`wdt_hal_feed()`) on every
iteration of the confirmation loop — same pattern as
`bootloader_support/src/flash_encryption/flash_encrypt.c` for its own
long operations at the same boot stage.
**File**: `hooks.c`.

### Bug 4 — incorrect duration counting (~3.5x underestimate)

**Discovered on the side of bug 3**, never solely responsible for a
reported symptom but a real bug nonetheless: `is_button_pressed()`
internally blocks for ~25 ms (5 reads × 5 ms), but the confirmation loop
added **on top of that** an artificial 10 ms delay while only counting
those 10 ms toward the total — the real elapsed time per iteration
(~35 ms) was underestimated by a factor of ~3.5. Concretely: for the
counter to reach a "nominal" 10 s, the button actually had to be held
for ~35 s.
**Fix**: count the time actually elapsed (the measured internal
duration of `is_button_pressed()`), with no extra delay.
**File**: `hooks.c`.

### Bug 5 — wrong bootloader tested (a tooling bug, not a code bug)

**The costliest of the seven.** After three successive code fixes (bugs
2, 3, 4), all confirmed "clean build," all reported broken on hardware
**identically**. The cause was upstream of all the code:
`./msf_build.bat build` run from the repo root — which *every* check in
this session used — compiles a bootloader that **does not contain
`hooks.c`**. ESP-IDF only detects `bootloader_components/` if it is a
direct child of the compiled project's `PROJECT_SOURCE_DIR`; this folder
lives under `boards/x4pro/factory/`, not at the root. The bootloader
actually flashed (`installer/x4pro_app/bootloader_16MB.bin`) is
**copied** from `boards/x4pro/factory/build/bootloader/bootloader.bin`
by the app's postbuild step, without ever being rebuilt or checked for
freshness through that path.

In other words: **none of the three previous bugs had actually been
tested on real hardware** by the time they all seemed to have "failed."
The firmware actually flashed was a much older version, frozen since the
very start of the session. Each "still broken, identically" was
therefore not a signal that the fix had failed — it was proof,
misread, that nothing new had ever run at all.

**How it was detected**: as a last resort, direct inspection of the
compiled binary (`nm bootloader.elf | grep bootloader_after_init` →
pointed to ESP-IDF's weak stub, not to our hook).
**Fix**: explicitly documented build procedure (`docs/FACTORY.md` §3.4)
— always rebuild `boards/x4pro/factory` first, separately, then the
root.

### Bug 6 — stack overflow in `power_mgr_switch_to_factory()`

**Symptom**: from an awake App0, a ≥10 s hold did "nothing," or left a
degraded screen requiring a serial reset to unblock (the physical
"reset" button had no effect — consistent with a frozen task that no
longer reads the GPIO at all).
**Cause**, finally visible once the right binary was tested *with
logs*: `power_mgr_switch_to_factory()` declares
`uint8_t buf[FLASH_SECTOR_SIZE]` — a local buffer of **4096 bytes** — in
a task (`power_button_task`) created with `xTaskCreate(..., 4096, ...)`.
The buffer alone already occupied the entire allocated stack, not
counting the rest of the locals or the stack usage of the
`esp_flash_read/erase_region/write` calls themselves. Overflow was
guaranteed as soon as the function was reached —
`vApplicationStackOverflowHook` → `panic_abort` → reboot.
This retroactively explains the "slightly grayed splash" observed
earlier in the session: not e-ink ghosting, a crash-reboot mid-refresh.
**Fix**: stack raised to 12,288 bytes (generous margin, plenty of
PSRAM).
**File**: `main/main.c`.

### Bug 7 — `esp3d_log()` compiled to a silent no-op across the whole app

**Discovered while adding the diagnostic logs** that made it possible to
find bug 6: no `esp3d_log()` call (the suffix-less macro) in `main.c`
had ever actually produced output, including the boot banner, since
(presumably) this file was first written — well before this session.
`esp3d_log()` only compiles if `ESP3D_LOG >= ESP3D_LOG_LEVEL_ALL` (4),
but `ESP3D_LOG` was **never defined at all** for any component: the root
`CMakeLists.txt` called
`add_compile_options(-DESP3D_LOG=${MSF_LOG_LEVEL})` **after** `project()`,
which does not propagate to IDF components in 5.5.5 — exactly the same
trap already documented (and fixed) for `MSF_BOARD_NAME` two lines above
in this same file, never applied to this line.
**Fix**: `idf_build_set_property(COMPILE_DEFINITIONS "ESP3D_LOG=..."
APPEND)`, same mechanism as for `MSF_BOARD_NAME`. The `esp3d_log()`
calls in `main.c` were also switched back to `esp3d_log_d()` (debug
level, consistent with `MSF_LOG_LEVEL=3`).
**Files**: `CMakeLists.txt` (root), `main/main.c`.

## 4. The rejected detour: app-level rather than bootloader-level

Between bugs 2 and 3, a first fix attempt (moving the "Power held ≥ 10 s
since wake" measurement to the app side rather than into the hook) was
**explicitly rejected on user feedback**: *"switching to factory should
be handled bootloader-side, I think."* Reason: if the app crashes or
freezes, an app-only measurement can never guarantee access to factory
— whereas a bootloader mechanism runs before any application code, and
therefore stays reachable even with a dead app.

This choice turned out to be right and was empirically validated much
later in the session (see §6): an application crash (bug 6 included)
reboots via `esp_restart_noos()`, which produces
`RESET_REASON_CPU0_SW`/`CPU1_SW` — a reset type the hook **still
checks** normally (only `RESET_REASON_CORE_SW`, reserved for resets
already driven by code that itself set otadata, is short-circuited).
The hook therefore remains the fallback path even during a crash loop,
with no extra action needed.

## 5. Numbers summary

| # | Bug | Layer | Reported symptom | Detected via |
|---|---|---|---|---|
| — | Power+Right combo | Hardware/RTC | Wake never triggers with both buttons | Targeted Test A/B |
| 1 | Immediate wake post-sleep | `power_mgr.c` | Instant sleep↔wake bounce | Direct observation |
| 2 | RTC_IO pad not handed back to digital | `hooks.c` | No more wake at all after switching to GPIO3 | ESP-IDF doc + HAL source reading |
| 3 | Bootloader RTC WDT (9s) < threshold (10s) | `hooks.c` | Same symptom after fix #2, neither wake nor factory at 12s | Reading `bootloader_init.c` source |
| 4 | Duration counting ×3.5 underestimated | `hooks.c` | (masked by #3, never symptomatic on its own) | Careful re-reading of the loop |
| 5 | Wrong bootloader tested (tooling) | build system | 3 consecutive code fixes "with no effect" | `nm`/`grep` on the compiled binary |
| 6 | Stack overflow (4KB buffer / 4KB stack) | `main.c` | Nothing, or frozen screen, serial reset required | Diagnostic logs added on request |
| 7 | `esp3d_log()` never compiled | root `CMakeLists.txt` | Total silence on the application log side | Search for strings missing from the binary |

**Seven bugs, five different layers** (hardware/RTC, bootloader ROM,
build system/tooling, application flash driver, FreeRTOS/build config),
**none visible without having already fixed at least one of the
others**.

## 6. Final validation (hardware, with logs)

Sequence confirmed working end-to-end, logs to back it up:

- **Sleep** (app awake, held ≥1.5s then released): `Power long: entering
  sleep` → sleep screen → deep sleep. ✅
- **Short wake** (asleep, brief tap): normal return to app, splash →
  ready. ✅
- **Clean cancellation** (asleep, held < 10s then released, either on
  hook or app side): `released too soon` → normal boot. ✅
- **Factory from wake** (asleep, held continuously ≥10s): bootloader
  hook `threshold reached` → `otadata erased` → factory boot. ✅
- **Factory from an awake app** (App0, held continuously ≥10s):
  `power_mgr_switch_to_factory()` completes without a crash →
  `esp_restart()` → otadata already empty → factory boot. ✅ (validated
  after the bug 6 fix)
- **Crash-loop resilience**: confirmed by the observed hook behavior on
  an `RTC_SW_CPU_RST` reset (the very one from bug 6, before its fix) —
  the hook correctly re-checked the button on this reset, proving the
  fallback path stays active even after an application crash.

## 7. Remaining trade-off, accepted and documented (not a "bug")

After a factory switch triggered app-side, the bootloader hook
*still* re-checks the button on the reboot that follows (since
`esp_restart()` produces `CPU0_SW`/`CPU1_SW`, different from the
`CORE_SW` that the hook short-circuits) — the user can therefore end up
holding Power through a redundant confirmation delay after the app has
already succeeded in switching itself. **Deliberately not fixed**: the
obvious fix (also ignoring `CPU0_SW`/`CPU1_SW`) would reopen the
crash-loop blind spot from §4, since a crash produces exactly the same
reset type as an intentional `esp_restart()` — impossible to
distinguish them by reset reason alone. A real solution would require
an explicit marker (e.g. a byte written by the app right before its own
restart, read and cleared by the hook) — not implemented at this stage,
left as an open decision.

## 8. Avoidable or unavoidable? An honest look at the approach

Question raised at the end of the session: were these seven bugs (eight
counting the combo) unavoidable — solvable only through
experiment/fix cycles on real hardware — or would better upfront
discipline have avoided some of them? Honest answer: **a mix of both,
but the majority was avoidable**. Bug-by-bug breakdown:

| # | Bug | Category | Justification |
|---|---|---|---|
| — | Power+Right combo never wakes | **Unavoidable** | Empirical electrical/RTC behavior, undocumented, not derivable from reading code or a datasheet — only an isolated hardware test (A/B) could reveal it. |
| 1 | Immediate wake post-sleep | **Partially avoidable** | The EXT1-ANY_LOW-already-true-on-entering-sleep bug is a known, documented ESP-IDF trap (looked up *a priori*, not stumbled upon) — but its precise manifestation is only observable through actual button use, not from reading code alone. |
| 2 | RTC_IO pad not handed back to digital | **Avoidable with more upfront research** | `rtc_gpio_deinit()` exists precisely for this documented ESP-IDF use case (reusing an EXT1 wake pin as a regular GPIO afterward). Reading the `esp_sleep`/`rtc_io` docs *before* writing the hook (rather than after the failure) would have flagged it. |
| 3 | Bootloader RTC WDT (9s) < threshold (10s) | **Avoidable** | `CONFIG_BOOTLOADER_WDT_TIME_MS` is a visible `Kconfig` entry, and `bootloader_init.c` (~200 lines) is short. Checking "what could interrupt a blocking 10s loop at this boot stage" *before* writing the loop — rather than after seeing it fail twice — would have avoided it. |
| 4 | Duration counting ×3.5 underestimated | **Avoidable** | Local arithmetic error in ~10 lines of code, detectable by a line-by-line re-read or a hand calculation of the real elapsed time per iteration. No hardware needed to find it. |
| 5 | Wrong bootloader tested (tooling) | **Avoidable — and the costliest of the seven** | The exact comment explaining the trap (`postbuild.cmake`: *"the app build's bootloader does NOT have the hook"*) already existed in the codebase, written in an earlier session. It wasn't re-read/applied before launching three fix cycles. The rule that would have avoided this cost: **as soon as a hardware result contradicts a fix supposedly applied, verify by direct binary inspection (`nm`, `grep`) that the fix actually reached the hardware — before formulating a new bug hypothesis.** |
| 6 | Stack overflow (4KB buffer / 4KB stack) | **Avoidable** | Basic embedded discipline: any addition of a local buffer of notable size (here, exactly `FLASH_SECTOR_SIZE`) in a function called from an existing task calls for an immediate check of that task's stack. Should have been done when writing `power_mgr_switch_to_factory()`, not after observing a crash. |
| 7 | `esp3d_log()` never compiled | **Avoidable — and the codebase already knew it** | The comment documenting exactly this CMake trap (`add_compile_options()` after `project()`) was already present in the same file, applied to `MSF_BOARD_NAME` right above. The pattern simply wasn't generalized to the next line when it was written. |

**Summary**: out of eight problems, **only one** (the Power+Right combo)
was truly irreducible to anything but a hardware experimentation cycle
— its cause, in fact, remains unconfirmed with certainty to this day.
The other seven each had a signal available *before* the first failed
test: an ESP-IDF doc to read, a config file to grep, a calculation to
verify by hand, a comment already written in the codebase itself. Bug
#5 (wrong binary tested) is the clearest case: on its own it multiplied
the number of required cycles by ~3, making what was an already
documented tooling problem look like "stubborn hardware."

**What would have shortened the session the most**, in likely order of
impact:
1. Verify the artifact actually being tested (bug #5) at the first
   unexpected hardware result, not after the third.
2. Add diagnostic logging (which unblocked bugs #5, #6 and revealed #7)
   *before* the first fix attempt on hardware, not after several
   failures — every round without logs cost a full
   rebuild/reflash/retest cycle for an ambiguous result.
3. Systematically re-read the comments already present in the files
   being touched before modifying them (bugs #5 and #7 both had their
   solution already written, elsewhere in the same file or an adjacent
   one, before even starting).

## 9. Lessons learned (for this codebase, and beyond)

1. **"Clean build" proves nothing about the content of the flashed
   binary.** A multi-project setup with shared/copied artifacts (here: a
   bootloader shared between app and factory) can run an old binary
   while you believe you're testing new code, with no visible error.
   Verify by direct inspection (`nm`, grep for known strings) as soon as
   a symptom persists identically after several supposedly independent
   fixes.
2. **`add_compile_options()`/`add_compile_definitions()` after
   `project()` do not propagate to ESP-IDF components (5.5.5).** A trap
   hit twice in this single file (`MSF_BOARD_NAME`, then `ESP3D_LOG`) —
   `idf_build_set_property(COMPILE_DEFINITIONS ...)` is the only
   reliable method after `project()`.
3. **A pin used for EXT1 wake cannot be re-read as raw digital without
   explicitly handing control back to the digital domain**
   (`rtcio_ll_function_select(..., RTCIO_LL_FUNC_DIGITAL)`), including in
   bootloader context.
4. **Any deliberate blocking loop at the bootloader level must feed the
   RTC watchdog** if its duration can exceed
   `CONFIG_BOOTLOADER_WDT_TIME_MS` — otherwise the watchdog protects the
   bootloader against... the bootloader itself.
5. **Size a task's stack based on its largest local buffers**, not on a
   generic estimate — a buffer the size of a flash sector (4 KB) is a
   frequent case as soon as `esp_flash_*` is used directly.
6. **The reset reason alone is not enough to distinguish a crash from an
   intentional restart** if both go through the same path
   (`esp_restart_noos()`) — an explicit state is needed to go further
   than "always check just in case."
7. **Getting real logs beats any hypothesis, however well argued.**
   Several rounds in this session (bugs 2, 3, 6) saw a plausible but
   wrong or incomplete hypothesis proposed and then fixed "blindly,"
   before an actual log (bug 5 unblocked, bug 7's logging fixed)
   revealed the exact cause in a single read. When a supposedly solid
   fix changes nothing on hardware, the priority becomes getting
   visibility (logs), not formulating a new hypothesis.

## 10. References

- `docs/ROADMAP.md` — ADR-009 and its amendments (full technical detail
  of each fix, in chronological order).
- `docs/FACTORY.md` §3.4 — correct build procedure (bug #5).
- `boards/x4pro/factory/bootloader_components/custom_bootloader/hooks.c`
  — bootloader hook (bugs #2, #3, #4).
- `components/power_mgr/power_mgr.c`, `main/main.c` — app-level logic
  (bugs #1, #6, #7).
- `CMakeLists.txt` (root) — bug #7.
