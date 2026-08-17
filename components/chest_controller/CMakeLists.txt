idf_component_register(SRCS "chest_controller.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board_config
                                driver
                                esp_common
                                display_status
                                freertos
                                kws_wakeup
                                led_control
                                log
                                servo
                                voiceprint_auth
                                voiceprint_delete_ui)
