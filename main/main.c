/**
 * @file main.c
 * @brief MySafeFob — point d'entrée applicatif (squelette Phase 8).
 *
 * Pour l'instant : banner + self-tests TOTP + console REPL minimale.
 * Le boot applicatif réel (UNLOCK -> UI) arrive avec la tâche 8.2/8.4.
 *
 * Logging applicatif : esp3d_log (hooks -> historique erreurs à l'écran
 * en 8.4). Les drivers/board restent sur ESP_LOG* (écosystème IDF).
 */
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

#include "esp_err.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp3d_log.h"
#include "totp_engine.h"
#include "secret_store.h"
#include "power_mgr.h"

static bool s_wake_from_sleep = false;

/* Splash e-ink au boot — implémenté par le composant board
 * (boards/<board>/app/splash.c, déclaré via EXTRA_COMPONENT_DIRS dans
 * board_config.cmake). Pas de symbole faible : avec une archive statique,
 * le faible satisfait la référence et l'implémentation forte n'est jamais
 * tirée du lien (constaté au build 2026-09-14). Un board sans e-ink doit
 * fournir un stub, le link error est explicite. */
void board_splash_show(void);
void board_ready_show(void);          /* écran "prêt", après le splash */
void board_sleep_screen_show(void);   /* écran de veille (deep sleep) */

/* BUGFIX 2026-09-16 : cmd_sleep() (REPL, sa propre tache esp_console) et
 * power_button_task() (appui physique Power) menent chacun independamment
 * vers board_sleep_screen_show()+power_mgr_shutdown() (et power_button_task
 * vers power_mgr_switch_to_factory()) sans aucune exclusion mutuelle. Deux
 * taches FreeRTOS distinctes declenchant l'une de ces sequences en meme
 * temps toucheraient l'e-ink concurremment (eink.c utilise des buffers
 * `static` dans ses fonctions de transfert ligne par ligne, pas de mutex) :
 * corruption plausible de l'affichage (SPI entrelace), etat indefini.
 * Un seul appelant peut "revendiquer" une transition terminale (veille ou
 * factory) — le second, s'il arrive, est ignore plutot que de s'executer
 * en parallele. */
static atomic_bool s_terminal_action_claimed = false;

static bool claim_terminal_action(void)
{
    bool expected = false;
    return atomic_compare_exchange_strong(&s_terminal_action_claimed, &expected, true);
}

static int cmd_about(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("MySafeFob (MSF) — squelette Phase 8\n");
    printf("  board : %s\n", MSF_BOARD_NAME);
    printf("  IDF   : %s\n", esp_get_idf_version());
    printf("  wake  : %s\n", s_wake_from_sleep ? "deep-sleep (Power)" : "cold boot");
    return 0;
}

static int cmd_sleep(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (!claim_terminal_action()) {
        printf("Deja en cours (bouton Power presse ?) — ignore.\n");
        return 0;
    }
    printf("Deep sleep : ecran de veille, puis veille. Reveil = Power (GPIO3).\n");
    printf("(Power tenu >= 10s = bascule factory — cf. ADR-009)\n");
    vTaskDelay(pdMS_TO_TICKS(500));   /* laisser le message sortir sur USB */
    board_sleep_screen_show();
    power_mgr_shutdown();
    return 0;   /* jamais atteint */
}

static int cmd_totpselftest(int argc, char **argv)
{
    (void)argc; (void)argv;
    esp_err_t err = totp_engine_run_self_tests();
    if (err == ESP_OK) {
        printf("TOTP self-tests (RFC 6238) : OK\n");
    } else {
        printf("TOTP self-tests : ECHEC (%s)\n", esp_err_to_name(err));
    }
    return 0;
}

/* -----------------------------------------------------------------------
 * Bouton Power (ADR-009 amende 2026-09-16) — version squelette, remplacee
 * par l'indev LVGL en 8.4. GPIO3 (Power, actif-LOW), mesure de duree
 * unique (plus de combo Right — abandonne, cf. power_mgr.h) :
 *   - relache < MSF_POWER_LONG_MS   : rien (appui court ignore ici)
 *   - maintenu >= MSF_POWER_LONG_MS et < MSF_POWER_FACTORY_MS : veille
 *   - maintenu >= MSF_POWER_FACTORY_MS : bascule factory (esp_ota, logiciel)
 * ----------------------------------------------------------------------- */
#define MSF_BTN_POWER_PIN      GPIO_NUM_3
#define MSF_POWER_LONG_MS      1500
#define MSF_POWER_FACTORY_MS   10000

static void power_button_task(void *arg)
{
    (void)arg;
    /* GPIO3 deja configure par power_mgr_init */

    bool held = false;
    int64_t t0_us = 0;
    bool factory_triggered = false;
    int64_t next_log_ms = 0;   /* diagnostic 2026-09-16 : progression toutes les ~1s */
    /* BUGFIX 2026-09-16 (symetrique au fix de power_mgr_shutdown) : au
     * reveil, Power est encore physiquement enfonce (c'est le geste qui a
     * reveille le device) — sans ca, cette tache armerait son timer de
     * long-press immediatement sur cet appui residuel, et rendormirait le
     * device 1,5 s plus tard sans jamais laisser voir l'app. On exige un
     * relachement observe au moins une fois avant d'armer un premier appui.
     *
     * NOTE 2026-09-16 : le cas "Power tenu >= 10 s en continu depuis la
     * veille" n'est PAS gere ici — il est resolu par le hook bootloader
     * (hooks.c) AVANT que cette tache (et l'app en general) ne demarre :
     * la bascule factory reste ainsi disponible meme si l'app plante ou se
     * bloque, ce qu'une mesure uniquement cote app ne pourrait pas garantir.
     * Cette tache ne gere que les appuis effectues APRES que l'app tourne. */
    bool seen_release = false;

    while (1) {
        bool p = gpio_get_level(MSF_BTN_POWER_PIN) == 0;

        if (!p) {
            seen_release = true;
        }
        if (p && !held && seen_release) {
            held = true;
            factory_triggered = false;
            t0_us = esp_timer_get_time();
            next_log_ms = 1000;
            esp3d_log_d("Power presse — detection en cours...");
        }
        if (held && p) {
            int64_t held_ms = (esp_timer_get_time() - t0_us) / 1000;
            if (held_ms >= next_log_ms) {
                esp3d_log_d("Power: maintenu %lld ms", (long long)held_ms);
                next_log_ms += 1000;
            }
            if (!factory_triggered && held_ms >= MSF_POWER_FACTORY_MS) {
                factory_triggered = true;
                if (claim_terminal_action()) {
                    esp3d_log_d("Power >= 10s : bascule vers factory");
                    power_mgr_switch_to_factory();   /* ne retourne jamais si OK */
                    esp3d_log_d("Power: bascule factory ECHOUEE, on reste eveille");
                    atomic_store(&s_terminal_action_claimed, false);
                } else {
                    esp3d_log_d("Power >= 10s : transition deja en cours (REPL sleep ?), ignore");
                }
            }
        }
        if (!p && held) {
            int64_t held_ms = (esp_timer_get_time() - t0_us) / 1000;
            if (!factory_triggered && held_ms >= MSF_POWER_LONG_MS) {
                if (claim_terminal_action()) {
                    esp3d_log_d("Power long: mise en veille");
                    board_sleep_screen_show();
                    power_mgr_shutdown();               /* jamais atteint */
                } else {
                    esp3d_log_d("Power long: transition deja en cours (REPL sleep ?), ignore");
                }
            }
            held = false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void)
{
    esp3d_log_init();

    esp3d_log_d("======================================");
    esp3d_log_d("  MySafeFob (MSF) — squelette Phase 8");
    esp3d_log_d("  board=%s  IDF=%s", MSF_BOARD_NAME,
              esp_get_idf_version());
    esp3d_log_d("======================================");

    /* Wake source + EXT1 config (ADR-009) — before anything else, so the
     * wake-cause log is always first. Flow validation: normal wake resumes
     * the app (splash skipped); Power+Right held at wake is intercepted by
     * the bootloader hook and never reaches this code. */
    ESP_ERROR_CHECK(power_mgr_init());
    s_wake_from_sleep = power_mgr_wakeup_from_power();

    if (s_wake_from_sleep) {
        /* Interim feedback (session 2026-09-14 : wake fonctionnel mais
         * invisible -> "le bouton ne fait rien"). Remplace par l'ecran
         * UNLOCK en 8.4 ; a garder en dev pour valider le flux wake. */
        esp3d_log_d("Wake from deep sleep — showing splash (UNLOCK screen in 8.4)");
        board_splash_show();
    } else {
        /* Page statique e-ink (no-op si le board n'en a pas) : ~3-4 s
         * bloquantes, avant la REPL — on sait toujours où l'on est. */
        board_splash_show();
    }
    /* Ecran "pret" juste apres le splash (demande 2026-09-16) : distingue
     * la transition (splash, transitoire) de l'etat stable — sans lui, un
     * blocage apres le splash resterait indiscernable d'un demarrage
     * reussi (l'image de transition resterait affichee dans les deux cas).
     * Provisoire, remplace par l'ecran UNLOCK reel en 8.4. */
    board_ready_show();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Console REPL (debug/dev — l'UI e-ink arrive en 8.4).
     * Backend selon CONFIG_ESP_CONSOLE_* : le X4 Pro utilise le
     * USB-Serial/JTAG natif (le dock ne câble pas UART0). */
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "msf> ";
    esp_console_register_help_command();
    const esp_console_cmd_t about_cmd = {
        .command = "about",
        .help = "Identité du firmware",
        .func = &cmd_about,
    };
    const esp_console_cmd_t selftest_cmd = {
        .command = "totpselftest",
        .help = "Self-tests RFC 6238 du moteur TOTP",
        .func = &cmd_totpselftest,
    };
    const esp_console_cmd_t sleep_cmd = {
        .command = "sleep",
        .help = "Ecran de veille puis deep sleep (reveil = Power GPIO3)",
        .func = &cmd_sleep,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&about_cmd));
    ESP_ERROR_CHECK(esp_console_cmd_register(&selftest_cmd));
    ESP_ERROR_CHECK(esp_console_cmd_register(&sleep_cmd));
    /* IDF 5.5 : esp_console_new_repl_*() spawn le thread REPL lui-meme
     * (plus de esp_console_repl_start). */
    esp_console_repl_t *repl = NULL;
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t usb_cfg =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usb_cfg, &repl_cfg,
                                                         &repl));
#elif CONFIG_ESP_CONSOLE_UART
    esp_console_dev_uart_config_t uart_cfg =
        ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));
#else
#error "Console REPL : ni USB_SERIAL_JTAG ni UART configures"
#endif
    (void)repl;

    /* Bouton Power : appui < 10s = veille / appui >= 10s = factory (ADR-009).
     * BUGFIX 2026-09-16 (stack overflow confirme sur hardware, log) :
     * power_mgr_switch_to_factory() declare un buffer local
     * uint8_t buf[FLASH_SECTOR_SIZE] (4096 octets) — a lui seul deja egal
     * a l'ancienne taille de pile totale de cette tache (4096), sans
     * compter entry1/entry2, l'usage de pile propre aux appels
     * esp_flash_read/erase_region/write, ni les autres locales de cette
     * boucle. Plantage systematique (vApplicationStackOverflowHook) des
     * que power_mgr_switch_to_factory() est atteinte depuis l'app eveillee
     * (jamais depuis le hook bootloader, qui tourne dans un contexte de
     * pile different). Marge large car PSRAM abondante sur cette board. */
    xTaskCreate(power_button_task, "pwr_btn", 12288, NULL, 5, NULL);

    esp3d_log_d("Squelette pret. Commandes : help, about, totpselftest, sleep");
}
