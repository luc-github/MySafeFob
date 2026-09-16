/**
 * @file splash.h
 * @brief MySafeFob App — static welcome screen (board x4pro).
 *
 * Displays a static page on the e-ink at app boot, so you know where
 * you are when leaving the factory (user request 2026-09-14).
 * First slice of Phase 8c: the e-ink drivers ported from the factory
 * (proven) will serve as the base for the app's full BSP.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Init rails + e-ink, displays the static page, powers off the
 *        controller (the image persists at zero power).
 *
 * Blocking ~3-4s (full refresh GC). Call once at boot.
 */
void board_splash_show(void);

/**
 * @brief Static "ready" screen, displayed right after the splash — visually
 *        distinguishes the transition (splash, transient) from the stable
 *        state (ready/console active), so that a possible hang after the
 *        splash is visible instead of suggesting the device has finished
 *        booting when it's actually stuck on the transition image.
 *        Provisional: replaced by the real UNLOCK screen in task 8.4.
 *
 * Blocking ~3-4s (full refresh GC).
 */
void board_ready_show(void);

/**
 * @brief Draw the deep-sleep screen (static, then controller POF).
 *
 * Shown right before power_mgr_shutdown(): the panel is bistable, so the
 * "device asleep" image persists at zero power until the next wake.
 * Deliberately does NOT mention the factory-rescue combo (DECISIONS §16).
 *
 * Blocking ~3-4 s (full refresh GC). Never returns on failure either —
 * the caller proceeds to deep sleep regardless (a missing image must not
 * prevent sleeping).
 */
void board_sleep_screen_show(void);

#ifdef __cplusplus
}
#endif
