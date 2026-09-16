/**
 * @file main.c
 * @brief MySafeFob — application entry point (Phase 8 skeleton).
 *
 * For now: banner + TOTP self-tests + minimal REPL console.
 * The real app boot (UNLOCK -> UI) arrives with task 8.2/8.4.
 *
 * The REPL console (F-20, docs/FEATURES.md) is NOT temporary: it
 * stays in place permanently, even once the touch UI ships — useful
 * for running commands and checking status without the screen. Only
 * its content (available commands) will grow across the phases.
 *
 * Application logging: esp3d_log (hooks -> on-screen error history
 * in 8.4). Drivers/board code stays on ESP_LOG* (IDF ecosystem).
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

/* E-ink splash at boot — implemented by the board component
 * (boards/<board>/app/splash.c, declared via EXTRA_COMPONENT_DIRS in
 * board_config.cmake). No weak symbol: with a static archive, the
 * weak symbol satisfies the reference and the strong implementation
 * is never pulled from the link (observed at the 2026-09-14 build). A
 * board without e-ink must provide a stub, the link error is explicit. */
void board_splash_show(void);
void board_ready_show(void);          /* "ready" screen, after the splash */
void board_sleep_screen_show(void);   /* sleep screen (deep sleep) */

/* BUGFIX 2026-09-16: cmd_sleep() (REPL, its own esp_console task) and
 * power_button_task() (physical Power press) each independently lead
 * to board_sleep_screen_show()+power_mgr_shutdown() (and
 * power_button_task also to power_mgr_switch_to_factory()) with no
 * mutual exclusion at all. Two distinct FreeRTOS tasks triggering one
 * of these sequences at the same time would touch the e-ink
 * concurrently (eink.c uses `static` buffers in its line-by-line
 * transfer functions, no mutex): plausible display corruption
 * (interleaved SPI), undefined state.
 * Only one caller may "claim" a terminal transition (sleep or
 * factory) — the second one, if it arrives, is ignored rather than
 * running in parallel. */
static atomic_bool s_terminal_action_claimed = false;

static bool claim_terminal_action(void)
{
    bool expected = false;
    return atomic_compare_exchange_strong(&s_terminal_action_claimed, &expected, true);
}

static int cmd_about(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("MySafeFob (MSF) — Phase 8 skeleton\n");
    printf("  board : %s\n", MSF_BOARD_NAME);
    printf("  IDF   : %s\n", esp_get_idf_version());
    printf("  wake  : %s\n", s_wake_from_sleep ? "deep-sleep (Power)" : "cold boot");
    return 0;
}

static int cmd_sleep(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (!claim_terminal_action()) {
        printf("Already in progress (Power button pressed?) — ignored.\n");
        return 0;
    }
    printf("Deep sleep: sleep screen, then sleep. Wake = Power (GPIO3).\n");
    printf("(Power held >= 10s = switch to factory — see ADR-009)\n");
    vTaskDelay(pdMS_TO_TICKS(500));   /* let the message get out over USB */
    board_sleep_screen_show();
    power_mgr_shutdown();
    return 0;   /* never reached */
}

static int cmd_totpselftest(int argc, char **argv)
{
    (void)argc; (void)argv;
    esp_err_t err = totp_engine_run_self_tests();
    if (err == ESP_OK) {
        printf("TOTP self-tests (RFC 6238): OK\n");
    } else {
        printf("TOTP self-tests: FAILED (%s)\n", esp_err_to_name(err));
    }
    return 0;
}

/* -----------------------------------------------------------------------
 * Power button (ADR-009 amended 2026-09-16) — skeleton version, replaced
 * by the LVGL indev in 8.4. GPIO3 (Power, active-LOW), single duration
 * measurement (no more Right combo — dropped, see power_mgr.h):
 *   - released < MSF_POWER_LONG_MS   : nothing (short press ignored here)
 *   - held >= MSF_POWER_LONG_MS and < MSF_POWER_FACTORY_MS : sleep
 *   - held >= MSF_POWER_FACTORY_MS : switch to factory (esp_ota, software)
 * ----------------------------------------------------------------------- */
#define MSF_BTN_POWER_PIN      GPIO_NUM_3
#define MSF_POWER_LONG_MS      1500
#define MSF_POWER_FACTORY_MS   10000

static void power_button_task(void *arg)
{
    (void)arg;
    /* GPIO3 already configured by power_mgr_init */

    bool held = false;
    int64_t t0_us = 0;
    bool factory_triggered = false;
    int64_t next_log_ms = 0;   /* 2026-09-16 diagnostic: progress every ~1s */
    /* BUGFIX 2026-09-16 (symmetrical to the power_mgr_shutdown fix): on
     * wake, Power is still physically held down (that's the gesture that
     * woke the device) — without this, this task would arm its
     * long-press timer immediately on that residual press, and put the
     * device back to sleep 1.5s later without ever letting the app be
     * seen. We require an observed release at least once before arming
     * a first press.
     *
     * NOTE 2026-09-16: the case "Power held >= 10s continuously since
     * sleep" is NOT handled here — it is resolved by the bootloader hook
     * (hooks.c) BEFORE this task (and the app in general) starts: the
     * switch to factory therefore stays available even if the app
     * crashes or hangs, which a measurement done only on the app side
     * could not guarantee. This task only handles presses made AFTER
     * the app is running. */
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
            esp3d_log_d("Power pressed — detecting...");
        }
        if (held && p) {
            int64_t held_ms = (esp_timer_get_time() - t0_us) / 1000;
            if (held_ms >= next_log_ms) {
                esp3d_log_d("Power: held %lld ms", (long long)held_ms);
                next_log_ms += 1000;
            }
            if (!factory_triggered && held_ms >= MSF_POWER_FACTORY_MS) {
                factory_triggered = true;
                if (claim_terminal_action()) {
                    esp3d_log_d("Power >= 10s: switching to factory");
                    power_mgr_switch_to_factory();   /* never returns if OK */
                    esp3d_log_d("Power: switch to factory FAILED, staying awake");
                    atomic_store(&s_terminal_action_claimed, false);
                } else {
                    esp3d_log_d("Power >= 10s: transition already in progress (REPL sleep?), ignored");
                }
            }
        }
        if (!p && held) {
            int64_t held_ms = (esp_timer_get_time() - t0_us) / 1000;
            if (!factory_triggered && held_ms >= MSF_POWER_LONG_MS) {
                if (claim_terminal_action()) {
                    esp3d_log_d("Power long press: going to sleep");
                    board_sleep_screen_show();
                    power_mgr_shutdown();               /* never reached */
                } else {
                    esp3d_log_d("Power long press: transition already in progress (REPL sleep?), ignored");
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
    esp3d_log_d("  MySafeFob (MSF) — Phase 8 skeleton");
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
        /* Interim feedback (2026-09-14 session: wake worked but was
         * invisible -> "the button does nothing"). Replaced by the
         * UNLOCK screen in 8.4; keep in dev to validate the wake flow. */
        esp3d_log_d("Wake from deep sleep — showing splash (UNLOCK screen in 8.4)");
        board_splash_show();
    } else {
        /* Static e-ink page (no-op if the board doesn't have one): ~3-4s
         * blocking, before the REPL — so we always know where we are. */
        board_splash_show();
    }
    /* "Ready" screen right after the splash (2026-09-16 request): tells
     * apart the transition (splash, transient) from the stable state —
     * without it, a hang after the splash would be indistinguishable from
     * a successful boot (the transition image would stay displayed in
     * both cases). Provisional, replaced by the real UNLOCK screen in 8.4. */
    board_ready_show();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* REPL console (debug/dev — the e-ink UI arrives in 8.4).
     * Backend depends on CONFIG_ESP_CONSOLE_*: the X4 Pro uses the
     * native USB-Serial/JTAG (the dock doesn't wire UART0). */
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "msf> ";
    esp_console_register_help_command();
    const esp_console_cmd_t about_cmd = {
        .command = "about",
        .help = "Firmware identity",
        .func = &cmd_about,
    };
    const esp_console_cmd_t selftest_cmd = {
        .command = "totpselftest",
        .help = "RFC 6238 self-tests for the TOTP engine",
        .func = &cmd_totpselftest,
    };
    const esp_console_cmd_t sleep_cmd = {
        .command = "sleep",
        .help = "Sleep screen then deep sleep (wake = Power GPIO3)",
        .func = &cmd_sleep,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&about_cmd));
    ESP_ERROR_CHECK(esp_console_cmd_register(&selftest_cmd));
    ESP_ERROR_CHECK(esp_console_cmd_register(&sleep_cmd));
    /* BUGFIX 2026-09-16: previous comment was wrong — esp_console_new_repl_*()
     * does create the REPL task, but it stays parked in the
     * CONSOLE_REPL_STATE_INIT state (read loop never executed) until
     * esp_console_start_repl() explicitly switches the state to
     * CONSOLE_REPL_STATE_START (esp_console_common.c, IDF 5.5.5). Without
     * this call (missing here, `(void)repl;` was throwing it away), the
     * console does show the banner/logs (esp3d_log/printf don't depend on
     * this task) but never processes any typed command again — confirmed
     * by comparison with `references/test_apps/x4pro-probe/main/main.c`
     * which does call esp_console_start_repl(repl) and whose console
     * worked. */
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
#error "REPL console: neither USB_SERIAL_JTAG nor UART configured"
#endif
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    /* Power button: press < 10s = sleep / press >= 10s = factory (ADR-009).
     * BUGFIX 2026-09-16 (stack overflow confirmed on hardware, logged):
     * power_mgr_switch_to_factory() declares a local buffer
     * uint8_t buf[FLASH_SECTOR_SIZE] (4096 bytes) — on its own already
     * equal to this task's former total stack size (4096), not counting
     * entry1/entry2, the stack usage of the esp_flash_read/erase_region/write
     * calls, or this loop's other locals. Systematic crash
     * (vApplicationStackOverflowHook) as soon as power_mgr_switch_to_factory()
     * is reached from the awake app (never from the bootloader hook, which
     * runs in a different stack context). Wide margin since this board has
     * abundant PSRAM. */
    xTaskCreate(power_button_task, "pwr_btn", 12288, NULL, 5, NULL);

    esp3d_log_d("Skeleton ready. Commands: help, about, totpselftest, sleep");
}
