# X4 Pro — board configuration (MySafeFob board-agnostic layer, ADR-008)
# Inclus par le CMakeLists racine AVANT project.cmake.
# Doit definir : IDF_TARGET et SDKCONFIG_DEFAULTS (fichiers sdkconfig en
# cascade : commun puis board).

set(IDF_TARGET "esp32s3")

set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults"
    "${CMAKE_CURRENT_SOURCE_DIR}/boards/x4pro/sdkconfig.defaults")

# Composant board (splash + écran de veille e-ink, Phase 8c slice 1).
# Le BSP complet (touch/RTC/gauge/UI) étendra ce dossier en 8.4.
# EXTRA_COMPONENT_DIRS doit être défini avant project() — ce fichier est
# inclus avant project.cmake par le CMakeLists racine.
#
# "boards/x4pro/app" EST lui-même un composant (son CMakeLists.txt fait
# idf_component_register directement) -> IDF ne scanne PAS automatiquement
# un sous-dossier components/ a l'interieur d'une entree EXTRA_COMPONENT_DIRS
# qui est deja un composant. D'où la 2e entrée séparée ci-dessous pour que
# components/freeinkui/ (copie figée, ADR-010 amendé) soit bien découvert.
list(APPEND EXTRA_COMPONENT_DIRS
     "${CMAKE_CURRENT_SOURCE_DIR}/boards/x4pro/app"
     "${CMAKE_CURRENT_SOURCE_DIR}/boards/x4pro/app/components")
