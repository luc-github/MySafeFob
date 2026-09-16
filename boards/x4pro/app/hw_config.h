/**
 * @file hw_config.h
 * @brief MySafeFob App — X4 Pro hardware pin definitions (splash bring-up).
 *
 * Source de verite : docs/hardware-specs.md (bring-up valide 2026-09-13).
 * Sous-ensemble de factory/main/hw_config.h : uniquement ce dont le splash
 * a besoin (rails + e-ink). Le BSP complet de l'app arrive en Phase 8c
 * (tâche 8.4) — NE PAS dupliquer les drivers touch/SD/RTC ici.
 */
#pragma once

#include "driver/gpio.h"

/* ---- Power rails (prerequis universel, actifs au boot) ---- */
#define RAIL_PERIPH_PIN     GPIO_NUM_1   /* peripheral rail, HIGH = ON, tenu HIGH */
#define RAIL_TOUCH_PIN      GPIO_NUM_2   /* touch power-enable, ACTIVE-LOW (LOW = on) */
#define RAIL_SD_PIN         GPIO_NUM_5   /* SD power-enable, ACTIVE-LOW (LOW = on) */

/* ---- E-Ink UC8279 (SPI2, write-only, CS manuel) ---- */
#define EINK_HOST           SPI2_HOST
#define EINK_SCLK           GPIO_NUM_12
#define EINK_MOSI           GPIO_NUM_11
#define EINK_CS             GPIO_NUM_13
#define EINK_DC             GPIO_NUM_18
#define EINK_RST            GPIO_NUM_14
#define EINK_BUSY           GPIO_NUM_6   /* BUSY_N : LOW = occupe, HIGH = idle */
#define EINK_SPI_HZ         10000000     /* 10 MHz */

/* Panneau : fb mémoire natif 800x480 PAYSAGE (100 octets/ligne). Le stream
 * brut = rotation 90 CW matérielle — ORIENTATION CORRECTE (probe mode 0,
 * mesure 12:18). L'UI travaille en coords PORTRAIT 480x800 : transpose à
 * l'écriture, fb_x = uy, fb_y = 479 - ux (miroir X inclus — fb(0,0) s'affiche
 * en haut-DROITE). NE PAS ajouter de transform au stream. */
#define EINK_W              800
#define EINK_H              480
#define EINK_WB             (EINK_W / 8)                  /* 100 octets/ligne fb */
#define SCREEN_WIDTH        480                          /* UI portrait */
#define SCREEN_HEIGHT       800
#define SCREEN_FB_SIZE      (EINK_WB * EINK_H)            /* 48000 octets */
