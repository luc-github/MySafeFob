/*
 Project: MySafeFob  usb_disk.h
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
 * @file usb_disk.h
 * @brief MySafeFob App (X4 Pro) — USB disk mode (2026-10-17): the SD card
 *        becomes a USB mass-storage disk on the PC (TinyUSB MSC over the
 *        S3's USB-OTG), to copy .msfx backups in and out without removing
 *        the card. Same approach as references/crosspoint-reader's "USB
 *        Transfer" on this hardware:
 *        - the USB-OTG controller takes over the internal PHY that the
 *          USB-Serial/JTAG console uses: no console during the session;
 *        - the device never touches the file system meanwhile (raw sector
 *          access for the PC only);
 *        - the session ends with a restart (PHY handed back to the
 *          console, card remounted cleanly, store locked).
 *        Only the SD card is exposed, never the internal flash.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    USB_DISK_OFF,            /* not started, or start failed */
    USB_DISK_WAITING,        /* started, no PC has configured the device yet */
    USB_DISK_CONNECTED,      /* a PC sees the disk */
    USB_DISK_DISCONNECTED,   /* the PC was there and is gone (cable out, PC asleep) */
} usb_disk_state_t;

/**
 * @brief Opens the SD card raw and starts the USB disk. The caller has
 *        already locked the store and leaves the card alone afterwards.
 * @return ESP_OK, or the error of the card / USB step that failed.
 */
esp_err_t usb_disk_start(void);

usb_disk_state_t usb_disk_state(void);

/**
 * @brief Detaches from the PC, hands the PHY back to the USB-Serial/JTAG
 *        console and restarts. Never returns. Also valid if the start
 *        failed half way.
 */
void usb_disk_finish_and_restart(void);

#ifdef __cplusplus
}
#endif
