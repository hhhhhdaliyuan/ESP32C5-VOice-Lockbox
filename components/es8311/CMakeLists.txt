idf_component_register(SRCS "es8311.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES audio_i2s esp_driver_i2c esp_codec_dev heap)
