idf_component_register(SRCS "gc9a01.c" "font8x16.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board_config common_spi esp_driver_gpio freertos)
