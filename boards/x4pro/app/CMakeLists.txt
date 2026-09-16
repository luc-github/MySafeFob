# MySafeFob — composant board x4pro : splash + écran de veille e-ink
# (Phase 8c slice 1), via FreeInkUI (ADR-010 amendé, copie figée
# components/freeinkui/). Le BSP complet de l'app (touch/RTC/gauge/UI)
# remplacera/étendra ce composant en tâche 8.4. Driver eink copié de la
# factory éprouvée.
idf_component_register(
    SRCS "splash.cpp" "eink.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer freeinkui
)
