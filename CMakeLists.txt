# MySafeFob — root CMakeLists
# Board selection: cmake -DMSF_BOARD=x4pro (default) or -DMSF_BOARD=m5paper_mono
cmake_minimum_required(VERSION 3.16)

set(MSF_BOARD "x4pro" CACHE STRING "Target board (see boards/)")

# Board config: IDF_TARGET + the board's additional sdkconfig.
# MUST be included before project.cmake (SDKCONFIG_DEFAULTS is set there).
include(${CMAKE_CURRENT_SOURCE_DIR}/boards/${MSF_BOARD}/board_config.cmake)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mysafefob)

# Board name exposed to app code (string literal).
# add_compile_definitions() after project() does NOT propagate to IDF
# components — idf_build_set_property is required (observed at the 5.5.5 build).
idf_build_set_property(COMPILE_DEFINITIONS "MSF_BOARD_NAME=\"${MSF_BOARD}\"" APPEND)

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
