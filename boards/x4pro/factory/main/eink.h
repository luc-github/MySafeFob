/**
 * @file eink.h
 * @brief MySafeFob Factory — driver E-Ink UC8279 (X4 Pro).
 *
 * Extrait du probe valide test_apps/x4pro-probe/main/eink_test.c
 * (sequences UC8279 mesurees 2026-09-13 — docs/hardware-specs.md).
 *
 * Contraintes critiques (NE PAS modifier sans revalidation hardware) :
 *  - PSR 0x37 (REG=1) a l'init ; entre PON et DRF re-ecriture COMPLETE des
 *    registres avec PSR 0x17 (REG=0, scan MTP). JAMAIS 0x37 au DRF
 *    (full GC bloque : BUSY LOW > 20 s, ecran gris).
 *  - Stream mode "brut" : gates 0..119 blancs (pad), 480 lignes fb en ordre
 *    direct octets tels quels, pad blanc jusqu'a 600 gates.
 *    => rotation 90 CW materielle, aucun transform logiciel.
 *  - BUSY actif-LOW ; timeout DRF 20 s ; chunk SPI <= 16 Ko (DMA S3).
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Init SPI + GPIO + reset + sequence d'init UC8279 (PSR 0x37).
 */
esp_err_t eink_init(void);

/**
 * @brief Affiche le framebuffer complet (1 bpp, 0xFF = blanc, 0x00 = noir,
 *        MSB-first, 100 octets/ligne x 480) puis refresh FULL GC.
 *
 * Un full refresh dure ~2-4 s (bloquant). Ne pas appeler en boucle rapide.
 */
esp_err_t eink_display_fb(const uint8_t *fb);

/**
 * @brief Affiche le framebuffer en refresh rapide DU (waveform OTP du
 *        stock FW : TSSET 0x5A + CDI 0xD7 + fenetre PTL pleine).
 *
 * Sans flash d'inversion, ~0,5-1 s bloquant. Differentiel : diff contre la
 * frame precedente, resync DTM1 apres. Retombe automatiquement sur un full
 * GC quand la frame precedente est inconnue (1er affichage) ou toutes les
 * EINK_FAST_BUDGET (10) fasts (purge des ghosts).
 */
esp_err_t eink_display_fb_fast(const uint8_t *fb);

/**
 * @brief Met le controleur en power-off (POF + attente idle).
 */
esp_err_t eink_power_off(void);
