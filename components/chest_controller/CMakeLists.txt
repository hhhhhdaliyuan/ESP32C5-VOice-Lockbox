idf_component_register(SRCS "chest_controller.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board_config
                                esp_common
                                freertos
                                kws_wakeup
                                log
                                servo)
