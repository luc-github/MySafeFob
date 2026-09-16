/* 
 Project: MySafeFob  rtc.h
  Copyright (c) 2026 Luc Lebosse. All rights reserved.

  This code is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
/**
 * @file rtc.h
 * @brief MySafeFob Factory — RTC BM8563 (I2C 0x51), lecture seule.
 *        Sequence validee x4pro-probe (cmd_rtc), regs 0x02-0x08 en BCD.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int year;    /* 20YY */
    int month;
    int day;
    int hour;
    int minute;
    int second;
} rtc_time_t;

/**
 * @brief Lit l'heure courante du BM8563.
 * @return true si le RTC a repondu.
 */
bool rtc_read(rtc_time_t *out);
