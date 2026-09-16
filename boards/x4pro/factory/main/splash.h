/**
 * @file splash.h
 * @brief MySafeFob Factory — splash statique au boot (ADR-010 amende
 *   2026-09-15 : FreeInkUI::DisplayTarget, copie figee propre a la factory,
 *   cf. components/freeinkui/CMakeLists.txt).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Dessine et affiche le splash (resources/splash.png, converti par
 *        tools/gen_splash.py) en full refresh. eink_init() doit deja avoir
 *        ete appele.
 */
void splash_show(void);

#ifdef __cplusplus
}
#endif
