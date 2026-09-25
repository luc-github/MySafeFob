# MySafeFob — x4pro board component: splash/sleep screens + the
# interactive menu and Settings screens (task 8.4), via LVGL
# (ADR-010 amended again — LVGL back as a managed dependency, see
# idf_component.yml, replacing FreeInkUI).
# eink.c/touch.c/buttons.c/i2c_bus.c/battery.c are hardware-validated
# copies from the factory (ADR-010 pt.3: deliberately separate, never
# shared). RTC pins still not needed by the app. settings_store.c is this
# component's first NVS-backed persistence (UI preferences only).
# lv_port_disp/lv_port_indev glue LVGL onto eink.c/touch.c without
# touching either driver.
idf_component_register(
    SRCS "splash.cpp" "eink.c" "touch.c" "buttons.c" "i2c_bus.c"
         "ui_nav.cpp" "ui_widgets.cpp"
         "ui_screen_home.cpp" "ui_screen_settings.cpp"
         "ui_screen_settings_controls.cpp" "ui_screen_settings_display.cpp"
         "ui_screen_settings_about.cpp" "ui_screen_touch_diag.cpp"
         "ui_screen_security.cpp" "ui_screen_time.cpp" "ui_keyboard.cpp"
         "time_service.c" "wifi_time.c" "ble_time.c"
         "settings_store.c" "battery.c" "frontlight.c"
         "lv_port_disp.c" "lv_port_indev.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer lvgl power_mgr nvs_flash esp_app_format
             esp_wifi esp_netif esp_event bt
)
