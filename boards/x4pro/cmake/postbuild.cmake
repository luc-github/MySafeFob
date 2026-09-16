# ---------------------------------------------------------------------------
# MySafeFob — postbuild APP : alimente installer/<board>_app/ (portage PiBot).
#
# Copie apres chaque build de l'app :
#   firmware_16MB_mysafefob.bin  l'app (slot app0 au flash)
#   bootloader_16MB.bin          celui du build FACTORY (hook recovery) — le
#                                bootloader du build app n'a PAS de hook
#   partitions_16mb.bin          table de la board (boards/<board>/partitions.csv)
#   factory_16MB.bin             factory depuis le build factory (si present)
#   ota_data_initial_16MB.bin    genere (seq=1 -> boot app0 direct, pas factory)
#
# Le dossier installer/<variant>/ + sa flash map JSON sont consommes par
# tools/flash_scripts/flash_mgr.py et, plus tard, un web installer.
#
# Les artefacts factory passent par copy_if_exists.cmake (mode script) :
# le build factory n'est pas obligatoire pour compiler l'app — un artefact
# absent produit un warning, pas une erreur. build_mgr construit toujours
# les variants factory en PREMIER, donc en pratique ils sont la.
# ---------------------------------------------------------------------------

set(MSF_INSTALLER_DIR "${CMAKE_SOURCE_DIR}/installer/${MSF_BOARD}_app")
set(MSF_FACTORY_BUILD "${CMAKE_SOURCE_DIR}/boards/${MSF_BOARD}/factory/build")
set(MSF_GEN_OTA "${CMAKE_SOURCE_DIR}/tools/build_scripts/gen_ota_initial.py")
set(MSF_COPY_IF_EXISTS "${CMAKE_SOURCE_DIR}/boards/${MSF_BOARD}/cmake/copy_if_exists.cmake")

add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${MSF_INSTALLER_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MSF_INSTALLER_DIR}"
    # Firmware (l'app elle-meme)
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin"
        "${MSF_INSTALLER_DIR}/firmware_16MB_mysafefob.bin"
    # otadata -> boot direct sur app0
    COMMAND ${PYTHON} "${MSF_GEN_OTA}"
        --output "${MSF_INSTALLER_DIR}/ota_data_initial_16MB.bin"
    # Artefacts factory (bootloader AVEC hook, table, factory bin)
    COMMAND ${CMAKE_COMMAND}
        -D "SRC_DIR=${MSF_FACTORY_BUILD}"
        -D "DST_DIR=${MSF_INSTALLER_DIR}"
        -D "BOARD=${MSF_BOARD}"
        -P "${MSF_COPY_IF_EXISTS}"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "============================================"
    COMMAND ${CMAKE_COMMAND} -E echo "  Installer pret : ${MSF_INSTALLER_DIR}"
    COMMAND ${CMAKE_COMMAND} -E echo "    - firmware_16MB_mysafefob.bin"
    COMMAND ${CMAKE_COMMAND} -E echo "    - ota_data_initial_16MB.bin (boot app0)"
    COMMAND ${CMAKE_COMMAND} -E echo "    + artefacts factory (bootloader hook,"
    COMMAND ${CMAKE_COMMAND} -E echo "      partitions, factory) si build factory present"
    COMMAND ${CMAKE_COMMAND} -E echo "============================================"
    VERBATIM
)
