# X4 Pro — board configuration (MySafeFob board-agnostic layer, ADR-008)
# Included by the root CMakeLists BEFORE project.cmake.
# Must define: IDF_TARGET and SDKCONFIG_DEFAULTS (cascading sdkconfig
# files: common, then board).

set(IDF_TARGET "esp32s3")

set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults"
    "${CMAKE_CURRENT_SOURCE_DIR}/boards/x4pro/sdkconfig.defaults")

# Board component (splash + e-ink sleep screen, Phase 8c slice 1).
# The full BSP (touch/RTC/gauge/UI) extended this folder in 8.4.
# EXTRA_COMPONENT_DIRS must be set before project() — this file is
# included before project.cmake by the root CMakeLists.
#
# "boards/x4pro/app" IS itself a component (its CMakeLists.txt calls
# idf_component_register directly) -- LVGL is pulled in as a managed
# dependency via boards/x4pro/app/idf_component.yml instead of a vendored
# sub-component, so no second EXTRA_COMPONENT_DIRS entry is needed here
# anymore (it used to point at components/freeinkui/, removed when LVGL
# replaced FreeInkUI — ADR-010 amended).
list(APPEND EXTRA_COMPONENT_DIRS
     "${CMAKE_CURRENT_SOURCE_DIR}/boards/x4pro/app")
