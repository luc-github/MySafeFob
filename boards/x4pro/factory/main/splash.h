/**
 * @file splash.h
 * @brief MySafeFob Factory — static splash at boot (ADR-010 amended
 *   2026-09-15: FreeInkUI::DisplayTarget, frozen copy specific to the factory,
 *   see components/freeinkui/CMakeLists.txt).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Draws and displays the splash (resources/splash.png, converted by
 *        tools/gen_splash.py) with a full refresh. eink_init() must
 *        already have been called.
 */
void splash_show(void);

#ifdef __cplusplus
}
#endif
