/*
 Project: MySafeFob  backup.c
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
 * @file backup.c
 * @brief MySafeFob App — SD backups, see backup.h.
 */
#include "backup.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdcard.h"
#include "settings_store.h"
#include "time_service.h"

#include "app_log_workaround.h"

static const char *TAG = "backup";

#define BACKUP_EXT ".msfx"
#define MAX_FILE_SIZE (640 * 1024)   /* above 250 full records (~275 KB); refuses anything odd */

/* "MSF-YYYYMMDD-HHMM" in local time, or "MSF-NODATE" if the clock is not set. */
static void base_name(char *out, size_t size, int64_t utc)
{
    if (utc <= 0) {
        snprintf(out, size, "MSF-NODATE");
        return;
    }
    time_service_dt_t dt;
    time_service_epoch_to_dt((time_t)(utc + (int64_t)settings_store_get_time_tz_offset_min() * 60), &dt);
    snprintf(out, size, "MSF-%04d%02d%02d-%02d%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute);
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* Writes `data` to `path` through a temporary file renamed at the end, so a
 * pulled card or a dead battery never leaves a truncated .msfx behind. */
static esp_err_t write_file(const char *path, const uint8_t *data, size_t len)
{
    char tmp[64];
    snprintf(tmp, sizeof(tmp), BACKUP_DIR "/MSF.TMP");
    unlink(tmp);
    FILE *f = fopen(tmp, "wb");
    if (!f) return ESP_FAIL;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = (fflush(f) == 0) && ok;
    ok = (fsync(fileno(f)) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    if (ok) ok = rename(tmp, path) == 0;
    if (!ok) unlink(tmp);
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t backup_export(const char *passphrase, char *name_out)
{
    name_out[0] = '\0';
    if (!accounts_is_unlocked()) return ESP_ERR_INVALID_STATE;
    size_t pass_len = passphrase ? strlen(passphrase) : 0;
    if (pass_len < MSF_BACKUP_PASS_MIN || pass_len > MSF_BACKUP_PASS_MAX) return ESP_ERR_INVALID_ARG;

    account_t *recs = heap_caps_calloc(MSF_STORE_MAX_ACCOUNTS, sizeof(account_t), MALLOC_CAP_SPIRAM);
    if (!recs) return ESP_ERR_NO_MEM;
    int n = accounts_export_all(recs, MSF_STORE_MAX_ACCOUNTS);
    int64_t now = time_service_is_valid() ? (int64_t)time_service_get_utc() : 0;

    uint8_t *file = NULL;
    size_t len = 0;
    esp_err_t err = msf_backup_seal(passphrase, recs, n, now, &file, &len);
    memset(recs, 0, MSF_STORE_MAX_ACCOUNTS * sizeof(account_t));
    heap_caps_free(recs);
    if (err != ESP_OK) return err;

    err = sdcard_mount();
    if (err != ESP_OK) {
        msf_backup_free(file);
        return ESP_ERR_NOT_FOUND;
    }
    mkdir(BACKUP_DIR, 0777);   /* EEXIST is fine */

    /* Never overwrite: a second export in the same minute gets "-2"... */
    char base[24], path[64];
    base_name(base, sizeof(base), now);
    err = ESP_FAIL;
    for (int k = 1; k <= 9; k++) {
        if (k == 1) snprintf(name_out, BACKUP_NAME_MAX, "%s" BACKUP_EXT, base);
        else snprintf(name_out, BACKUP_NAME_MAX, "%s-%d" BACKUP_EXT, base, k);
        snprintf(path, sizeof(path), BACKUP_DIR "/%s", name_out);
        if (!file_exists(path)) {
            err = write_file(path, file, len);
            break;
        }
    }
    sdcard_unmount();
    msf_backup_free(file);   /* ciphertext only, nothing to wipe */
    if (err == ESP_OK) ESP_LOGI(TAG, "exported %d account(s) to %s", n, name_out);
    else name_out[0] = '\0';
    return err;
}

/* Reads a whole backup file into PSRAM (free with heap_caps_free). */
static esp_err_t read_file(const char *path, uint8_t **data, size_t *len)
{
    struct stat st;
    if (stat(path, &st) != 0) return ESP_ERR_NOT_FOUND;
    if (st.st_size <= 0 || st.st_size > MAX_FILE_SIZE) return ESP_ERR_INVALID_SIZE;
    uint8_t *buf = heap_caps_malloc((size_t)st.st_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) return ESP_ERR_NO_MEM;
    FILE *f = fopen(path, "rb");
    bool ok = f && fread(buf, 1, (size_t)st.st_size, f) == (size_t)st.st_size;
    if (f) fclose(f);
    if (!ok) {
        heap_caps_free(buf);
        return ESP_FAIL;
    }
    *data = buf;
    *len = (size_t)st.st_size;
    return ESP_OK;
}

static bool has_ext(const char *name)
{
    size_t n = strlen(name), e = strlen(BACKUP_EXT);
    return n > e && strcasecmp(name + n - e, BACKUP_EXT) == 0;
}

static int cmp_newest_first(const void *a, const void *b)
{
    const backup_file_t *x = a, *y = b;
    if (x->created != y->created) return x->created < y->created ? 1 : -1;
    return strcmp(y->name, x->name);
}

int backup_list(backup_file_t *out, int max)
{
    if (sdcard_mount() != ESP_OK) return -1;
    int n = 0;
    DIR *dir = opendir(BACKUP_DIR);
    if (dir) {
        struct dirent *e;
        while (n < max && (e = readdir(dir)) != NULL) {
            if (e->d_type == DT_DIR || !has_ext(e->d_name) || strlen(e->d_name) >= BACKUP_NAME_MAX) continue;
            char path[sizeof(BACKUP_DIR) + 1 + sizeof(e->d_name)];
            snprintf(path, sizeof(path), BACKUP_DIR "/%s", e->d_name);
            /* Whole file (a few KiB): peek also checks the size matches the
             * header, so a truncated copy is not even listed. */
            msf_backup_info_t info;
            uint8_t *whole = NULL;
            size_t len = 0;
            if (read_file(path, &whole, &len) != ESP_OK) continue;
            esp_err_t err = msf_backup_peek(whole, len, &info);
            heap_caps_free(whole);
            if (err != ESP_OK) continue;
            strlcpy(out[n].name, e->d_name, sizeof(out[n].name));
            out[n].created = info.created;
            out[n].count = info.count;
            n++;
        }
        closedir(dir);
    }
    sdcard_unmount();
    qsort(out, n, sizeof(out[0]), cmp_newest_first);
    return n;
}

static bool name_has_suffix(const char *name, const char *suffix)
{
    size_t n = strlen(name), e = strlen(suffix);
    return n > e && strcasecmp(name + n - e, suffix) == 0;
}

/* A MySafeFob JSON starts with {"format": "mysafefob", ... (both tools). */
static bool is_our_json(const char *path)
{
    char head[256];
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t got = fread(head, 1, sizeof(head) - 1, f);
    fclose(f);
    head[got] = '\0';
    return strstr(head, "mysafefob") != NULL;
}

static void scan_dir(const char *dir, bool rescue, char (*paths)[BACKUP_PATH_MAX], int max, int *n)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while (*n < max && (e = readdir(d)) != NULL) {
        if (e->d_type == DT_DIR) continue;
        bool json = name_has_suffix(e->d_name, ".json");
        bool msr = rescue && name_has_suffix(e->d_name, ".msr") && strncasecmp(e->d_name, "RESCUE", 6) == 0;
        if (!json && !msr) continue;
        if (strlen(dir) + 1 + strlen(e->d_name) >= BACKUP_PATH_MAX) continue;
        strlcpy(paths[*n], dir, BACKUP_PATH_MAX);   /* length checked above */
        strlcat(paths[*n], "/", BACKUP_PATH_MAX);
        strlcat(paths[*n], e->d_name, BACKUP_PATH_MAX);
        if (json && !is_our_json(paths[*n])) continue;
        (*n)++;
    }
    closedir(d);
}

int backup_find_plaintext(char (*paths)[BACKUP_PATH_MAX], int max)
{
    if (sdcard_mount() != ESP_OK) return -1;
    int n = 0;
    scan_dir(SD_MOUNT_POINT, false, paths, max, &n);
    scan_dir(BACKUP_DIR, true, paths, max, &n);
    sdcard_unmount();
    return n;
}

int backup_wipe_files(char (*paths)[BACKUP_PATH_MAX], int count)
{
    if (sdcard_mount() != ESP_OK) return 0;
    static const uint8_t kZeros[512];
    int wiped = 0;
    for (int i = 0; i < count; i++) {
        struct stat st;
        if (stat(paths[i], &st) == 0) {
            FILE *f = fopen(paths[i], "r+b");
            if (f) {
                for (off_t done = 0; done < st.st_size; done += sizeof(kZeros)) {
                    size_t chunk = (size_t)(st.st_size - done) < sizeof(kZeros) ? (size_t)(st.st_size - done)
                                                                                  : sizeof(kZeros);
                    fwrite(kZeros, 1, chunk, f);
                }
                fflush(f);
                fsync(fileno(f));
                fclose(f);
            }
        }
        if (unlink(paths[i]) == 0) wiped++;
        else ESP_LOGW(TAG, "could not delete %s", paths[i]);
    }
    sdcard_unmount();
    ESP_LOGI(TAG, "SD check: %d plaintext file(s) wiped", wiped);
    return wiped;
}

esp_err_t backup_load(const char *name, const char *passphrase, account_t *out, int max, int *count)
{
    *count = 0;
    if (!name || strchr(name, '/') || !has_ext(name)) return ESP_ERR_INVALID_ARG;
    if (sdcard_mount() != ESP_OK) return ESP_ERR_NOT_FOUND;
    char path[64];
    snprintf(path, sizeof(path), BACKUP_DIR "/%s", name);
    uint8_t *file = NULL;
    size_t len = 0;
    esp_err_t err = read_file(path, &file, &len);
    sdcard_unmount();   /* the rest works on the RAM copy */
    if (err != ESP_OK) return err;
    err = msf_backup_open(passphrase, file, len, out, max, count, NULL);
    heap_caps_free(file);
    return err;
}
