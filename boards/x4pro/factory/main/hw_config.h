/**
 * @file hw_config.h
 * @brief MySafeFob Factory — X4 Pro hardware pin definitions (ADR-008).
 *
 * Source de verite : docs/hardware-specs.md (bring-up valide 2026-09-13).
 * Portage du pattern PiBot hw_config.h vers le X4 Pro.
 */
#pragma once

#include "driver/gpio.h"

/* ---- Power rails (prerequis universel, actifs au boot) ---- */
#define RAIL_PERIPH_PIN     GPIO_NUM_1   /* peripheral rail, HIGH = ON, tenu HIGH */
#define RAIL_TOUCH_PIN      GPIO_NUM_2   /* touch power-enable, ACTIVE-LOW (LOW = on) */
#define RAIL_SD_PIN         GPIO_NUM_5   /* SD power-enable, ACTIVE-LOW, pulse au mount */

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
 * brut = rotation 90 CW materielle — c'est l'ORIENTATION CORRECTE du device
 * (probe mesure 12:18, mode 0). L'UI travaille en coordonnees PORTRAIT
 * 480x800 : gfx transpose a l'ecriture (user(x,y) -> fb(y,x)) — la transpose
 * annule la rotation du panneau, le rendu est droit (validation fleche 12:38).
 * NE PAS ajouter de transform au stream : tout passe par put_pixel(). */
#define EINK_W              800
#define EINK_H              480
#define EINK_WB             (EINK_W / 8)                  /* 100 octets/ligne fb */
#define SCREEN_WIDTH        480                          /* UI portrait */
#define SCREEN_HEIGHT       800
#define SCREEN_FB_SIZE      (EINK_WB * EINK_H)            /* 48000 octets */

/* ---- Bus I2C #0 (partage : GT911, BM8563, CW2017 — cf. i2c_bus.c) ---- */
#define I2C_PORT            I2C_NUM_0
#define I2C_SDA_PIN         GPIO_NUM_39
#define I2C_SCL_PIN         GPIO_NUM_38
#define I2C_FREQ_HZ         400000

/* ---- RTC BM8563 (0x51) + gauge CW2017 (0x63), meme bus I2C #0 ---- */
#define RTC_I2C_ADDR        0x51
#define GAUGE_I2C_ADDR      0x63
#define CHARGE_PIN          GPIO_NUM_21   /* actif-HIGH = en charge/USB */

/* ---- Touch GT911 ---- */
#define TOUCH_RST_PIN       GPIO_NUM_4
#define TOUCH_INT_PIN       GPIO_NUM_10  /* LOW au reset -> adresse 0x5D ; POR
                                            sous INT low = mode CONFIG UPDATE */
/* Mapping valide (test 4 coins 00:55, hardware-specs.md) :
 * coords framebuffer (paysage 800x480) : fb_x = raw_y, fb_y = 479 - raw_x.
 * (swapXY = true, invert_y post-swap = true) */

/* ---- Boutons physiques (actif-LOW, pull-up interne) ---- */
/* GPIO0 = strapping : OK a runtime, jamais maintenu au boot (cf. hooks.c). */
#define BTN_LEFT_PIN        GPIO_NUM_0   /* role : haut (BTN1) */
#define BTN_RIGHT_PIN       GPIO_NUM_7   /* role : bas (BTN2) ; trigger recovery au boot */
#define BTN_POWER_PIN       GPIO_NUM_3   /* role : select (BTN3) ; ne coupe pas l'alim */

/* ---- SD card — SDMMC natif 1-bit, slot 1 ---- */
#define SD_MOUNT_POINT      "/sdcard"
#define SD_CLK_PIN          GPIO_NUM_41
#define SD_CMD_PIN          GPIO_NUM_42
#define SD_D0_PIN           GPIO_NUM_40
/* power : RAIL_SD_PIN (GPIO5), pulse HIGH 80 ms -> LOW 120 ms au mount */
