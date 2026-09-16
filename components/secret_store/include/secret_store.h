/**
 * @file secret_store.h
 * @brief MySafeFob — encrypted keystore (contract, ADR-002 / ADR-004 / ADR-005).
 *
 * Principle (VALIDATED, do not reopen):
 *  - Application blob encrypted in the "secrets" partition (no filesystem,
 *    no encrypted partition — ADR-002). Encryption/decryption of the whole
 *    blob in RAM on every mutation.
 *  - Key = Argon2id(PIN, salt, m=8 MiB, t=4, p=1) — ADR-004. Full
 *    AES-256-GCM envelope: magic "MSF1" | version | entry_count | salt(16) | nonce(12)
 *    | ciphertext(internal plaintext "MSFS") | tag(16).
 *  - SD export: "MSFEX1" format with a dedicated passphrase — ADR-005.
 *  - The internal plaintext ("MSFS") NEVER leaves this component; getters
 *    copy into buffers provided by the caller.
 *
 * STATUS: contract frozen; implementation = stub (ESP_ERR_NOT_SUPPORTED).
 * First implementation: task 8.2, after Argon2id calibration on S3.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Max number of keystore entries (F-04). */
#define MSF_STORE_MAX_ENTRIES   64

/** Max length of a service label. */
#define MSF_STORE_LABEL_MAX     32

/** Max length of a decoded Base32 secret. */
#define MSF_STORE_SECRET_MAX    64

/** Keystore entry types. */
typedef enum {
    MSF_ENTRY_TOTP = 0,     /**< TOTP secret (Base32) + digits + period. */
    MSF_ENTRY_PASSWORD = 1, /**< Password / note (single field). */
    MSF_ENTRY_RECOVERY = 2, /**< Recovery codes (multi-line). */
} msf_entry_type_t;

/** Plaintext entry — transfer structure, never persisted as-is. */
typedef struct {
    msf_entry_type_t type;
    char label[MSF_STORE_LABEL_MAX];        /**< Service name. */
    uint8_t secret[MSF_STORE_SECRET_MAX];   /**< Secret or payload. */
    size_t secret_len;
    uint8_t digits;     /**< TOTP: 6 or 8. */
    uint8_t period;     /**< TOTP: seconds (default 30). */
} msf_store_entry_t;

/**
 * @brief Initializes the component (detects an existing store on the partition).
 * @return ESP_OK if a store exists, ESP_ERR_NOT_FOUND if blank,
 *         or a partition access error.
 */
esp_err_t msf_store_init(void);

/**
 * @brief Creates a new store with the given PIN (blank device).
 * Derives the key (Argon2id), generates the salt, writes the empty encrypted blob.
 */
esp_err_t msf_store_create(const char *pin, size_t pin_len);

/**
 * @brief Unlocks the store (derivation + decryption + GCM verification).
 * All read APIs require an unlocked store.
 */
esp_err_t msf_store_unlock(const char *pin, size_t pin_len);

/** @brief Locks: purges the internal plaintext from RAM. */
void msf_store_lock(void);

/** @brief True if the store is unlocked. */
bool msf_store_is_unlocked(void);

/** @brief Number of entries. */
esp_err_t msf_store_count(size_t *out_count);

/** @brief Lists the labels (index 0..count-1). */
esp_err_t msf_store_list(char labels[][MSF_STORE_LABEL_MAX], size_t max,
                         size_t *out_count);

/** @brief Reads one entry by index. */
esp_err_t msf_store_get(size_t index, msf_store_entry_t *out_entry);

/** @brief Adds an entry and rewrites the encrypted blob. */
esp_err_t msf_store_add(const msf_store_entry_t *entry);

/** @brief Removes an entry by index. */
esp_err_t msf_store_remove(size_t index);

/**
 * @brief Changes the PIN: re-derives the key and rewrites the blob.
 * Requires an unlocked store.
 */
esp_err_t msf_store_change_pin(const char *new_pin, size_t new_pin_len);

/**
 * @brief Exports the blob in "MSFEX1" format (dedicated passphrase) — ADR-005.
 * Atomic write to SD: temp + rename, never an overwrite.
 * @param sd_path Full path of the output file.
 */
esp_err_t msf_store_export_sd(const char *sd_path, const char *passphrase,
                              size_t passphrase_len);

/**
 * @brief Imports an "MSFEX1" export. Parses + verifies the tag BEFORE touching
 * the current keystore (ADR-005). Replaces the current content if validated.
 */
esp_err_t msf_store_import_sd(const char *sd_path, const char *passphrase,
                              size_t passphrase_len);

#ifdef __cplusplus
}
#endif
