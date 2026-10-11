# MySafeFob main — Phase 8 skeleton (hello world + console)
idf_component_register(SRCS "main.c" "cmd_setting.c" "cmd_battery.c" "cmd_settime.c" "cmd_kdfbench.c" "cmd_store.c" "cmd_backup.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES totp_engine secret_store esp3d_log power_mgr
                                    console nvs_flash esp_driver_uart freertos app monocypher)
