# MySafeFob — x4pro board component: splash/sleep screens + the
# interactive menu and Settings screens (task 8.4), via FreeInkUI
# (ADR-010 amended, frozen copy components/freeinkui/).
# eink.c/touch.c/buttons.c/i2c_bus.c/battery.c are hardware-validated
# copies from the factory (ADR-010 pt.3: deliberately separate, never
# shared). RTC pins still not needed by the app. settings_store.c is this
# component's first NVS-backed persistence (UI preferences only).
idf_component_register(
    SRCS "splash.cpp" "eink.c" "touch.c" "buttons.c" "i2c_bus.c" "ui_nav.cpp"
         "settings_store.c" "battery.c" "frontlight.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer freeinkui power_mgr nvs_flash esp_app_format
)
