file(GLOB_RECURSE SRCS "energy_saving_manager.c" "cloud_event_handler.c" "cloud_sync.c" "report_alert.c" "filter_manager.c" "flush_cycle_state.c" "water_profile.c" "app.c" "tds_manager.c" "tds_task.c" "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES sdkcontract sdk protocols app_context systems json
                       REQUIRES common nvs_flash
                       )
