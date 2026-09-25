/*
 Project: MySafeFob  settings_defs.inc
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
 * @file settings_defs.inc
 * @brief MySafeFob App — settings table (X-macro), settings_store.c/.h.
 *
 * Pattern adapted from references/Luc-Pibot-cnc-pendant-firmware's
 * ESP3DSettings module (esp3d_settings_defs.inc) — same idea (one line per
 * setting: id, NVS key, type, default — generates the id enum AND the
 * lookup table settings_store.c reads/writes against, so a new setting
 * never needs a hand-written NVS read/write pair), stripped down to just
 * that idea. NOT a vendored copy: PiBot's actual ESP3DSettings class is a
 * full C++ module tied to its own WiFi/CNC/INI-update type system
 * (ip_t/mask/bitsfield, multi-target firmware defs) that doesn't apply
 * here — this file and settings_store.c are a MySafeFob-specific,
 * from-scratch, much smaller implementation of the same table-driven
 * principle.
 *
 * SETTINGS_DEF(id, nvs_key, type, default_value)
 *   id            -> becomes SETTINGS_ID_<id> in the settings_id_t enum
 *   nvs_key       -> NVS storage key, max 15 chars (NVS limit)
 *   type          -> BOOL or U32 (settings_type_t, settings_store.c)
 *   default_value -> returned whenever the key isn't set yet (or NVS
 *                    failed to open) — never touches flash to read a
 *                    default
 *
 * #include this file only from inside settings_store.c's X-macro blocks
 * (SETTINGS_DEF defined before, #undef'd after) — never on its own.
 */

SETTINGS_DEF(PowerShortConfirm, "pwr_shortcfm", BOOL, 1)
SETTINGS_DEF(IdleTimeoutS,      "idle_to_s",    U32,  0)
SETTINGS_DEF(FrontlightOn,        "fl_on",      BOOL, 0)
SETTINGS_DEF(FrontlightColor,     "fl_color",   U32,  0)   /* 0-100 warm<->cool mix, 0=warm */
SETTINGS_DEF(FrontlightIntensity, "fl_intens",  U32,  60)
/* Touch calibration (touch.c's per-unit correction, ui_screen_touch_diag.cpp) --
 * scale is x1000 fixed-point (1000 = 1.000x); offset is a signed pixel
 * delta stored via its raw 32-bit bit pattern (nvs_(get|set)_u32 round-trip
 * two's complement bit-for-bit, no bias trick needed -- see
 * settings_store.h's touch cal accessors). */
SETTINGS_DEF(TouchCalScaleX,  "tc_scale_x", U32, 1000)
SETTINGS_DEF(TouchCalOffsetX, "tc_off_x",   U32, 0)
SETTINGS_DEF(TouchCalScaleY,  "tc_scale_y", U32, 1000)
SETTINGS_DEF(TouchCalOffsetY, "tc_off_y",   U32, 0)
/* Local time zone as a fixed UTC offset in minutes (signed, stored via its
 * raw 32-bit pattern like the touch offsets). Display only: the RTC, the
 * system clock and TOTP are always UTC. */
SETTINGS_DEF(TimeTzOffsetMin, "tz_off_min", U32, 0)
/* Last 3 clock synchronisations, newest = index 0 (drift tracking, shown in
 * About): UTC epoch of the sync (0 = none), how far the clock was off just
 * before it (signed seconds, new minus old; INT32_MIN = unknown), and the
 * source (0 manual, 1 Wi-Fi, 2 BLE). settings_store_push_time_sync() shifts
 * the records down. */
SETTINGS_DEF(TimeSyncEpoch0,  "sync_e0", U32, 0)
SETTINGS_DEF(TimeSyncEpoch1,  "sync_e1", U32, 0)
SETTINGS_DEF(TimeSyncEpoch2,  "sync_e2", U32, 0)
SETTINGS_DEF(TimeSyncDelta0,  "sync_d0", U32, 0x80000000u)
SETTINGS_DEF(TimeSyncDelta1,  "sync_d1", U32, 0x80000000u)
SETTINGS_DEF(TimeSyncDelta2,  "sync_d2", U32, 0x80000000u)
SETTINGS_DEF(TimeSyncSource0, "sync_s0", U32, 0)
SETTINGS_DEF(TimeSyncSource1, "sync_s1", U32, 0)
SETTINGS_DEF(TimeSyncSource2, "sync_s2", U32, 0)
