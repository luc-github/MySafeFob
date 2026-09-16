# Custom bootloader hooks (MySafeFob, ADR-007) — portage du PiBot.
# Le bootloader est un sous-build séparé : garder ENABLE_FACTORY_DEBUG_LOG
# en sync avec le CMakeLists.txt racine du sous-projet factory.
# TEMPORAIRE (diagnostic 2026-09-16, ADR-009) : ON pour voir la trace
# complete du hook (HOOK_LOGI) pendant les tests du seuil Power >= 10s.
# Remettre a OFF une fois le mecanisme reconfirme fonctionnel sur hardware.
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
