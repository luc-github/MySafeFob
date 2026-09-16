/**
 * @file power_mgr.h
 * @brief MySafeFob — gestion on/off logiciel (contrat, ADR-009).
 *
 * Le bouton Power (GPIO3, actif-LOW) est une simple entree : la fonction
 * on/off est realisee par DEEP SLEEP + wake-up GPIO. Le device vit "eteint"
 * entre deux usages ; les codes TOTP sont calcules a la demande au reveil
 * (le RTC BM8563 tient l'heure sur batterie).
 *
 * Sequence d'arret (power_mgr_shutdown) :
 *   1. e-ink power-off (image conservee — bistable)
 *   2. frontlight off, rails optionnels coupes (hold RTC)
 *   3. esp_deep_sleep_start() + wake-up GPIO3 LOW (pull-up RTC)
 *
 * Au reveil : reboot S3, esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1
 *   (GPIO3 = RTC IO -> EXT1 ANY_LOW ; le wake-up GPIO digital n'existe pas
 *   sur S3 : pas de SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP)
 *   => l'app saute directement a l'ecran UNLOCK.
 *
 *   SEMANTIQUE BOUTONS (ADR-009 amende 2026-09-16) : **Power seul**, mesure
 *   de duree, plus de combo Power+Right — abandonne car empiriquement le
 *   combo empechait le reveil lui-meme de se declencher (teste sur
 *   hardware : Power seul reveille systematiquement, Power+Right ensemble
 *   ne reveille JAMAIS, quelle que soit la duree de maintien — probleme en
 *   amont du hook bootloader, pas un souci de timing logiciel).
 *     - Power tenu < 10 s (depuis l'app eveillee) => deep sleep normal.
 *     - Power tenu >= 10 s (eveille OU pendant la fenetre de reveil
 *       depuis la veille) => bascule factory :
 *         - Eveille : power_mgr_switch_to_factory() (logiciel, esp_ota).
 *         - Endormi : hooks.c (bootloader) mesure directement la duree de
 *           maintien de GPIO3 (meme seuil 10 s), plus de dependance a
 *           GPIO7 pendant la fenetre de reveil.
 *   Left+Power reste impossible (GPIO0 strapping -> download mode).
 *
 * STATUS (2026-09-14): wake/shutdown flow wired for validation —
 * power_mgr_init() at boot logs the wake cause, the REPL `sleep` command
 * draws the board sleep screen then enters deep sleep. Board deinit
 * (e-ink POF from power_mgr itself, rails, frontlight) still TODO 8c.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure la source de reveil (GPIO Power, actif-LOW) et le
 *        pull-up RTC. A appeler une fois au boot, avant toute veille.
 */
esp_err_t power_mgr_init(void);

/**
 * @brief Vrai si le boot courant est un reveil par bouton Power (deep sleep).
 *        L'app l'utilise pour sauter le splash et aller direct a UNLOCK.
 */
bool power_mgr_wakeup_from_power(void);

/**
 * @brief Etat de la batterie cote gestion d'energie : true si le seuil
 *        critique est atteint et qu'un shutdown imminent est recommande.
 *        (Implementation avec CW2017 en 8c ; retourne false par defaut.)
 */
bool power_mgr_battery_critical(void);

/**
 * @brief Met le device en veille profondee. NE RETOURNE JAMAIS
 *        (reboot au reveil). Consomme les deinitialisations board
 *        (e-ink POF, frontlight, rails) avant esp_deep_sleep_start().
 */
void power_mgr_shutdown(void);

/**
 * @brief Bascule logicielle vers la partition factory (Power tenu >= 10 s
 *        depuis l'app eveillee — ADR-009 amende 2026-09-16). Sauvegarde
 *        otadata @0xB000 (meme contrat que hooks.c / factory main.c — la
 *        factory restaure ce backup a son demarrage, donc un Cancel depuis
 *        la factory revient proprement sur l'app), efface otadata, puis
 *        esp_restart(). NE RETOURNE JAMAIS en cas de succes ; en cas
 *        d'echec (lecture/ecriture flash), retourne et log l'erreur —
 *        l'appelant reste eveille, ne rentre pas en veille par erreur.
 */
void power_mgr_switch_to_factory(void);

#ifdef __cplusplus
}
#endif
