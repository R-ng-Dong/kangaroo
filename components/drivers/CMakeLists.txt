file(GLOB_RECURSE SRCS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES freertos log esp_timer esp_adc nvs_flash
                       REQUIRES driver common)
