/**
 * @file splash.h
 * @brief MySafeFob App — écran d'accueil statique (board x4pro).
 *
 * Affiche une page statique sur l'e-ink au boot de l'app, pour savoir où
 * l'on est quand on quitte la factory (demande utilisateur 2026-09-14).
 * Premier slice de la Phase 8c : les drivers e-ink portés de la factory
 * (éprouvés) serviront de base au BSP complet de l'app.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Init rails + e-ink, affiche la page statique, power-off du
 *        contrôleur (l'image persiste à consommation nulle).
 *
 * Bloquant ~3-4 s (full refresh GC). À appeler une fois au boot.
 */
void board_splash_show(void);

/**
 * @brief Écran "prêt" statique, affiché juste après le splash — distingue
 *        visuellement la transition (splash, transitoire) de l'état stable
 *        (prêt/console active), pour qu'un blocage éventuel après le splash
 *        soit visible au lieu de laisser croire que le device a fini de
 *        démarrer alors qu'il est resté bloqué sur l'image de transition.
 *        Provisoire : remplacé par l'écran UNLOCK réel en tâche 8.4.
 *
 * Bloquant ~3-4 s (full refresh GC).
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
