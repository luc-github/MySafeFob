# MySafeFob Factory app — recovery X4 Pro (gfx 1 bpp maison + splash
# FreeInkUI, copie figee cf. components/freeinkui/, ADR-010 amende)
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

# BUGFIX : ENABLE_FACTORY_DEBUG_LOG (factory/CMakeLists.txt) n'etait cable
# que pour le composant bootloader (cf. bootloader_components/custom_bootloader/
# CMakeLists.txt) -> FACTORY_LOG_LEVEL valait toujours 0 ici, donc TOUS les
# FACTORY_LOGD(...) de main.c etaient compiles en no-op quel que soit l'option.
if(ENABLE_FACTORY_DEBUG_LOG)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FACTORY_LOG_LEVEL=1)
    message(STATUS "Factory app debug log: ON")
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FACTORY_LOG_LEVEL=0)
    message(STATUS "Factory app debug log: OFF")
endif()
