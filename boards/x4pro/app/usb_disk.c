/*
 Project: MySafeFob  usb_disk.c
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
 * @file usb_disk.c
 * @brief MySafeFob App (X4 Pro) — USB disk mode, see usb_disk.h. Built on
 *        espressif/esp_tinyusb 2.x (MSC storage over an sdmmc_card_t),
 *        with automatic app-side remounting turned off: the device never
 *        mounts the FAT while the PC owns it, even after an eject.
 */
#include "usb_disk.h"

#include <stdbool.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdcard.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/usb_serial_jtag_reg.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "tusb.h"

#include "app_log_workaround.h"

static const char *TAG = "usb_disk";

static bool s_usb_up;          /* tinyusb_driver_install() succeeded */
static bool s_host_seen;       /* a PC configured the device at least once */

esp_err_t usb_disk_start(void)
{
    if (s_usb_up) return ESP_OK;
    sdcard_unmount();   /* normally already: the backup flows unmount after each use */
    sdmmc_card_t *card = NULL;
    esp_err_t err = sdcard_open_raw(&card);
    if (err != ESP_OK) return err;

    const tinyusb_msc_driver_config_t msc_cfg = {
        .user_flags.auto_mount_off = 1,   /* never remount for the app on eject / unplug */
    };
    err = tinyusb_msc_install_driver(&msc_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MSC driver: %s", esp_err_to_name(err));
        return err;
    }
    const tinyusb_msc_storage_config_t storage_cfg = {
        .medium.card = card,
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
        .fat_fs.do_not_format = true,   /* never format the user's card */
    };
    err = tinyusb_msc_new_storage_sdmmc(&storage_cfg, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MSC storage: %s", esp_err_to_name(err));
        return err;
    }

    /* From here the console is gone (the PHY belongs to USB-OTG). */
    ESP_LOGI(TAG, "starting: the USB-Serial/JTAG console stops until the restart");
    vTaskDelay(pdMS_TO_TICKS(50));   /* let the line above leave */
    const tinyusb_config_t usb_cfg = TINYUSB_DEFAULT_CONFIG();
    err = tinyusb_driver_install(&usb_cfg);
    if (err != ESP_OK) return err;
    s_usb_up = true;
    return ESP_OK;
}

usb_disk_state_t usb_disk_state(void)
{
    if (!s_usb_up) return USB_DISK_OFF;
    if (tud_mounted()) {
        s_host_seen = true;
        return USB_DISK_CONNECTED;
    }
    return s_host_seen ? USB_DISK_DISCONNECTED : USB_DISK_WAITING;
}

/* Gives the internal PHY back to USB-Serial/JTAG (the efuse default) and
 * re-enables its pads. The RTC_CNTL selection survives a software restart,
 * so without this the console would stay dead after it. Same register
 * sequence as crosspoint-reader's UsbSerialJtagHandoff.cpp (same chip). */
static void phy_to_serial_jtag(void)
{
    CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG,
                        RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE);
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL);
    SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
}

void usb_disk_finish_and_restart(void)
{
    if (s_usb_up) {
        /* Drop the D+ pull-up first so the PC releases the disk, and let any
         * MSC transfer in flight finish before the stack goes away. */
        tud_disconnect();
        for (int i = 0; i < 50 && tud_mounted(); i++) vTaskDelay(pdMS_TO_TICKS(10));
        vTaskDelay(pdMS_TO_TICKS(100));
        tinyusb_driver_uninstall();
    }
    phy_to_serial_jtag();
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_restart();
}
