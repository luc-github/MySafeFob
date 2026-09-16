/**
 * @file secret_store.h
 * @brief MySafeFob — keystore chiffré (contrat, ADR-002 / ADR-004 / ADR-005).
 *
 * Principe (VALIDÉ, ne pas rouvrir) :
 *  - Blob applicatif chiffré dans la partition "secrets" (pas de filesystem,
 *    pas de partition chiffrée — ADR-002). Chiffrement/déchiffrement du blob
 *    entier en RAM à chaque mutation.
 *  - Clé = Argon2id(PIN, sel, m=8 Mo, t=4, p=1) — ADR-004. Enveloppe complète
 *    AES-256-GCM : magic "MSF1" | version | entry_count | salt(16) | nonce(12)
 *    | ciphertext(clair interne "MSFS") | tag(16).
 *  - Export SD : format "MSFEX1" avec passphrase dédiée — ADR-005.
 *  - Le clair interne ("MSFS") ne sort JAMAIS de ce composant ; les getters
 *    copient dans des buffers fournis par l'appelant.
 *
 * STATUT : contrat figé ; implémentation = stub (ESP_ERR_NOT_SUPPORTED).
 * Première implémentation : tâche 8.2, après calibration Argon2id sur S3.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Nombre max d'entrées du keystore (F-04). */
#define MSF_STORE_MAX_ENTRIES   64

/** Longueur max d'un label de service. */
#define MSF_STORE_LABEL_MAX     32

/** Longueur max d'un secret Base32 décodé. */
#define MSF_STORE_SECRET_MAX    64

/** Types d'entrée du keystore. */
typedef enum {
    MSF_ENTRY_TOTP = 0,     /**< Secret TOTP (Base32) + digits + period. */
    MSF_ENTRY_PASSWORD = 1, /**< Mot de passe / note (champ unique). */
    MSF_ENTRY_RECOVERY = 2, /**< Codes de recovery (multi-lignes). */
} msf_entry_type_t;

/** Entrée en clair — structure de transfert, jamais persistée telle quelle. */
typedef struct {
    msf_entry_type_t type;
    char label[MSF_STORE_LABEL_MAX];        /**< Nom du service. */
    uint8_t secret[MSF_STORE_SECRET_MAX];   /**< Secret ou payload. */
    size_t secret_len;
    uint8_t digits;     /**< TOTP : 6 ou 8. */
    uint8_t period;     /**< TOTP : secondes (défaut 30). */
} msf_store_entry_t;

/**
 * @brief Initialise le composant (détecte un store existant sur la partition).
 * @return ESP_OK si un store existe, ESP_ERR_NOT_FOUND si vierge,
 *         ou erreur d'accès partition.
 */
esp_err_t msf_store_init(void);

/**
 * @brief Crée un nouveau store avec le PIN fourni (device vierge).
 * Dérive la clé (Argon2id), génère le sel, écrit le blob vide chiffré.
 */
esp_err_t msf_store_create(const char *pin, size_t pin_len);

/**
 * @brief Déverrouille le store (dérivation + déchiffrement + vérif GCM).
 * Toutes les APIs de lecture exigent un store déverrouillé.
 */
esp_err_t msf_store_unlock(const char *pin, size_t pin_len);

/** @brief Verrouille : purge le clair interne de la RAM. */
void msf_store_lock(void);

/** @brief Vrai si le store est déverrouillé. */
bool msf_store_is_unlocked(void);

/** @brief Nombre d'entrées. */
esp_err_t msf_store_count(size_t *out_count);

/** @brief Liste les labels (index 0..count-1). */
esp_err_t msf_store_list(char labels[][MSF_STORE_LABEL_MAX], size_t max,
                         size_t *out_count);

/** @brief Lecture d'une entrée par index. */
esp_err_t msf_store_get(size_t index, msf_store_entry_t *out_entry);

/** @brief Ajoute une entrée et ré-écrit le blob chiffré. */
esp_err_t msf_store_add(const msf_store_entry_t *entry);

/** @brief Supprime une entrée par index. */
esp_err_t msf_store_remove(size_t index);

/**
 * @brief Change le PIN : re-dérive la clé et ré-écrit le blob.
 * Exige un store déverrouillé.
 */
esp_err_t msf_store_change_pin(const char *new_pin, size_t new_pin_len);

/**
 * @brief Exporte le blob au format "MSFEX1" (passphrase dédiée) — ADR-005.
 * Écriture atomique sur SD : temp + rename, jamais d'écrasement.
 * @param sd_path Chemin complet du fichier de sortie.
 */
esp_err_t msf_store_export_sd(const char *sd_path, const char *passphrase,
                              size_t passphrase_len);

/**
 * @brief Importe un export "MSFEX1". Parse + vérif tag AVANT de toucher au
 * keystore courant (ADR-005). Remplace le contenu courant si validé.
 */
esp_err_t msf_store_import_sd(const char *sd_path, const char *passphrase,
                              size_t passphrase_len);

#ifdef __cplusplus
}
#endif
