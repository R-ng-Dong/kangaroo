file(GLOB_RECURSE SRCS "led_manager.c" "water_runtime.c" "status_manager.c" "water_profile.c" "tds_task.c" "tds_manager.c" "app.c" "system_runtime.c" "filter_runtime.c" "filter_manager.c" "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES sdk protocols app_context
                       REQUIRES common nvs_flash
                       )
