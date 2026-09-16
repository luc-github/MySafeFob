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
