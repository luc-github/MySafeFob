# copy_if_exists.cmake — copie les artefacts factory vers installer/ (app).
# Lance en mode script (-P) par le postbuild de l'app. Un artefact absent
# = warning (le build factory n'est pas une dependance de l'app).
#
# Attendu : -D SRC_DIR=<build factory> -D DST_DIR=<installer variant> -D BOARD=<board>

if(NOT EXISTS "${SRC_DIR}")
    message(WARNING
        "[${BOARD}] build factory absent : ${SRC_DIR}\n"
        "   -> installer sans bootloader hook / partitions / factory.\n"
        "   Lancer d'abord : python boards/${BOARD}/build_scripts/build_one.py factory")
    return()
endif()

set(_artifacts
    "bootloader/bootloader.bin:bootloader_16MB.bin"
    "partition_table/partition-table.bin:partitions_16mb.bin"
    "mysafefob-factory.bin:factory_16MB.bin"
)

foreach(_pair ${_artifacts})
    string(REPLACE ":" ";" _parts "${_pair}")
    list(GET _parts 0 _src)
    list(GET _parts 1 _dst)
    if(EXISTS "${SRC_DIR}/${_src}")
        file(COPY_FILE "${SRC_DIR}/${_src}" "${DST_DIR}/${_dst}" ONLY_IF_DIFFERENT)
        message(STATUS "[${BOARD}]   + ${_dst}")
    else()
        message(WARNING "[${BOARD}] artefact factory manquant : ${_src}")
    endif()
endforeach()
