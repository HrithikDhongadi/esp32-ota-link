idf_component_register(
    SRCS
        "main.c"
        "protocol.c"
        "commands.c"
        "device_info.c"
    INCLUDE_DIRS "."
    REQUIRES
        app_update
        esp_app_format
        esp_driver_uart
        esp_timer
        heap
        nvs_flash
)
