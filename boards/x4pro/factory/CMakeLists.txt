# MySafeFob Factory — recovery app (standalone sub-project, ADR-007)
cmake_minimum_required(VERSION 3.16)

# Verbose factory/bootloader debug logs (independent of the sdkconfig log level).
# MUST stay in sync with bootloader_components/custom_bootloader/CMakeLists.txt
# DIAG 2026-09-14: ON while bringing up the factory (touch/e-ink).
# Set back to OFF for release (the factory must stay lean and reliable).
option(ENABLE_FACTORY_DEBUG_LOG "Enable Factory app and bootloader debug logs" ON)

# The app's encrypted store, for the rescue export (rescue_export.c): only
# these two shared components, not the whole components/ folder.
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../../../components/secret_store
    ${CMAKE_CURRENT_LIST_DIR}/../../../components/monocypher)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mysafefob-factory)

# Postbuild: feeds installer/x4pro_factory/ (flash_mgr + web installer).
include(cmake/postbuild.cmake)
