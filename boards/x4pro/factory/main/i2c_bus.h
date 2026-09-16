/**
 * @file i2c_bus.h
 * @brief MySafeFob Factory — bus I2C partage (SDA=39/SCL=38), un seul
 *        i2c_master_bus_handle_t pour tous les peripheriques (GT911 touch,
 *        BM8563 RTC, CW2017 gauge). ESP-IDF n'autorise qu'UNE instance de
 *        bus par port I2C — chaque driver ajoute/retire ses propres
 *        i2c_master_dev_handle_t sur ce bus partage (meme pattern que
 *        touch.c pour l'acces par device, deja valide sur cette unite).
 */
#pragma once

#include "driver/i2c_master.h"

/**
 * @brief Retourne le bus I2C partage, le cree au premier appel.
 */
esp_err_t i2c_bus_get(i2c_master_bus_handle_t *out);
