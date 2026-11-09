idf_component_register(SRCS "storage.c" "rtc_time.c" "system_config.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES spiffs nvs_flash
                       REQUIRES common)
