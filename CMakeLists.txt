# MySafeFob — root CMakeLists
# Board selection: cmake -DMSF_BOARD=x4pro (default) or -DMSF_BOARD=m5paper_mono
cmake_minimum_required(VERSION 3.16)

set(MSF_BOARD "x4pro" CACHE STRING "Target board (see boards/)")

# Board config: IDF_TARGET + the board's additional sdkconfig.
# MUST be included before project.cmake (SDKCONFIG_DEFAULTS is set there).
include(${CMAKE_CURRENT_SOURCE_DIR}/boards/${MSF_BOARD}/board_config.cmake)

# esp_lcd is not used (the e-ink panel has its own SPI driver, eink.c; LVGL
# does not require it), and GCC 14.2 intermittently crashes on its
# rgb/esp_lcd_panel_rgb.c ("internal compiler error" in the ira pass, known
# issue). ESP-IDF builds every component by default: exclude it. If
# something ever needs esp_lcd, the build fails clearly at configure time.
set(EXCLUDE_COMPONENTS esp_lcd)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mysafefob)

# Board name exposed to app code (string literal).
# add_compile_definitions() after project() does NOT propagate to IDF
# components — idf_build_set_property is required (observed at the 5.5.5 build).
idf_build_set_property(COMPILE_DEFINITIONS "MSF_BOARD_NAME=\"${MSF_BOARD}\"" APPEND)

# LVGL fonts: the app's own msf_font_24 / msf_font_32 (boards/x4pro/app/fonts,
# tools/gen_fonts.py) instead of the built-in Montserrat 24/32 (disabled in
# sdkconfig). Declared for every component (LVGL's theme uses the default
# font); Kconfig's default-font choice cannot name a custom font.
idf_build_set_property(COMPILE_DEFINITIONS "LV_FONT_DEFAULT=&msf_font_24" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS
    "LV_FONT_CUSTOM_DECLARE=LV_FONT_DECLARE(msf_font_24) LV_FONT_DECLARE(msf_font_32)" APPEND)

# Postbuild: feeds installer/<board>_app/ (flash_mgr + web installer).
include(boards/${MSF_BOARD}/cmake/postbuild.cmake)

# esp3d_log app log level (0=none 1=error 2=warning 3=debug 4=all)
# Dev: 3. Release: 2 (warnings+errors only).
# BUGFIX 2026-09-16: add_compile_options() here (after project()) does NOT
# propagate to IDF components — same trap as MSF_BOARD_NAME above,
# unapplied to this line until now. Consequence discovered after the fact:
# ESP3D_LOG was never defined for any component -> ALL esp3d_log()/
# esp3d_log_d()/esp3d_log_w()/esp3d_log_e() calls in the project (including
# those in main.c, the boot banner included) compiled to a silent no-op,
# regardless of MSF_LOG_LEVEL. idf_build_set_property() is the mechanism
# that actually works (already used just above).
set(MSF_LOG_LEVEL 3 CACHE STRING "esp3d_log level (0-4)")
idf_build_set_property(COMPILE_DEFINITIONS "ESP3D_LOG=${MSF_LOG_LEVEL}" APPEND)
