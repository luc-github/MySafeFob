/*
 Project: MySafeFob  time_service.h
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
 * @file time_service.h
 * @brief MySafeFob App — the device clock. The system clock and the BM8563
 *        battery-backed RTC (I2C 0x51) always hold UTC (TOTP needs UTC, RFC
 *        6238). The time zone is a fixed UTC offset kept in settings_store
 *        and only used to convert to/from the local time shown in the UI.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int year;   /* full year, e.g. 2026 */
    int month;  /* 1-12 */
    int day;    /* 1-31 */
    int hour;
    int minute;
    int second;
} time_service_dt_t;

/** @brief Loads the RTC into the system clock. Call once at boot (I2C bus must be usable). */
void time_service_init(void);

/** @brief true once a plausible time (year >= 2024) is available. */
bool time_service_is_valid(void);

/** @brief Current UTC epoch seconds (0 if the clock was never set). */
time_t time_service_get_utc(void);

typedef enum { TIME_SOURCE_MANUAL = 0, TIME_SOURCE_WIFI = 1, TIME_SOURCE_BLE = 2 } time_sync_source_t;

/** @brief Marks "offset unknown" in the stored sync record (clock was not set before). */
#define TIME_SYNC_DELTA_UNKNOWN ((int32_t)0x80000000)

/**
 * @brief Sets the system clock and the RTC from a UTC epoch, and records the
 *        sync (date, offset old->new, source) for drift tracking.
 * @return false if the RTC write failed (system clock is still set).
 */
bool time_service_set_utc(time_t utc, time_sync_source_t source);

/**
 * @brief Sets ONLY the system clock, with microsecond precision, and returns
 *        how far off it was (new minus old, rounded to seconds). Cheap and
 *        non-blocking: call it at the instant the time is received, then call
 *        time_service_commit_system_time() (which blocks 1-2 s for the aligned
 *        RTC write) once the source has finished.
 * @param delta_valid set to false if the clock had never been set.
 */
void time_service_set_system_precise(time_t utc, int usec, int32_t *delta_s, bool *delta_valid);

/**
 * @brief For sources that already set the system clock themselves (SNTP):
 *        writes the current system time to the RTC and records the sync.
 *        `delta_s` is how far off the clock was at the instant the source
 *        set it (new minus old, measured by the caller), `delta_valid`
 *        false if the clock had never been set.
 */
bool time_service_commit_system_time(int32_t delta_s, bool delta_valid, time_sync_source_t source);

typedef enum {
    TIME_SYNC_DUE_DISABLED,      /* TimeSyncMaxAgeS = 0: no staleness alert */
    TIME_SYNC_DUE_NEVER_SYNCED,  /* no sync recorded */
    TIME_SYNC_DUE_OK,            /* next sync due in the future */
    TIME_SYNC_DUE_OVERDUE,       /* due date reached (or the clock is not valid) */
} time_sync_due_t;

/**
 * @brief When the next time sync is due: last sync + TimeSyncMaxAgeS
 *        (ADR-018). Single source of truth for Settings > About and HOME's
 *        "time sync is old" alert, so both always agree.
 * @param due_utc set to the due date (UTC epoch) for OK/OVERDUE, else 0.
 */
time_sync_due_t time_service_next_sync_due(time_t *due_utc);

/** @brief Validates a calendar date/time (year 2024-2099). */
bool time_service_dt_is_valid(const time_service_dt_t *dt);

/** @brief Calendar date/time -> epoch seconds, as if `dt` were UTC. */
time_t time_service_dt_to_epoch(const time_service_dt_t *dt);

/** @brief Epoch seconds -> calendar date/time (as UTC). */
void time_service_epoch_to_dt(time_t t, time_service_dt_t *out);

#ifdef __cplusplus
}
#endif
