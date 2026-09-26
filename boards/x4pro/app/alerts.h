/*
 Project: MySafeFob  alerts.h
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
 * @file alerts.h
 * @brief MySafeFob App — alert registry (ADR-018): a fixed list of
 *        condition-based alerts, shown by HOME's warning button and the
 *        Alerts screen. An alert is active while its condition holds; there
 *        is no acknowledge state. Evaluated when HOME/Alerts are shown,
 *        never from a timer. Texts never contain a secret.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ALERT_TIME_NEVER_SYNCED,  /* no time sync recorded */
    ALERT_TIME_SYNC_STALE,    /* last sync older than TimeSyncMaxAgeS */
    ALERT_COUNT
} alert_id_t;

/** @brief Re-checks every condition, logs the result, returns the active count. */
int alerts_evaluate(void);

/** @brief Result of the last alerts_evaluate(). */
bool alerts_is_active(alert_id_t id);

/** @brief Short title, e.g. "Time sync is old". */
const char *alerts_title(alert_id_t id);

/** @brief Explanation (what is wrong, why it matters, what to do), built now. */
void alerts_describe(alert_id_t id, char *buf, size_t len);

#ifdef __cplusplus
}
#endif
