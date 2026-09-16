# MySafeFob Factory app — X4 Pro recovery (homemade 1 bpp gfx + FreeInkUI
# splash, frozen copy see components/freeinkui/, ADR-010 amended)
idf_component_register(SRCS
                           "main.c"
                           "eink.c"
                           "gfx.c"
                           "font8x16.c"
                           "touch.c"
                           "buttons.c"
                           "sdcard.c"
                           "splash.cpp"
                           "i2c_bus.c"
                           "battery.c"
                           "battery_icon.cpp"
                           "rtc.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_timer spi_flash esp_partition
                                app_update sdmmc fatfs freeinkui)

# BUGFIX: ENABLE_FACTORY_DEBUG_LOG (factory/CMakeLists.txt) was only wired
# for the bootloader component (see bootloader_components/custom_bootloader/
# CMakeLists.txt) -> FACTORY_LOG_LEVEL was always 0 here, so ALL
# FACTORY_LOGD(...) calls in main.c compiled to a no-op regardless of the option.
if(ENABLE_FACTORY_DEBUG_LOG)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FACTORY_LOG_LEVEL=1)
    message(STATUS "Factory app debug log: ON")
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FACTORY_LOG_LEVEL=0)
    message(STATUS "Factory app debug log: OFF")
endif()
