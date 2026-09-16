/**
 * @file main.c
 * @brief MySafeFob Recovery (factory) — portage PiBot Recovery vers X4 Pro.
 *   Copyright (c) 2025-2026 Luc LEBOSSE. All rights reserved.
 *   Licensed under GNU Lesser General Public License v2.1 or later.
 *
 * Menu recovery (e-ink, sans LVGL — gfx 1 bpp maison) :
 *   Left (GPIO0) = haut   Right (GPIO7) = bas
 *   Pad Home (GT911, zone tactile validee) = valider (OK)
 *   Power (GPIO3) = Cancel : reboot sur la partition par defaut
 *
 * Flot (identique au PiBot, ADR-007) :
 *   Le bootloader hook a sauvegarde otadata @0xB000 puis l'a efface avant de
 *   sauter ici. Au demarrage on restaure otadata : un power-off depuis la
 *   factory renvoie vers la bonne partition OTA.
 *
 * Actions :
 *   Boot app0  : esp_ota_set_boot_partition + reboot
 *   SD -> app0 : flash /sdcard/msf-fw.bin via OTA vers le seul slot app
 *                (ADR-011 : app1 retire, pas d'OTA reseau -> pas besoin
 *                du rollback esp_ota A/B, la factory est le filet de
 *                securite si l'update boote mal)
 *
 * PAS de "SD -> factory" (retire 2026-09-15, decision utilisateur) : la
 * factory ne s'auto-met jamais a jour depuis la SD — elle s'ecrirait sur la
 * partition qu'elle execute elle-meme (XIP), sans filet de recovery en cas
 * d'interruption en plein vol. Traitee comme le bootloader : mise a jour
 * uniquement par USB/serial (flash_mgr.py --variant x4pro_factory).
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_flash.h"

#include "hw_config.h"
#include "eink.h"
#include "gfx.h"
#include "buttons.h"
#include "touch.h"
#include "sdcard.h"
#include "factory_log.h"
#include "version.h"
#include "splash.h"
#include "battery.h"
#include "battery_icon.h"
#include "rtc.h"
#include "esp_timer.h"

static const char *TAG = "RECOVERY";

/* Otadata backup — DOIT matcher hooks.c EXACTEMENT (contrat partage).
 * Contraintes du secteur 0xB000 : apres bootloader (~0x7000 S3), avant la
 * table des partitions (0xC000), hors de toute partition (NVS @0xD000),
 * aligne 4 Ko. La factory exige CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED=y
 * (deja dans factory/sdkconfig.defaults). */
#define OTADATA_OFFSET          0x10000
#define OTADATA_SECTOR_SIZE     0x1000
#define OTADATA_ENTRY_SIZE      32
#define OTADATA_BACKUP_OFFSET   0xB000
#define BACKUP_MAGIC_OFFSET     0x40
#define BACKUP_MAGIC            0xAA55AA55

/* -----------------------------------------------------------------------
 * Restore otadata (logique PiBot, identique)
 * ----------------------------------------------------------------------- */

static bool restore_otadata_from_backup(void)
{
    uint32_t magic = 0;
    esp_err_t err = esp_flash_read(NULL, &magic,
                                   OTADATA_BACKUP_OFFSET + BACKUP_MAGIC_OFFSET,
                                   sizeof(magic));
    if (err != ESP_OK || magic != BACKUP_MAGIC) {
        FACTORY_LOGD(TAG, "pas de backup otadata (magic=0x%lx)",
                     (unsigned long)magic);
        return false;
    }

    FACTORY_LOGD(TAG, "backup otadata trouve, restauration...");
    uint8_t entry1[OTADATA_ENTRY_SIZE];
    uint8_t entry2[OTADATA_ENTRY_SIZE];
    esp_flash_read(NULL, entry1, OTADATA_BACKUP_OFFSET, OTADATA_ENTRY_SIZE);
    esp_flash_read(NULL, entry2, OTADATA_BACKUP_OFFSET + OTADATA_ENTRY_SIZE,
                   OTADATA_ENTRY_SIZE);

    bool entry1_empty = true, entry2_empty = true;
    for (int i = 0; i < OTADATA_ENTRY_SIZE; i++) {
        if (entry1[i] != 0xFF) entry1_empty = false;
        if (entry2[i] != 0xFF) entry2_empty = false;
    }
    if (entry1_empty && entry2_empty) {
        ESP_LOGW(TAG, "backup otadata vide, restore ignore");
        goto clear_backup;
    }

    esp_flash_erase_region(NULL, OTADATA_OFFSET, OTADATA_SECTOR_SIZE);
    esp_flash_erase_region(NULL, OTADATA_OFFSET + OTADATA_SECTOR_SIZE,
                           OTADATA_SECTOR_SIZE);
    if (!entry1_empty) {
        esp_flash_write(NULL, entry1, OTADATA_OFFSET, OTADATA_ENTRY_SIZE);
    }
    if (!entry2_empty) {
        esp_flash_write(NULL, entry2, OTADATA_OFFSET + OTADATA_SECTOR_SIZE,
                        OTADATA_ENTRY_SIZE);
    }
    FACTORY_LOGD(TAG, "otadata restaure");

clear_backup:
    esp_flash_erase_region(NULL, OTADATA_BACKUP_OFFSET, OTADATA_SECTOR_SIZE);
    FACTORY_LOGD(TAG, "secteur backup efface");
    return true;
}

/* -----------------------------------------------------------------------
 * Helpers partitions / boot
 * ----------------------------------------------------------------------- */

/* Label actif cale UNE FOIS au boot : esp_ota_get_boot_partition() a chaque
 * redraw (a) spammait le log esp_ota_ops et (b) relisait la flash a chaque
 * navigation. Le label ne peut pas changer en session : toute action qui le
 * modifierait (boot/flash OK) rebooter avant de revenir ici. */
static char s_active_label[17] = "";

static void cache_active_ota_label(void)
{
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    if (boot == NULL) {
        snprintf(s_active_label, sizeof(s_active_label), "unknown");
    } else if (boot->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        snprintf(s_active_label, sizeof(s_active_label), "factory");
    } else {
        /* Copy bornee sans warning -Wformat-truncation (labels <= 16). */
        size_t n = strnlen(boot->label, sizeof(s_active_label) - 1);
        memcpy(s_active_label, boot->label, n);
        s_active_label[n] = '\0';
    }
}

static void boot_partition(const char *label)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label);
    if (!part) {
        ESP_LOGE(TAG, "partition '%s' introuvable", label);
        return;
    }
    esp_err_t err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set boot partition: %s", esp_err_to_name(err));
        return;
    }
    /* Relecture diagnostique (session 2026-09-14 : otadata devenu "invalid"
     * en cours de session sans cause identifiee — on verifie ce que la
     * flash contient reellement apres set_boot, avant le reboot). */
    const esp_partition_t *check = esp_ota_get_boot_partition();
    ESP_LOGI(TAG, "boot -> '%s' (otadata relu: %s)", label,
             check ? check->label : "INVALID");
    eink_power_off();
    esp_restart();
}

/* -----------------------------------------------------------------------
 * Etat menu / SD
 * ----------------------------------------------------------------------- */

static bool has_fwfile = false;

static void probe_sd_files(void)
{
    has_fwfile = false;
    if (sdcard_mount() != ESP_OK) return;
    FILE *f = fopen(FW_FILENAME, "rb");
    if (f) { has_fwfile = true; fclose(f); }
    sdcard_unmount();
}

/* -----------------------------------------------------------------------
 * Menu (gfx 1 bpp — chaque interaction = redraw complet + full refresh)
 * ----------------------------------------------------------------------- */

#define MENU_MAX_ITEMS  6
#define MENU_START_Y    232
#define MENU_ITEM_H     60          /* police x2 : glyphe 32 px + marge */
#define MENU_PAD_X      30
#define STATUS_Y        (SCREEN_HEIGHT - 56)

typedef enum {
    MENU_ACTION_BOOT_APP0,
    MENU_ACTION_SD_APP0,
} menu_action_t;

typedef struct {
    const char *label;
    menu_action_t action;
} menu_item_t;

static menu_item_t menu_items[MENU_MAX_ITEMS];
static int menu_count = 0;
static int menu_selected = 0;
static char last_status[60] = "";
static bool last_status_ok = false;

static void draw_menu_impl(bool fast)
{
    gfx_clear(COLOR_WHITE);

    gfx_rect(2, 2, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, COLOR_BLACK);
    gfx_rect(3, 3, SCREEN_WIDTH - 6, SCREEN_HEIGHT - 6, COLOR_BLACK);

    char title[48];
    snprintf(title, sizeof(title), "MySafeFob Recovery v" VERSION_FACTORY);
    gfx_draw_string(GFX_CENTER_X(title), 20, title, COLOR_BLACK, COLOR_WHITE);
    gfx_hline(20, 62, SCREEN_WIDTH - 40, COLOR_BLACK);

    /* Deux lignes (demande 2026-09-14). Ligne 108 : batterie/charge (CW2017)
     * remplace "Default: app0" — devenu tautologique depuis ADR-011 (un
     * seul slot app, plus de comparaison a faire). Ligne 144 : date/heure
     * (BM8563) remplace le statut touch T:OK/KO — retire 2026-09-15
     * (redondant : un touch mort se voit immediatement a l'usage). */
    char info[32];
    snprintf(info, sizeof(info), "Active: %s", s_active_label);
    gfx_draw_string(GFX_CENTER_X(info), 72, info, COLOR_BLACK, COLOR_WHITE);

    /* Icone batterie (FreeInkUI batteryIndicator) + pourcentage en texte
     * gfx_draw_string (meme police que le reste — pas celle de FreeInkUI,
     * pour rester visuellement coherent). */
    char batt[8];
    uint8_t soc = 0;
    bool charging = false;
    bool batt_ok = battery_read(&soc, &charging);
    if (batt_ok) {
        snprintf(batt, sizeof(batt), "%u%%", (unsigned)soc);
    } else {
        snprintf(batt, sizeof(batt), "--");
    }
    /* Icone agrandie (36x20 -> 64x28, retour utilisateur 2026-09-16 :
     * illisible en petit) + texte "Charging" explicite a cote — le petit
     * eclair dessine par le composant restait peu visible meme agrandi,
     * mieux vaut ne pas compter dessus seul pour l'etat de charge. */
    const int batt_icon_w = 64, batt_icon_h = 28, batt_gap = 14;
    const char *charge_label = (batt_ok && charging) ? "Charging" : "";
    int batt_total_w = GFX_TEXT_WIDTH(batt) + batt_gap + batt_icon_w;
    if (charge_label[0]) batt_total_w += batt_gap + GFX_TEXT_WIDTH(charge_label);
    const int batt_x = (SCREEN_WIDTH - batt_total_w) / 2;
    gfx_draw_string(batt_x, 108, batt, COLOR_BLACK, COLOR_WHITE);
    const int batt_icon_x = batt_x + GFX_TEXT_WIDTH(batt) + batt_gap;
    if (batt_ok) {
        battery_icon_draw(batt_icon_x, 108, batt_icon_w, batt_icon_h, soc, charging);
    }
    if (charge_label[0]) {
        gfx_draw_string(batt_icon_x + batt_icon_w + batt_gap, 108, charge_label,
                        COLOR_BLACK, COLOR_WHITE);
    }

    char dt[24];
    rtc_time_t t;
    if (rtc_read(&t)) {
        snprintf(dt, sizeof(dt), "%02d/%02d %02d:%02d", t.day, t.month, t.hour, t.minute);
    } else {
        snprintf(dt, sizeof(dt), "--/-- --:--");
    }
    gfx_draw_string(GFX_CENTER_X(dt), 144, dt, COLOR_BLACK, COLOR_WHITE);

    /* SD sur sa propre ligne (demande 2026-09-15 : partageait la ligne
     * avec le statut touch T:OK/KO avant son retrait, plus de raison de
     * la garder collee a la date). */
    gfx_draw_string(MENU_PAD_X, 180, "SD:", COLOR_BLACK, COLOR_WHITE);
    gfx_draw_string(MENU_PAD_X + 96, 180, has_fwfile ? "FW" : "-",
                    COLOR_BLACK, COLOR_WHITE);
    gfx_hline(20, 222, SCREEN_WIDTH - 40, COLOR_BLACK);

    for (int i = 0; i < menu_count; i++) {
        int y = MENU_START_Y + i * MENU_ITEM_H;
        if (i == menu_selected) {
            /* selection = inversion video */
            gfx_fill_rect(MENU_PAD_X, y, SCREEN_WIDTH - 2 * MENU_PAD_X,
                          MENU_ITEM_H - 8, COLOR_BLACK);
            gfx_draw_string(MENU_PAD_X + 20, y + 14, menu_items[i].label,
                            COLOR_WHITE, COLOR_BLACK);
        } else {
            gfx_fill_rect(MENU_PAD_X, y, SCREEN_WIDTH - 2 * MENU_PAD_X,
                          MENU_ITEM_H - 8, COLOR_WHITE);
            gfx_draw_string(MENU_PAD_X + 20, y + 14, menu_items[i].label,
                            COLOR_BLACK, COLOR_WHITE);
        }
    }

    if (last_status[0] && !last_status_ok) {
        /* Erreur : bandeau inverse en incrustation sur l'ecran menu, PAS un
         * ecran separe (demande 2026-09-16 : le message doit rester sur le
         * menu -> refresh partiel possible, pas de full refresh dedie). */
        const int box_x = 8, box_h = GFX_FONT_H + 16;
        const int box_y = STATUS_Y - 20;
        gfx_fill_rect(box_x, box_y, SCREEN_WIDTH - 2 * box_x, box_h, COLOR_BLACK);
        gfx_draw_string(GFX_CENTER_X(last_status), box_y + 8, last_status,
                        COLOR_WHITE, COLOR_BLACK);
    } else {
        gfx_hline(20, STATUS_Y - 8, SCREEN_WIDTH - 40, COLOR_BLACK);
        const char *footer = last_status[0] ? last_status
                                            : "L/R=nav Home=OK Power=Cancel";
        gfx_draw_string(GFX_CENTER_X(footer), STATUS_Y, footer,
                        COLOR_BLACK, COLOR_WHITE);
    }

    /* Navigation = fast DU (sans flash) ; etats/status = full GC net. */
    if (fast) {
        gfx_flush_fast();
    } else {
        gfx_flush();
    }
}

static void draw_menu(void)
{
    draw_menu_impl(false);
}

static void draw_menu_nav(void)
{
    draw_menu_impl(true);
}

static void set_status(const char *msg, bool ok)
{
    strncpy(last_status, msg, sizeof(last_status) - 1);
    last_status[sizeof(last_status) - 1] = '\0';
    last_status_ok = ok;
    /* Refresh rapide (demande 2026-09-16) : le message reste incruste sur
     * le menu (cf. draw_menu_impl, bandeau inverse si !ok), plus besoin
     * d'un full refresh dedie a chaque statut — le budget de refresh rapide
     * de eink.c retombe en full GC tout seul quand necessaire. */
    draw_menu_nav();
}

/* -----------------------------------------------------------------------
 * Progression / resultats (ecrans dedies, refresh par paliers de 10%)
 * ----------------------------------------------------------------------- */

static void draw_progress(const char *title, int percent)
{
    gfx_clear(COLOR_WHITE);
    gfx_rect(2, 2, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, COLOR_BLACK);

    gfx_draw_string(GFX_CENTER_X(title), 120, title, COLOR_BLACK, COLOR_WHITE);
    const char *warn = "Ne PAS couper l'alimentation !";
    gfx_draw_string(GFX_CENTER_X(warn), 150, warn, COLOR_BLACK, COLOR_WHITE);

    int bar_x = 100, bar_y = 240, bar_w = SCREEN_WIDTH - 200, bar_h = 50;
    gfx_rect(bar_x, bar_y, bar_w, bar_h, COLOR_BLACK);
    int fill_w = ((bar_w - 4) * percent) / 100;
    if (fill_w > 0) {
        gfx_fill_rect(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, COLOR_BLACK);
    }

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    char pct[16];
    snprintf(pct, sizeof(pct), "%d%%", percent);
    gfx_draw_string(GFX_CENTER_X(pct), bar_y + bar_h + 20, pct,
                    COLOR_BLACK, COLOR_WHITE);
    gfx_flush();
}


/* -----------------------------------------------------------------------
 * Actions
 * ----------------------------------------------------------------------- */

static void action_boot(const char *label)
{
    /* Splash au lieu du texte "Boot app0..." (demande 2026-09-15) : c'est
     * la meme image que l'app affichera a son propre demarrage -> transition
     * visuelle continue au lieu d'un aller-retour menu->splash->app. */
    splash_show();
    boot_partition(label);
    /* si on revient ici : echec (tres improbable, app0 est toujours
     * presente dans la table de partitions) — la, un vrai texte d'erreur
     * reste justifie. */
    char msg[40];
    snprintf(msg, sizeof(msg), "%s introuvable !", label);
    set_status(msg, false);
}

/**
 * @brief Flash /sdcard/msf-fw.bin vers app0 (esp_ota_*). La factory ne se
 *        met JAMAIS a jour depuis la SD : elle s'ecrirait sur la partition
 *        qu'elle execute elle-meme (XIP) — toute interruption en plein vol
 *        (coupure batterie, bug) la laisse invalide sans filet de recovery
 *        sans PC. Traitee comme le bootloader : mise a jour uniquement par
 *        USB/serial (flash_mgr.py --variant x4pro_factory), jamais en
 *        self-service sur le terrain. Decision utilisateur 2026-09-15.
 */
/* Erreur SD/flash visible (demande 2026-09-15/16) : bandeau inverse
 * incruste sur le menu (draw_menu_impl + last_status_ok=false), pas un
 * ecran separe — pas besoin, il y a la place sur le menu, et ca evite un
 * aller-retour en full refresh vers un ecran dedie puis retour. */
static void fail_result(const char *msg)
{
    probe_sd_files();
    set_status(msg, false);
}

static void action_sd_flash(const char *target_label)
{
    /* BUGFIX 2026-09-16 : "Flash en cours..." se declenchait ICI (full
     * refresh immediat), avant meme de savoir si la SD est montee ou le
     * fichier present -> jusqu'a 3 flashs full-refresh d'affilee pour une
     * simple erreur de validation (flash "en cours" inutile, puis l'erreur,
     * puis le retour menu). Deplace juste avant esp_ota_begin : un seul
     * flash dans le cas d'erreur (celui du message lui-meme). */
    FACTORY_LOGD(TAG, "SD flash -> %s", target_label);

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, target_label);
    if (!part) {
        fail_result("Partition cible absente !");
        return;
    }

    if (sdcard_mount() != ESP_OK) {
        fail_result("Pas de carte SD !");
        return;
    }

    FILE *fw = fopen(FW_FILENAME, "rb");
    if (!fw) {
        ESP_LOGE(TAG, "%s absent (%s)", FW_FILENAME, strerror(errno));
        sdcard_unmount();
        fail_result("msf-fw.bin absent de la SD !");
        return;
    }
    fseek(fw, 0, SEEK_END);
    long fw_size = ftell(fw);
    fseek(fw, 0, SEEK_SET);
    if (fw_size <= 0 || fw_size > (long)part->size) {
        ESP_LOGE(TAG, "taille invalide: %ld (max %lu)", fw_size,
                 (unsigned long)part->size);
        fclose(fw);
        sdcard_unmount();
        fail_result("Taille firmware invalide !");
        return;
    }

    char title[40];
    snprintf(title, sizeof(title), "Flash %s <- SD", target_label);

    /* Toutes les validations sont passees : c'est le premier flash reel. */
    set_status("Flash en cours...", true);

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(part, fw_size, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        fclose(fw);
        sdcard_unmount();
        fail_result("OTA begin KO !");
        return;
    }

    uint8_t buf[1024];
    long written = 0;
    int last_shown = -10;
    bool ok = true;

    while (written < fw_size) {
        size_t to_read = sizeof(buf);
        if (written + (long)to_read > fw_size) to_read = fw_size - written;
        if (fread(buf, 1, to_read, fw) == 0) {
            ESP_LOGE(TAG, "erreur lecture @%ld", written);
            ok = false;
            break;
        }
        err = esp_ota_write(ota_handle, buf, to_read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ecriture @%ld: %s", written, esp_err_to_name(err));
            ok = false;
            break;
        }
        written += (long)to_read;
        int percent = (int)((written * 100) / fw_size);
        if (percent - last_shown >= 10 || percent == 100) {
            draw_progress(title, percent);   /* full refresh ~2-4 s par palier */
            last_shown = percent;
        }
    }
    fclose(fw);

    if (ok) {
        if (esp_ota_end(ota_handle) != ESP_OK) ok = false;
    } else if (ota_handle) {
        esp_ota_abort(ota_handle);
    }

    if (ok) {
        /* Bandeau in-menu (pas d'ecran separe, demande 2026-09-16), non
         * inverse (ok=true) — le temps de le lire avant le reboot. */
        set_status("Flash OK ! Redemarrage...", true);
        remove(FW_OK_FILENAME);
        rename(FW_FILENAME, FW_OK_FILENAME);
        sdcard_unmount();
        vTaskDelay(pdMS_TO_TICKS(2000));
        boot_partition(target_label);
    }

    remove(FW_BAD_FILENAME);
    rename(FW_FILENAME, FW_BAD_FILENAME);
    sdcard_unmount();
    probe_sd_files();
    set_status("Flash ECHEC !", false);
}

static void execute_selected_action(void)
{
    switch (menu_items[menu_selected].action) {
        case MENU_ACTION_BOOT_APP0:    action_boot("app0");    break;
        case MENU_ACTION_SD_APP0:      action_sd_flash("app0");   break;
    }
}

static void action_cancel(void)
{
    /* Power = Cancel (spec utilisateur 2026-09-14) : reboot sur la partition
     * par defaut. otadata a deja ete restaure au boot -> un simple reboot
     * renvoie vers la bonne partition OTA (factory si aucune).
     * Pas de set_status()/redraw ici (retire 2026-09-15) : le splash est
     * deja affiche par la boucle principale des la detection brute du
     * bouton Power, avant meme d'arriver ici. Un second flash vers un
     * ecran menu+texte juste avant le power-off serait redondant et
     * casserait la continuite visuelle du splash comme image de veille. */
    ESP_LOGI(TAG, "cancel -> reboot partition par defaut (%s)",
             s_active_label);
    eink_power_off();
    esp_restart();
}

static void dispatch_button(button_id_t btn)
{
    switch (btn) {
        case BTN_1:
            menu_selected = (menu_selected + menu_count - 1) % menu_count;
            draw_menu_nav();
            break;
        case BTN_2:
            menu_selected = (menu_selected + 1) % menu_count;
            draw_menu_nav();
            break;
        case BTN_3:
            action_cancel();
            break;
        default:
            break;
    }
}

/* Hit-test pad Home : la zone est detectee dans touch_read() (raw valide
 * par mesure 01:34) — aucune calibration a faire ici. */

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

void app_main(void)
{
    factory_log_silence_sd_stack();
    FACTORY_LOGD(TAG, "MySafeFob Recovery demarre");

    /* 1. Restore otadata AVANT TOUT (power-off depuis factory = retour a
     *    la bonne partition OTA). Logique PiBot identique. */
    restore_otadata_from_backup();
    cache_active_ota_label();
    ESP_LOGI(TAG, "active partition: %s", s_active_label);

    /* 2. E-ink + gfx */
    esp_err_t err = eink_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "eink init: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(200));
        abort();
    }
    gfx_init();

    /* 2b. Splash (ADR-010 amende) : couvre le temps d'init entrees/SD
     * ci-dessous d'une image fixe plutot que de laisser l'utilisateur voir
     * directement le menu se faire flasher par son propre full refresh —
     * le flash physique du GC reste inevitable (ADR-010, section e-paper),
     * seul CE QUI flashe change. */
    splash_show();

    /* 3. Entrees */
    buttons_init();
    bool touch_ok = touch_init();
    FACTORY_LOGD(TAG, "touch: %s", touch_ok ? "OK" : "ABSENT");

    /* 4. SD (plus de sondage app1 : partition retiree, ADR-011 ; pas de
     * "SD -> factory" : la factory ne s'auto-met jamais a jour, cf.
     * commentaire de action_sd_flash()) */
    menu_count = 0;
    menu_items[menu_count++] = (menu_item_t){ "Boot app0", MENU_ACTION_BOOT_APP0 };
    menu_items[menu_count++] = (menu_item_t){ "SD -> app0", MENU_ACTION_SD_APP0 };
    menu_selected = 0;
    probe_sd_files();
    draw_menu();

    /* 5. Boucle : boutons physiques (Power = Cancel) + pad Home (touch) =
     *    validation. NB : si le touch est KO, plus de validation possible —
     *    le touch devient critique pour la recovery (diags au boot). */
    bool touch_was_pressed = false;
    /* Rafraichissement ambiant batterie/charge/date (demande 2026-09-15) :
     * draw_menu_impl() ne relit la jauge/RTC qu'a chaque redraw, donc
     * jamais si l'utilisateur reste sans interagir. 45 s (compromis
     * 30-60 s) plutot que 1 s : un redraw e-ink automatique par seconde
     * ferait scintiller l'ecran en continu et reconsommerait le budget de
     * refresh rapide bien plus vite (retour du ghosting deja corrige). */
    int64_t last_ambient_refresh = esp_timer_get_time();
    while (1) {
        int64_t now = esp_timer_get_time();
        if (now - last_ambient_refresh > 45LL * 1000000) {
            draw_menu_nav();   /* fast refresh, actualise batt/charge/date */
            last_ambient_refresh = now;
        }

        /* Feedback immediat sur Power (demande utilisateur 2026-09-15) :
         * pas de buzzer (ADR-007), et button_wait_press() ne retourne
         * qu'APRES relachement -> sans ca, aucun signal ne dit "c'est pris
         * en compte, tu peux lacher". Le splash (full refresh) demarre
         * des la detection brute (avant meme le debounce) : le fait que
         * l'ecran commence a flasher EST le signal de relachement, pas
         * besoin d'attendre la fin du refresh. Uniquement sur Power : Left/
         * Right/Home ne doivent pas flasher a chaque appui de nav. */
        if (gpio_get_level(BTN_POWER_PIN) == 0) {
            splash_show();
        }
        dispatch_button(button_wait_press(50));

        if (touch_ok) {
            touch_point_t tp = touch_read();
            if (tp.pressed && !touch_was_pressed) {
                /* DIAG 2026-09-15 : rien ne remontait quand l'utilisateur
                 * touchait l'ecran -> touch_read() n'avait aucun log. On
                 * trace chaque front presse (edge, pas de spam a 20 Hz)
                 * pour savoir si le GT911 remonte des points du tout, et
                 * s'ils tombent dans la zone Home (rx<70, ry 380-580). */
                ESP_LOGI(TAG, "touch: point (x=%d y=%d) home=%s",
                         tp.x, tp.y, tp.home ? "OUI" : "non");
            }
            if (tp.pressed && !touch_was_pressed && tp.home) {
                vTaskDelay(pdMS_TO_TICKS(80));   /* feedback minimal */
                execute_selected_action();
            }
            touch_was_pressed = tp.pressed;
        }
    }
}
