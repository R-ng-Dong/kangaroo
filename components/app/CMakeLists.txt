file(GLOB_RECURSE SRCS "water_controller_task.c" "water_controller.c" "device_element_manager.c" "energy_saving_manager.c" "energy_consumption.c" "led_manager.c" "water_runtime.c" "status_manager.c" "water_profile.c" "tds_task.c" "tds_manager.c" "app.c" "system_runtime.c" "filter_runtime.c" "filter_manager.c" "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES sdk protocols app_context systems
                       REQUIRES common nvs_flash
                       )
