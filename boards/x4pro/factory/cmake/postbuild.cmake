# ---------------------------------------------------------------------------
# MySafeFob — postbuild FACTORY : alimente installer/<board>_factory/.
#
# Contenu : factory_16MB.bin + bootloader_16MB.bin (hook) + partitions_16mb.bin
# + ota_data_initial_16MB.bin ZERO-filled (celui du build idf = "aucune app
# OTA selectionnee" -> boot factory GARANTI apres flash de cette variante).
# ---------------------------------------------------------------------------

set(MSF_INSTALLER_DIR "${CMAKE_SOURCE_DIR}/../../../installer/x4pro_factory")

add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${MSF_INSTALLER_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MSF_INSTALLER_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_BINARY_DIR}/mysafefob-factory.bin"
        "${MSF_INSTALLER_DIR}/factory_16MB.bin"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_BINARY_DIR}/bootloader/bootloader.bin"
        "${MSF_INSTALLER_DIR}/bootloader_16MB.bin"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_BINARY_DIR}/partition_table/partition-table.bin"
        "${MSF_INSTALLER_DIR}/partitions_16mb.bin"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_BINARY_DIR}/ota_data_initial.bin"
        "${MSF_INSTALLER_DIR}/ota_data_initial_16MB.bin"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "============================================"
    COMMAND ${CMAKE_COMMAND} -E echo "  Installer pret : ${MSF_INSTALLER_DIR}"
    COMMAND ${CMAKE_COMMAND} -E echo "    - factory_16MB.bin"
    COMMAND ${CMAKE_COMMAND} -E echo "    - bootloader_16MB.bin (hook recovery)"
    COMMAND ${CMAKE_COMMAND} -E echo "    - partitions_16mb.bin"
    COMMAND ${CMAKE_COMMAND} -E echo "    - ota_data_initial_16MB.bin (zero -> boot factory)"
    COMMAND ${CMAKE_COMMAND} -E echo "============================================"
    VERBATIM
)
