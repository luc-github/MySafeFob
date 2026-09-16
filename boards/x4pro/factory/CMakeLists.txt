# MySafeFob Factory — app de recovery (sous-projet autonome, ADR-007)
cmake_minimum_required(VERSION 3.16)

# Verbose debug logs factory/bootloader (indépendant du sdkconfig log level).
# DOIT rester en sync avec bootloader_components/custom_bootloader/CMakeLists.txt
# DIAG 2026-09-14 : ON le temps de la mise au point factory (touch/e-ink).
# Repasser a OFF en release (la factory doit rester sobre et fiable).
option(ENABLE_FACTORY_DEBUG_LOG "Enable Factory app and bootloader debug logs" ON)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mysafefob-factory)

# Postbuild : alimente installer/x4pro_factory/ (flash_mgr + web installer).
include(cmake/postbuild.cmake)
