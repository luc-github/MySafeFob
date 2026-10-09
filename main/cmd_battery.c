/*
 Project: MySafeFob  cmd_battery.c
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
 * @file cmd_battery.c
 * @brief MySafeFob — `battery` console command (F-20): shows the CW2017
 *        gauge state (SoC, cell voltage, charge pin, engine version/mode,
 *        BATINFO profile), and can reload the profile.
 *
 *   battery          read-only status
 *   battery reload   upload the BATINFO profile if missing, restart the gauge
 *   battery force    upload it even if it already matches, restart the gauge
 *   battery log      gauge events since boot (in RAM, ms timestamps)
 */
#include "cmd_battery.h"

#include <stdio.h>
#include <string.h>

#include "battery.h"
#include "esp_console.h"
#include "ui_nav.h"

static void print_status(void)
{
    battery_status_t st;
    if (!battery_get_status(&st)) {
        printf("Gauge not responding on I2C (charging pin: %s).\n", st.charging ? "high" : "low");
        return;
    }
    printf("SoC       %u%%\n", st.soc);
    printf("VCELL     %u mV\n", st.vcell_mv);
    printf("Charging  %s (GPIO21)\n", st.charging ? "yes" : "no");
    printf("Version   0x%02X (%s)\n", st.version, (st.version & 0xFD) == 0x0D ? "running" : "not running");
    printf("Mode      0x%02X (%s)\n", st.mode, st.mode == 0x00 ? "normal" : "not normal");
    printf("Profile   %s, update flag %s\n", st.profile_match ? "loaded" : "MISSING",
           st.update_flag ? "set" : "clear");
    printf("I2C       %lu failed read attempts since boot (retried)\n", (unsigned long)st.i2c_errors);
    if (!st.profile_match) printf("SoC reads 0%% without the profile: try 'battery reload'.\n");
}

static int cmd_battery(int argc, char **argv)
{
    board_activity_notify();
    if (argc == 1) {
        print_status();
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "log")) {
        battery_print_log();
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "reload") || !strcmp(argv[1], "force"))) {
        bool force = !strcmp(argv[1], "force");
        printf("Loading profile and restarting the gauge (up to ~4 s)...\n");
        bool ok = battery_reload_profile(force);
        printf("%s\n", ok ? "Gauge ready." : "Gauge NOT ready.");
        print_status();
        return ok ? 0 : 1;
    }
    printf("Usage: battery [reload|force|log]\n");
    return 1;
}

esp_err_t cmd_battery_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "battery",
        .help = "Battery gauge status (SoC, voltage, charging, profile). "
                "'battery reload' uploads the gauge profile if missing, 'battery force' always, 'battery log' gauge events since boot.",
        .func = &cmd_battery,
    };
    return esp_console_cmd_register(&cmd);
}
