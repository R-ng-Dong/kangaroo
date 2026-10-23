idf_component_register(SRCS "storage.c" "rtc_time.c" 
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES spiffs
                       REQUIRES common)
