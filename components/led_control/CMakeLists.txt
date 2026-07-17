idf_component_register(SRCS "led_control.c" "led_demo.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver freertos servo)
