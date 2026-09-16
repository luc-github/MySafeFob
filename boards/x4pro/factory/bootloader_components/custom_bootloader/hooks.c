/**
 * @file hooks.c
 * @brief MySafeFob — custom bootloader hooks (ADR-007).
 *
 * Portage du hook éprouvé du projet PiBot (PiBot CNC Pendant, Luc LEBOSSE,
 * LGPL-2.1+) vers ESP32-S3 / IDF 5.5.x, X4 Pro.
 *
 * Recovery trigger (ADR-009 amende 2026-09-16) : bouton POWER (GPIO3)
 * maintenu au reveil pendant >= 10 s :
 *   1. Backup otadata vers le secteur réservé 0xB000 (+ magic 0xAA55AA55)
 *   2. Erase otadata -> le bootloader standard boote la partition "factory"
 *   3. Software reset -> boot factory
 *
 * La factory app restaure otadata depuis le backup au démarrage (puis
 * efface le backup) : un power-off depuis la factory renvoie vers la
 * bonne partition OTA.
 *
 * Historique : le trigger initial utilisait un combo Power+Right (GPIO7).
 * Abandonne — teste sur hardware, Power+Right ensemble n'a JAMAIS reveille
 * le device, quelle que soit la duree de maintien (probleme en amont du
 * hook, pas un souci de timing logiciel). Remplace par Power seul, mesure
 * de duree : ce hook s'execute au reveil (le bouton qui reveille le S3 est
 * deja Power), donc il suffit de continuer a mesurer combien de temps
 * GPIO3 reste bas apres le reveil.
 *
 * Points de portage validés PiBot->MSF (ADR-007 §4) :
 *   - Pas de buzzer sur le X4 Pro : acquittement = logs esp_rom_printf.
 *   - Trigger = GPIO3 (Power) : GPIO0 est strapping (Left).
 *   - Signatures esp_rom_spiflash_* sur S3 / IDF 5.5.5 : A VERIFIER a la
 *     1re compilation (le code d'origine tourne en 5.4.3 sur ESP32 classic).
 */

#include <string.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_rom_sys.h"
#include "esp_rom_gpio.h"
#include "esp_rom_spiflash.h"
#include "sdkconfig.h"
#include "hal/gpio_ll.h"
#include "hal/rtc_io_ll.h"
#include "hal/wdt_hal.h"
#include "soc/reset_reasons.h"

static const char *TAG = "msf-hook";

/* CONFIG_BOOTLOADER_LOG_LEVEL_NONE strippe ESP_LOG* a ce stade de build :
 * impressions directes via esp_rom_printf, gatees par FACTORY_LOG_LEVEL
 * (voir CMakeLists.txt), independant du sdkconfig. Erreurs/warnings toujours. */
#ifndef FACTORY_LOG_LEVEL
#define FACTORY_LOG_LEVEL 0
#endif
#define HOOK_LOGE(tag, fmt, ...) esp_rom_printf("E (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define HOOK_LOGW(tag, fmt, ...) esp_rom_printf("W (%s) " fmt "\n", tag, ##__VA_ARGS__)
#if FACTORY_LOG_LEVEL
#define HOOK_LOGI(tag, fmt, ...) esp_rom_printf("I (%s) " fmt "\n", tag, ##__VA_ARGS__)
#else
#define HOOK_LOGI(tag, fmt, ...) do {} while (0)
#endif

#define FLASH_SECTOR_SIZE  0x1000

/* -----------------------------------------------------------------------
 * Hardware config (X4 Pro — docs/hardware-specs.md)
 * ----------------------------------------------------------------------- */

#define RECOVERY_BUTTON_PIN   GPIO_NUM_3   /* Bouton Power, actif-LOW, pull-up */

/*
 * WARNING — CONTRAINTE CRITIQUE : SECTEUR DE BACKUP OTADATA
 * ==========================================================
 * Le secteur de backup doit satisfaire TOUTES ces conditions :
 *
 *   1. APRES la fin du bootloader : bootloader @0x1000, binaire S3 ~20-24 Ko,
 *      prochaine frontiere 4 KB = 0x7000. Backup doit etre >= 0x7000
 *      (0xB000 le fait avec marge ; taille bootloader S3 a mesurer).
 *   2. AVANT la table des partitions : CONFIG_PARTITION_TABLE_OFFSET=0xC000.
 *      Backup + 0x1000 <= 0xC000  =>  backup <= 0xB000.
 *   3. EN DEHORS de toute partition : le check dangerous-write de l'IDF aborte
 *      si l'adresse est dans une partition connue. 0xB000 est hors partitions
 *      (1re partition = NVS @0xD000). OK.
 *   4. Aligne 4 KB (frontiere de secteur).
 *
 * DANGER — CONFIG_SPI_FLASH_DANGEROUS_WRITE :
 *   La factory efface le backup via esp_flash_erase_region(NULL, 0xB000, ...).
 *   Avec le defaut ABORTS, toute adresse avant la 1re partition (0xD000) est
 *   "dangerous" -> abort(). Le sdkconfig.defaults de la factory DOIT avoir
 *   CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED=y. (deja en place)
 *
 * Layout MSF (16 MB, ADR-007) :
 *   0x1000       Bootloader
 *   ~0x7000      Fin bootloader S3 (a mesurer — marge jusqu'a 0xB000)
 *   0xB000       <- OTADATA BACKUP (ce secteur)
 *   0xC000       Table des partitions
 *   0xD000       NVS (1re partition)
 *
 * Contenu du secteur de backup :
 *   [0x000] entree otadata 1 (32 octets utilises, reste 0xFF)
 *   [0x020] entree otadata 2 (32 octets utilises, reste 0xFF)
 *   [0x040] magic : 0xAA55AA55 (backup valide)
 *
 * CONTRAT PARTAGE : ces valeurs DOIVENT etre identiques dans hooks.c et
 * dans main.c de la factory app.
 */
#define OTADATA_OFFSET          0x10000
#define OTADATA_SECTOR_1        (OTADATA_OFFSET / FLASH_SECTOR_SIZE)
#define OTADATA_SECTOR_2        ((OTADATA_OFFSET + FLASH_SECTOR_SIZE) / FLASH_SECTOR_SIZE)

#define OTADATA_BACKUP_OFFSET   0xB000
#define OTADATA_BACKUP_SECTOR   (OTADATA_BACKUP_OFFSET / FLASH_SECTOR_SIZE)
#define OTADATA_ENTRY_SIZE      32
#define BACKUP_MAGIC_OFFSET     0x40
#define BACKUP_MAGIC            0xAA55AA55

/* BUGFIX UX 2026-09-16 : l'ancienne logique demandait de RELACHER le bouton
 * dans une fenetre courte suivant sa detection, sinon annulation ("maintenu
 * trop longtemps"). Aucun retour (visuel/audio) n'existe pendant le boot
 * pour savoir quand cette fenetre commence -> impossible de timer un
 * relachement precis en usage reel. Inverse : Power tenu en continu
 * JUSQU'AU seuil declenche la bascule automatiquement (pas besoin de
 * relacher a un instant precis) ; relache avant le seuil = annule.
 *
 * Seuil final (2026-09-16, ADR-009 amende) : 10 s, aligne sur le seuil
 * logiciel cote app eveillee (power_mgr_switch_to_factory) — meme geste
 * ("tenir Power 10s") quel que soit l'etat de depart (eveille ou endormi). */
#define CONFIRM_HOLD_US      (10 * 1000000)

/* BUGFIX 2026-09-16 : is_button_pressed() bloque deja ~25 ms en interne
 * (5 lectures x 5 ms). La boucle de confirmation ajoutait PAR-DESSUS un
 * delai artificiel de 10 ms (POLL_INTERVAL_US) tout en ne comptant QUE ces
 * 10 ms dans held_us — le temps reel ecoule par iteration (~35 ms) etait
 * donc sous-estime d'un facteur ~3.5x. Consequence concrete : pour que
 * held_us atteigne CONFIRM_HOLD_US (10 s), il fallait en realite maintenir
 * le bouton ~35 s. Fix : compter le temps reellement ecoule (la duree
 * interne de is_button_pressed()), sans delai supplementaire. */
#define BUTTON_SAMPLE_US    (5 * 5000)  /* duree reelle de is_button_pressed() */

/* -----------------------------------------------------------------------
 * Bouton (actif-LOW, anti-rebond logiciel)
 * ----------------------------------------------------------------------- */

static bool is_button_pressed(gpio_num_t pin)
{
    int pressed_count = 0;
    for (int i = 0; i < 5; i++) {
        if (gpio_ll_get_level(&GPIO, pin) == 0) {
            pressed_count++;
        }
        esp_rom_delay_us(5000);
    }
    return pressed_count >= 3;
}

/* BUGFIX 2026-09-16 (root cause du "plus aucun reveil ni bascule factory,
 * meme apres 12 s") : CONFIG_BOOTLOADER_WDT_ENABLE arme le RTC WDT (RWDT)
 * avec un timeout unique CONFIG_BOOTLOADER_WDT_TIME_MS = 9000 ms AVANT
 * l'appel a bootloader_after_init() (bootloader_init.c, action
 * WDT_STAGE_ACTION_RESET_RTC). Notre boucle de confirmation bloque
 * DELIBEREMENT jusqu'a CONFIRM_HOLD_US = 10 s SANS jamais nourrir ce chien
 * de garde -> il se declenche a 9 s, RESET le chip AVANT d'atteindre le
 * seuil, qui reboote... dans le meme hook, qui rearme le meme WDT pour un
 * nouveau cycle de 9 s max, etc. Tant que l'utilisateur maintient le
 * bouton, le seuil de 10 s n'est donc JAMAIS atteignable : boucle de reset
 * silencieuse (l'ecran n'est jamais rafraichi a ce stade, aucun code appli
 * ne tourne encore) qui se voit de l'exterieur comme un blocage total.
 * Fix : nourrir explicitement le RWDT a chaque iteration du poll, exactement
 * comme le fait bootloader_support/src/flash_encryption/flash_encrypt.c
 * pour ses propres operations longues au meme stade. */
static void feed_bootloader_wdt(void)
{
    wdt_hal_context_t rwdt_ctx = RWDT_HAL_CONTEXT_DEFAULT();
    wdt_hal_write_protect_disable(&rwdt_ctx);
    wdt_hal_feed(&rwdt_ctx);
    wdt_hal_write_protect_enable(&rwdt_ctx);
}

/* -----------------------------------------------------------------------
 * Backup + erase otadata
 * ----------------------------------------------------------------------- */

static void backup_and_erase_otadata(void)
{
    uint8_t buf[FLASH_SECTOR_SIZE];
    uint8_t entry1[OTADATA_ENTRY_SIZE];
    uint8_t entry2[OTADATA_ENTRY_SIZE];
    uint32_t magic = BACKUP_MAGIC;

    if (esp_rom_spiflash_read(OTADATA_OFFSET, (uint32_t *)entry1,
                              OTADATA_ENTRY_SIZE) != ESP_ROM_SPIFLASH_RESULT_OK) {
        HOOK_LOGE(TAG, "lecture otadata secteur 1 impossible");
        return;
    }
    if (esp_rom_spiflash_read(OTADATA_OFFSET + FLASH_SECTOR_SIZE,
                              (uint32_t *)entry2,
                              OTADATA_ENTRY_SIZE) != ESP_ROM_SPIFLASH_RESULT_OK) {
        HOOK_LOGE(TAG, "lecture otadata secteur 2 impossible");
        return;
    }

    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, entry1, OTADATA_ENTRY_SIZE);
    memcpy(buf + OTADATA_ENTRY_SIZE, entry2, OTADATA_ENTRY_SIZE);
    memcpy(buf + BACKUP_MAGIC_OFFSET, &magic, sizeof(magic));

    if (esp_rom_spiflash_erase_sector(OTADATA_BACKUP_SECTOR) != ESP_ROM_SPIFLASH_RESULT_OK) {
        HOOK_LOGE(TAG, "erase secteur backup impossible");
        return;
    }
    if (esp_rom_spiflash_write(OTADATA_BACKUP_OFFSET, (uint32_t *)buf,
                               FLASH_SECTOR_SIZE) != ESP_ROM_SPIFLASH_RESULT_OK) {
        HOOK_LOGE(TAG, "ecriture backup impossible");
        return;
    }

    HOOK_LOGI(TAG, "otadata sauvegarde @0x%x", OTADATA_BACKUP_OFFSET);

    if (esp_rom_spiflash_erase_sector(OTADATA_SECTOR_1) != ESP_ROM_SPIFLASH_RESULT_OK ||
        esp_rom_spiflash_erase_sector(OTADATA_SECTOR_2) != ESP_ROM_SPIFLASH_RESULT_OK) {
        HOOK_LOGE(TAG, "erase otadata impossible");
        return;
    }

    HOOK_LOGI(TAG, "otadata efface — reboot vers factory");
}

/* -----------------------------------------------------------------------
 * Bootloader hooks
 * ----------------------------------------------------------------------- */

void bootloader_hooks_include(void) {}

void bootloader_before_init(void)
{
    /* Reserve pour usage futur */
}

void bootloader_after_init(void)
{
    /* BUGFIX 2026-09-16 (root cause identifiee) : quand RECOVERY_BUTTON_PIN
     * etait Right (GPIO7, jamais utilise comme source de wake), le lire en
     * digital brut ici etait sans risque. Depuis le passage a Power (GPIO3,
     * ADR-009 amende) — qui est justement le pin EXT1 ayant reveille le
     * chip pour TOUT reveil deep sleep — esp_sleep_enable_ext1_wakeup()
     * route ce pad par le domaine RTC_IO pour la duree du sommeil, et cette
     * configuration persiste au travers du reveil : lire GPIO3 via la
     * matrice GPIO digitale (gpio_ll_get_level) sans d'abord rendre la main
     * au digital cote RTC_IO renvoyait une valeur figee, independante de
     * l'etat reel du bouton (plus aucun reveil ni bascule factory possible,
     * observe sur hardware). Fix : rtcio_ll_function_select(...,
     * RTCIO_LL_FUNC_DIGITAL) — l'equivalent bas niveau (registre) de
     * rtc_gpio_deinit(), utilisable ici car rtc_io_ll.h est un header HAL
     * "inline registres" sans dependance driver/FreeRTOS — rend la main au
     * digital AVANT toute lecture. Sur S3, rtcio_num == gpio_num (offset 0).
     * Sur un reset "froid" (jamais route RTC), cet appel est un no-op sans
     * effet de bord. */
    rtcio_ll_function_select(RECOVERY_BUTTON_PIN, RTCIO_LL_FUNC_DIGITAL);

    /* Demande utilisateur 2026-09-16 : la bascule factory doit rester
     * pilotable au niveau bootloader, independamment de l'etat de l'app
     * (app plantee/gelee = toujours reparable). Seule exception : un reset
     * logiciel (RESET_REASON_CORE_SW, esp_restart) declenche par un chemin
     * qui a DEJA positionne otadata lui-meme (power_mgr_switch_to_factory()
     * cote app, action_cancel() cote factory) — relire le bouton ici
     * ajouterait un 2e delai de confirmation (jusqu'a CONFIRM_HOLD_US)
     * inutile et deroutant, sans rien changer a la destination du boot. */
    soc_reset_reason_t reset_reason = esp_rom_get_reset_reason(0);
    if (reset_reason == RESET_REASON_CORE_SW) {
        return;
    }

    /* Pas de buzzer sur le X4 Pro : pas d'init de sortie, juste le bouton. */
    esp_rom_gpio_pad_select_gpio(RECOVERY_BUTTON_PIN);
    gpio_ll_input_enable(&GPIO, RECOVERY_BUTTON_PIN);
    gpio_ll_pullup_en(&GPIO, RECOVERY_BUTTON_PIN);

    feed_bootloader_wdt();
    esp_rom_delay_us(100000);  /* 100ms : stabilisation du pull-up */

    if (!is_button_pressed(RECOVERY_BUTTON_PIN)) {
        return;  /* Boot normal, rien modifie */
    }

    HOOK_LOGI(TAG, "POWER (GPIO3) presse — maintenir %u ms pour confirmer...",
              (unsigned)(CONFIRM_HOLD_US / 1000));

    uint32_t held_us = 0;
    uint32_t next_log_us = 1000000;   /* diagnostic 2026-09-16 : progression toutes les ~1s */
    while (is_button_pressed(RECOVERY_BUTTON_PIN) && held_us < CONFIRM_HOLD_US) {
        feed_bootloader_wdt();   /* voir feed_bootloader_wdt() : evite le reset RWDT a 9s */
        held_us += BUTTON_SAMPLE_US;   /* voir BUTTON_SAMPLE_US : compte le temps reel */
        if (held_us >= next_log_us) {
            HOOK_LOGI(TAG, "... maintenu %u ms", (unsigned)(held_us / 1000));
            next_log_us += 1000000;
        }
    }

    if (held_us < CONFIRM_HOLD_US) {
        HOOK_LOGW(TAG, "relache trop tot (%u ms) — recovery annule",
                  (unsigned)(held_us / 1000));
        return;
    }

    HOOK_LOGI(TAG, "seuil atteint — bascule vers factory");
    backup_and_erase_otadata();

    HOOK_LOGI(TAG, "reset logiciel -> bootloader -> partition factory");

    esp_rom_delay_us(200000);
    esp_rom_software_reset_system();
    while (1) { }  /* Jamais atteint */
}
