idf_component_register(
    SRCS
        "main.c"
        "protocol.c"
        "commands.c"
        "device_info.c"
        "ota_manager.c"
    INCLUDE_DIRS "."
    REQUIRES
        app_update
        esp_app_format
        esp_driver_uart
        esp_partition
        esp_timer
        heap
        log
        nvs_flash
)
