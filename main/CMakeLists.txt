# Keep sources explicit so a newly added factory module is compiled on the next build.
set(SRCS "main.c" "factory_test_udp.c")

idf_component_register(SRCS "${SRCS}" 
                       INCLUDE_DIRS "include"
                       REQUIRES app drivers iotcore iotplatform sdk esp_wifi esp_event nvs_flash esp_eth bt mqtt esp_netif json )

# target_compile_options(${COMPONENT_LIB} PRIVATE "-std=gnu++17")

# component_compile_options(-Wno-unused-variable -Wno-unused-function)
# component_compile_options(-Wno-missing-field-initializers)
# component_compile_options(-Wno-deprecated-declarations)
# component_compile_options(-Wno-error=format= -Wno-format)
