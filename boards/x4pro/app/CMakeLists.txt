# MySafeFob — x4pro board component: splash + e-ink sleep screen
# (Phase 8c slice 1), via FreeInkUI (ADR-010 amended, frozen copy
# components/freeinkui/). The app's full BSP (touch/RTC/gauge/UI)
# will replace/extend this component in task 8.4. Eink driver copied
# from the proven factory.
idf_component_register(
    SRCS "splash.cpp" "eink.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer freeinkui
)
