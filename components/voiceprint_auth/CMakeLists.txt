idf_component_register(SRCS "voiceprint_auth.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_client
                                json
                                nvs_flash)
