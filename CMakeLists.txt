# MySafeFob — CMakeLists racine
# Sélection du board : cmake -DMSF_BOARD=x4pro (défaut) ou -DMSF_BOARD=m5paper_mono
cmake_minimum_required(VERSION 3.16)

set(MSF_BOARD "x4pro" CACHE STRING "Target board (see boards/)")

# Board config : IDF_TARGET + sdkconfig complémentaire du board.
# DOIT être inclus avant project.cmake (SDKCONFIG_DEFAULTS y est défini).
include(${CMAKE_CURRENT_SOURCE_DIR}/boards/${MSF_BOARD}/board_config.cmake)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mysafefob)

# Nom du board expose au code applicatif (string literal).
# add_compile_definitions() apres project() ne se propage PAS aux composants
# IDF — il faut idf_build_set_property (constaté au build 5.5.5).
idf_build_set_property(COMPILE_DEFINITIONS "MSF_BOARD_NAME=\"${MSF_BOARD}\"" APPEND)

# Postbuild : alimente installer/<board>_app/ (flash_mgr + web installer).
include(boards/${MSF_BOARD}/cmake/postbuild.cmake)

# Niveau de log applicatif esp3d_log (0=none 1=error 2=warning 3=debug 4=all)
# Dev : 3. Release : 2 (warnings+erreurs seulement).
# BUGFIX 2026-09-16 : add_compile_options() ici (apres project()) ne se
# propage PAS aux composants IDF — meme piege que MSF_BOARD_NAME ci-dessus,
# non applique a cette ligne jusqu'ici. Consequence decouverte apres coup :
# ESP3D_LOG n'a jamais ete defini pour aucun composant -> TOUS les appels
# esp3d_log()/esp3d_log_d()/esp3d_log_w()/esp3d_log_e() du projet (dont
# ceux de main.c, y compris la banniere de boot) se compilaient en no-op
# silencieux, quel que soit MSF_LOG_LEVEL. idf_build_set_property() est le
# mecanisme qui fonctionne reellement (deja utilise juste au-dessus).
set(MSF_LOG_LEVEL 3 CACHE STRING "esp3d_log level (0-4)")
idf_build_set_property(COMPILE_DEFINITIONS "ESP3D_LOG=${MSF_LOG_LEVEL}" APPEND)
