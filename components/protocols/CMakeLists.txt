file(GLOB_RECURSE SRCS "uart_protocol.c" "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_timer log
                       REQUIRES drivers)
