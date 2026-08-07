idf_component_register(SRCS "es8311.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board_config
                                esp_codec_dev
                                esp_driver_gpio
                                esp_driver_i2c
                                esp_driver_i2s)
