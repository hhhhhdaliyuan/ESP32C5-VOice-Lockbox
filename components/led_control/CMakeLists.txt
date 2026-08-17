idf_component_register(SRCS "led_control.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board_config driver freertos)