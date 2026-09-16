# MySafeFob power_mgr — Phase 8
# Deep-sleep/wake contract (ADR-009). Board deinit (e-ink POF, rails)
# lands with the board drivers (task 8c); flow validation is already
# possible: wake on Power (GPIO3, EXT1) + bootloader rescue on Right (GPIO7).
idf_component_register(SRCS "power_mgr.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_hw_support log spi_flash)
