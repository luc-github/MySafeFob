/*
 Project: MySafeFob  cmd_setting.c
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
 * @file cmd_setting.c
 * @brief MySafeFob — `setting` console command (F-20, ADR-018): list, read,
 *        write and reset any entry of the settings table
 *        (settings_defs.inc) from the serial console, without reflashing.
 *
 *   setting list
 *   setting get   <id>
 *   setting set   <id> <value>
 *   setting reset <id>
 *
 * <id> is the setting's name or NVS key, case-insensitive. Values: BOOL
 * accepts 0/1/on/off/true/false; U32/I32 accept a decimal number (or 0x
 * hex), negative for I32, with an optional duration suffix s/m/h/d
 * (multiplied into seconds: "5m" = 300). Secrets never live in this table
 * (secret_store owns them), so nothing here can expose one.
 */
#include "cmd_setting.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_console.h"
#include "settings_store.h"
#include "ui_nav.h"

static const char *kind_name(settings_kind_t kind)
{
    switch (kind) {
    case SETTINGS_KIND_BOOL: return "bool";
    case SETTINGS_KIND_I32:  return "i32";
    default:                 return "u32";
    }
}

static void format_value(settings_kind_t kind, uint32_t raw, char *buf, size_t len)
{
    switch (kind) {
    case SETTINGS_KIND_BOOL: snprintf(buf, len, "%s", raw ? "on" : "off"); break;
    case SETTINGS_KIND_I32:  snprintf(buf, len, "%" PRId32, (int32_t)raw); break;
    default:                 snprintf(buf, len, "%" PRIu32, raw); break;
    }
}

/* Parses `text` for a setting of `kind` into its raw 32-bit pattern. */
static bool parse_value(settings_kind_t kind, const char *text, uint32_t *out)
{
    if (kind == SETTINGS_KIND_BOOL) {
        if (!strcmp(text, "1") || !strcasecmp(text, "on") || !strcasecmp(text, "true")) {
            *out = 1;
            return true;
        }
        if (!strcmp(text, "0") || !strcasecmp(text, "off") || !strcasecmp(text, "false")) {
            *out = 0;
            return true;
        }
        return false;
    }

    const char *digits = (text[0] == '-' || text[0] == '+') ? text + 1 : text;
    int base = (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) ? 16 : 10;
    char *end = NULL;
    errno = 0;
    long long v = strtoll(text, &end, base);
    if (errno != 0 || end == text) return false;

    long long mult = 1;
    if (*end != '\0') {
        switch (tolower((unsigned char)*end)) {
        case 's': mult = 1; break;
        case 'm': mult = 60; break;
        case 'h': mult = 3600; break;
        case 'd': mult = 86400; break;
        default:  return false;
        }
        if (end[1] != '\0') return false;
    }
    v *= mult;

    if (kind == SETTINGS_KIND_I32) {
        if (v < INT32_MIN || v > INT32_MAX) return false;
        *out = (uint32_t)(int32_t)v;
    } else {
        if (v < 0 || v > (long long)UINT32_MAX) return false;
        *out = (uint32_t)v;
    }
    return true;
}

static void print_setting(int index)
{
    settings_info_t info;
    if (!settings_store_describe(index, &info)) return;
    char value[16];
    char def[16];
    format_value(info.kind, settings_store_get_raw(index), value, sizeof(value));
    format_value(info.kind, info.default_value, def, sizeof(def));
    printf("%-20s %-15s %-4s %12s  (default %s)\n", info.name, info.nvs_key, kind_name(info.kind), value, def);
}

static int find_or_complain(const char *name)
{
    int index = settings_store_find(name);
    if (index < 0) printf("Unknown setting '%s' (see 'setting list').\n", name);
    return index;
}

static int usage(void)
{
    printf("Usage: setting list | get <id> | set <id> <value> | reset <id>\n");
    return 1;
}

static int cmd_setting(int argc, char **argv)
{
    board_activity_notify();
    if (argc < 2) return usage();
    const char *sub = argv[1];

    if (!strcmp(sub, "list") && argc == 2) {
        int count = settings_store_count();
        for (int i = 0; i < count; i++) print_setting(i);
        return 0;
    }
    if (!strcmp(sub, "get") && argc == 3) {
        int index = find_or_complain(argv[2]);
        if (index < 0) return 1;
        print_setting(index);
        return 0;
    }
    if (!strcmp(sub, "set") && argc == 4) {
        int index = find_or_complain(argv[2]);
        if (index < 0) return 1;
        settings_info_t info;
        settings_store_describe(index, &info);
        uint32_t raw;
        if (!parse_value(info.kind, argv[3], &raw)) {
            printf("Invalid %s value '%s'.\n", kind_name(info.kind), argv[3]);
            return 1;
        }
        esp_err_t err = settings_store_set_raw(index, raw);
        if (err != ESP_OK) {
            printf("Write failed (%s).\n", esp_err_to_name(err));
            return 1;
        }
        print_setting(index);
        return 0;
    }
    if (!strcmp(sub, "reset") && argc == 3) {
        int index = find_or_complain(argv[2]);
        if (index < 0) return 1;
        esp_err_t err = settings_store_reset(index);
        if (err != ESP_OK) {
            printf("Reset failed (%s).\n", esp_err_to_name(err));
            return 1;
        }
        print_setting(index);
        return 0;
    }
    return usage();
}

esp_err_t cmd_setting_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "setting",
        .help = "Settings table: list | get <id> | set <id> <value> | reset <id>. "
                "Values: bool on/off, numbers with optional s/m/h/d suffix (5m = 300). "
                "Applied at the setting's next read (some only at boot).",
        .func = &cmd_setting,
    };
    return esp_console_cmd_register(&cmd);
}
