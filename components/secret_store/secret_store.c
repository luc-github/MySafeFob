/**
 * @file secret_store.c
 * @brief MySafeFob — encrypted keystore: STUB Phase 8.
 *
 * All APIs return ESP_ERR_NOT_SUPPORTED until the implementation lands
 * in task 8.2 (Argon2id + AES-256-GCM of the blob, ADR-002/004/005).
 */
#include <string.h>
#include "secret_store.h"

#define STUB_ERR ESP_ERR_NOT_SUPPORTED

esp_err_t msf_store_init(void) { return STUB_ERR; }

esp_err_t msf_store_create(const char *pin, size_t pin_len)
{
    (void)pin; (void)pin_len;
    return STUB_ERR;
}

esp_err_t msf_store_unlock(const char *pin, size_t pin_len)
{
    (void)pin; (void)pin_len;
    return STUB_ERR;
}

void msf_store_lock(void) { }

bool msf_store_is_unlocked(void) { return false; }

esp_err_t msf_store_count(size_t *out_count)
{
    (void)out_count;
    return STUB_ERR;
}

esp_err_t msf_store_list(char labels[][MSF_STORE_LABEL_MAX], size_t max,
                         size_t *out_count)
{
    (void)labels; (void)max; (void)out_count;
    return STUB_ERR;
}

esp_err_t msf_store_get(size_t index, msf_store_entry_t *out_entry)
{
    (void)index; (void)out_entry;
    return STUB_ERR;
}

esp_err_t msf_store_add(const msf_store_entry_t *entry)
{
    (void)entry;
    return STUB_ERR;
}

esp_err_t msf_store_remove(size_t index)
{
    (void)index;
    return STUB_ERR;
}

esp_err_t msf_store_change_pin(const char *new_pin, size_t new_pin_len)
{
    (void)new_pin; (void)new_pin_len;
    return STUB_ERR;
}

esp_err_t msf_store_export_sd(const char *sd_path, const char *passphrase,
                              size_t passphrase_len)
{
    (void)sd_path; (void)passphrase; (void)passphrase_len;
    return STUB_ERR;
}

esp_err_t msf_store_import_sd(const char *sd_path, const char *passphrase,
                              size_t passphrase_len)
{
    (void)sd_path; (void)passphrase; (void)passphrase_len;
    return STUB_ERR;
}
