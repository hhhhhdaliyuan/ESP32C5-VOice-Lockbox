idf_component_register(SRCS "kws_wakeup.c"
                       INCLUDE_DIRS "include"
                       REQUIRES es8311
                                board_config
                                voiceprint_auth
                                esp_driver_gpio
                                heap
                                mbedtls
                                esp_event
                                esp_hw_support
                                esp_netif
                                esp_wifi
                                esp_websocket_client
                                json
                                nvs_flash)
