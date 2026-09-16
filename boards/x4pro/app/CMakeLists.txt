# MySafeFob — x4pro board component: splash/sleep screens + the
# interactive menu (task 8.4 minimal slice: touch + button navigation,
# ADR-012 idle activity manager), via FreeInkUI (ADR-010 amended, frozen
# copy components/freeinkui/). eink.c/touch.c/buttons.c/i2c_bus.c are
# hardware-validated copies from the factory (ADR-010 pt.3: deliberately
# separate, never shared). RTC/gauge pins still not needed by the app.
idf_component_register(
    SRCS "splash.cpp" "eink.c" "touch.c" "buttons.c" "i2c_bus.c" "ui_nav.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer freeinkui power_mgr
)
