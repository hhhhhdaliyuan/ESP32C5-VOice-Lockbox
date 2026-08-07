idf_component_register(SRCS "led_control.c" "led_demo.c"
                    INCLUDE_DIRS "include"
                    REQUIRES board_config driver freertos servo)
