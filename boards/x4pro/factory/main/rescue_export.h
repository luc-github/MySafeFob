/*
 Project: MySafeFob  rescue_export.h
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
 * @file rescue_export.h
 * @brief MySafeFob Factory — rescue export of the account store to the SD
 *        card (2026-10-11, user decision: option A of ADR-005's amendment).
 *        For the rare case where the app no longer starts: the device PIN
 *        is asked (Left/Right change the digit, Home confirms, Power
 *        cancels) and checked against the store with the app's rules
 *        (failure counter, back-off); then the newest store slot is copied
 *        as is to /sdcard/MSF/RESCUEnn.MSR. The file is still encrypted
 *        with the PIN-derived key only (weaker than a .msfx passphrase):
 *        tools/msf_backup.py decrypts it with the PIN; delete it after use.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Runs the whole flow (own screens). Returns false if the user
 *        cancelled; otherwise `msg` holds the one-line result for the menu
 *        status (at most 29 characters) and *ok whether it succeeded.
 */
bool rescue_export_run(bool touch_ok, char *msg, size_t msg_size, bool *ok);
