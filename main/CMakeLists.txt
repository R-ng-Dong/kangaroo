set(ROOT_DIR ${CMAKE_SOURCE_DIR}) # chỉ gán biến, không hiểu j về path, không xử lý được các lệnh: ./, ../
get_filename_component(ROOT_DIR ${ROOT_DIR} REALPATH) # xử lý đường dẫn, ROOT_DIR = CMAKE_SOURCE_DIR = .../FW_SDK

# Keep sources explicit so a newly added factory module is compiled on the next build.
set(SRCS "main.c" "factory_test_udp.c" "factory_test_ble.c")

# Collect all include directories
file(GLOB_RECURSE ALL_HEADERS "${ROOT_DIR}/main/*.h") 
# Extract directory paths from header list
set(INCLUDE_DIRS "")
foreach(_header ${ALL_HEADERS})
    get_filename_component(_dir ${_header} DIRECTORY)    
    list(APPEND INCLUDE_DIRS ${_dir}) 
endforeach()
message(STATUS "Root directory: ${ROOT_DIR}")
 
# Remove duplicates 
list(REMOVE_DUPLICATES INCLUDE_DIRS)
   
idf_component_register(SRCS "${SRCS}" 
                       INCLUDE_DIRS "${INCLUDE_DIRS}"  
                       REQUIRES app drivers iotcore iotplatform sdk esp_wifi esp_event nvs_flash esp_eth bt mqtt esp_netif json )

# target_compile_options(${COMPONENT_LIB} PRIVATE "-std=gnu++17")

# component_compile_options(-Wno-unused-variable -Wno-unused-function)
# component_compile_options(-Wno-missing-field-initializers)
# component_compile_options(-Wno-deprecated-declarations)
# component_compile_options(-Wno-error=format= -Wno-format)
