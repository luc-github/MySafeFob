/*
 Project: MySafeFob  cmd_settime.c
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
 * @file cmd_settime.c
 * @brief MySafeFob — `settime` console command: sets the clock from the
 *        PC over the serial console (ADR-006 serial channel, ROADMAP 8.0
 *        P4). Same path as the BLE sync: the system clock is set to the
 *        microsecond when the command is parsed, then the RTC is written
 *        (aligned, 1-2 s) and the sync recorded with source "serial".
 *
 *   settime                              show the clock and the last sync
 *   settime <epoch>[.fraction]           UTC epoch seconds, e.g. 1791417821.25
 *   settime YYYY-MM-DDTHH:MM:SS[.f][Z]   UTC date/time ("T" or a space)
 *
 * Always UTC (the time zone setting is display only). Typed by hand the
 * second is enough for TOTP; a PC script sending the epoch with a fraction
 * gets sub-second accuracy. Same format as PiBot's [ESP800]time=, but UTC.
 * The time carries no secret, so no confirmation (the PC drives it).
 */
#include "cmd_settime.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_console.h"
#include "settings_store.h"
#include "time_service.h"
#include "ui_nav.h"

static void print_dt(const char *label, time_t t)
{
    time_service_dt_t dt;
    time_service_epoch_to_dt(t, &dt);
    printf("%s %04d-%02d-%02d %02d:%02d:%02d", label, dt.year, dt.month, dt.day, dt.hour, dt.minute,
           dt.second);
}

static void print_status(void)
{
    /* Read first, so the PC script (tools/settime.py --check) compares
     * against the instant closest to the command's arrival. */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (!time_service_is_valid()) {
        printf("Clock     not set\n");
    } else {
        printf("Epoch     %lld.%03ld\n", (long long)tv.tv_sec, (long)(tv.tv_usec / 1000));
        time_t now = time_service_get_utc();
        print_dt("UTC      ", now);
        int32_t tz = settings_store_get_time_tz_offset_min();
        print_dt("\nLocal    ", now + (time_t)tz * 60);
        printf(" (UTC%+ld min)\n", (long)tz);
    }
    uint32_t epoch, source;
    int32_t delta;
    if (settings_store_get_time_sync(0, &epoch, &delta, &source)) {
        print_dt("Last sync", (time_t)epoch);
        if (delta == TIME_SYNC_DELTA_UNKNOWN) {
            printf(" UTC, %s, offset unknown\n", time_service_source_name(source));
        } else {
            printf(" UTC, %s, offset %+ld s\n", time_service_source_name(source), (long)delta);
        }
    } else {
        printf("Last sync none\n");
    }
}

/* Parses ".ddd..." (any number of digits, microseconds kept). */
static bool parse_fraction(const char *p, int *usec, const char **end)
{
    *usec = 0;
    if (*p != '.') {
        *end = p;
        return true;
    }
    p++;
    if (!isdigit((unsigned char)*p)) return false;
    int scale = 100000;
    while (isdigit((unsigned char)*p)) {
        *usec += (*p - '0') * scale;
        scale /= 10;
        p++;
    }
    *end = p;
    return true;
}

static bool parse_epoch(const char *text, time_t *utc, int *usec)
{
    if (!isdigit((unsigned char)text[0])) return false;
    char *end = NULL;
    long long v = strtoll(text, &end, 10);
    const char *rest;
    if (!parse_fraction(end, usec, &rest) || *rest != '\0') return false;
    *utc = (time_t)v;
    return true;
}

static bool parse_iso(const char *text, time_t *utc, int *usec)
{
    time_service_dt_t dt = {0};
    int n = 0;
    if (sscanf(text, "%4d-%2d-%2d%*1[T ]%2d:%2d:%2d%n", &dt.year, &dt.month, &dt.day, &dt.hour,
               &dt.minute, &dt.second, &n) != 6) {
        return false;
    }
    const char *rest;
    if (!parse_fraction(text + n, usec, &rest)) return false;
    if (*rest == 'Z' || *rest == 'z') rest++;
    if (*rest != '\0') return false;
    if (dt.hour > 23 || dt.minute > 59 || dt.second > 59) return false;
    *utc = time_service_dt_to_epoch(&dt);
    return true;
}

static int usage(void)
{
    printf("Usage: settime [<epoch>[.f] | YYYY-MM-DDTHH:MM:SS[.f][Z]]   (UTC)\n");
    return 1;
}

static int cmd_settime(int argc, char **argv)
{
    board_activity_notify();
    if (argc == 1) {
        print_status();
        return 0;
    }

    /* "2026-10-09 14:30:00" arrives as two arguments. */
    char text[48];
    if (argc == 2) {
        snprintf(text, sizeof(text), "%s", argv[1]);
    } else if (argc == 3) {
        snprintf(text, sizeof(text), "%s %s", argv[1], argv[2]);
    } else {
        return usage();
    }

    time_t utc = 0;
    int usec = 0;
    if (!parse_epoch(text, &utc, &usec) && !parse_iso(text, &utc, &usec)) {
        printf("Invalid time '%s'.\n", text);
        return usage();
    }
    time_service_dt_t dt;
    time_service_epoch_to_dt(utc, &dt);
    if (!time_service_dt_is_valid(&dt)) {
        printf("Out of range: the year must be 2024-2099.\n");
        return 1;
    }

    int32_t delta = 0;
    bool delta_valid = false;
    time_service_set_system_precise(utc, usec, &delta, &delta_valid);
    bool rtc_ok = time_service_commit_system_time(delta, delta_valid, TIME_SOURCE_SERIAL);

    print_dt("Clock set to", utc);
    if (delta_valid) {
        printf(".%03d UTC, it was off by %+ld s.\n", usec / 1000, (long)delta);
    } else {
        printf(".%03d UTC (it was not set before).\n", usec / 1000);
    }
    if (!rtc_ok) {
        printf("WARNING: RTC write failed, the time may be lost after the next sleep or reboot.\n");
        return 1;
    }
    return 0;
}

esp_err_t cmd_settime_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "settime",
        .help = "Set the clock (UTC): settime <epoch>[.f] | YYYY-MM-DDTHH:MM:SS[Z]. "
                "No argument: show the clock and the last sync.",
        .func = &cmd_settime,
    };
    return esp_console_cmd_register(&cmd);
}
