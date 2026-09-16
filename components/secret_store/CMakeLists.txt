# MySafeFob secret_store — Phase 8 stub
# The Argon2id port (libsodium/monocypher) and AES-256-GCM encryption
# of the blob arrive in task 8.2 (ADR-002 / ADR-004).
idf_component_register(SRCS "secret_store.c"
                       INCLUDE_DIRS "include"
                       REQUIRES mbedtls)
