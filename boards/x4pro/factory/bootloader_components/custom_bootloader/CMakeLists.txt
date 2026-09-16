# Custom bootloader hooks (MySafeFob, ADR-007) — port of PiBot's.
# The bootloader is a separate sub-build: keep ENABLE_FACTORY_DEBUG_LOG
# in sync with the factory sub-project's root CMakeLists.txt.
# TEMPORARY (2026-09-16 diagnostic, ADR-009): ON to see the full hook
# trace (HOOK_LOGI) during Power >= 10s threshold testing.
# Set back to OFF once the mechanism is reconfirmed working on hardware.
option(ENABLE_FACTORY_DEBUG_LOG "Enable bootloader debug logs" ON)

idf_component_register(SRCS "hooks.c"
                       PRIV_REQUIRES hal bootloader_support soc)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u bootloader_hooks_include")

if(ENABLE_FACTORY_DEBUG_LOG)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FACTORY_LOG_LEVEL=1)
    message(STATUS "Bootloader debug log: ON")
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FACTORY_LOG_LEVEL=0)
    message(STATUS "Bootloader debug log: OFF")
endif()
