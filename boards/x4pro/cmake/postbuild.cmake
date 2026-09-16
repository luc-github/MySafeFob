# ---------------------------------------------------------------------------
# MySafeFob — postbuild APP: feeds installer/<board>_app/.
#
# Copies after every app build:
#   firmware_16MB_mysafefob.bin  the app (app0 slot when flashed)
#   bootloader_16MB.bin          the one from the FACTORY build (recovery
#                                hook) — the app build's own bootloader has
#                                NO hook
#   partitions_16mb.bin          the board's table (boards/<board>/partitions.csv)
#   factory_16MB.bin             factory from the factory build (if present)
#   ota_data_initial_16MB.bin    generated (seq=1 -> boots straight into app0,
#                                not factory)
#
# The installer/<variant>/ folder + its JSON flash map are consumed by
# tools/flash_scripts/flash_mgr.py and, later, a web installer.
#
# Factory artifacts go through copy_if_exists.cmake (script mode): the
# factory build is not required to compile the app — a missing artifact
# produces a warning, not an error. build_mgr always builds the factory
# variants FIRST, so in practice they're already there.
# ---------------------------------------------------------------------------

set(MSF_INSTALLER_DIR "${CMAKE_SOURCE_DIR}/installer/${MSF_BOARD}_app")
set(MSF_FACTORY_BUILD "${CMAKE_SOURCE_DIR}/boards/${MSF_BOARD}/factory/build")
set(MSF_GEN_OTA "${CMAKE_SOURCE_DIR}/tools/build_scripts/gen_ota_initial.py")
set(MSF_COPY_IF_EXISTS "${CMAKE_SOURCE_DIR}/boards/${MSF_BOARD}/cmake/copy_if_exists.cmake")

add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${MSF_INSTALLER_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MSF_INSTALLER_DIR}"
    # Firmware (the app itself)
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin"
        "${MSF_INSTALLER_DIR}/firmware_16MB_mysafefob.bin"
    # otadata -> boots straight into app0
    COMMAND ${PYTHON} "${MSF_GEN_OTA}"
        --output "${MSF_INSTALLER_DIR}/ota_data_initial_16MB.bin"
    # Factory artifacts (bootloader WITH hook, table, factory bin)
    COMMAND ${CMAKE_COMMAND}
        -D "SRC_DIR=${MSF_FACTORY_BUILD}"
        -D "DST_DIR=${MSF_INSTALLER_DIR}"
        -D "BOARD=${MSF_BOARD}"
        -P "${MSF_COPY_IF_EXISTS}"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "============================================"
    COMMAND ${CMAKE_COMMAND} -E echo "  Installer ready: ${MSF_INSTALLER_DIR}"
    COMMAND ${CMAKE_COMMAND} -E echo "    - firmware_16MB_mysafefob.bin"
    COMMAND ${CMAKE_COMMAND} -E echo "    - ota_data_initial_16MB.bin (boot app0)"
    COMMAND ${CMAKE_COMMAND} -E echo "    + factory artifacts (bootloader hook,"
    COMMAND ${CMAKE_COMMAND} -E echo "      partitions, factory) if a factory build is present"
    COMMAND ${CMAKE_COMMAND} -E echo "============================================"
    VERBATIM
)
