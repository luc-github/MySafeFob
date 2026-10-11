# MySafeFob secret_store — encrypted account store (F-04, ADR-002/004/019):
# AES-256-GCM (mbedtls), key = Argon2id(PIN) (Monocypher), raw `secrets`
# partition with two alternating slots.
idf_component_register(SRCS "secret_store.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_partition
                       PRIV_REQUIRES mbedtls monocypher bootloader_support esp_timer esp_hw_support spi_flash)
