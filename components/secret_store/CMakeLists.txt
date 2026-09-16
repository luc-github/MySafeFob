# MySafeFob secret_store — stub Phase 8
# Le portage Argon2id (libsodium/monocypher) et le chiffrement AES-256-GCM
# du blob arrivent dans la tâche 8.2 (ADR-002 / ADR-004).
idf_component_register(SRCS "secret_store.c"
                       INCLUDE_DIRS "include"
                       REQUIRES mbedtls)
