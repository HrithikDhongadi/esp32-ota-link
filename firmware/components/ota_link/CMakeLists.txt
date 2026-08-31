idf_component_register(
    SRCS
        "src/ota_link.c"
        "src/protocol.c"
        "src/commands.c"
        "src/auth.c"
        "src/device_info.c"
        "src/ota_manager.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES
        app_update
        esp_app_format
        esp_driver_uart
        esp_partition
        esp_timer
        freertos
        heap
        log
        mbedtls
        nvs_flash
)
