cmake_minimum_required(VERSION 3.10)


get_filename_component(SDK_PATH
        "${CMAKE_CURRENT_LIST_DIR}/EmbeddedCore/IOTCoreBase/" 
        REALPATH
    )
# set(PROJECT_VER "2.0.0.0") 

# Now include ESP-IDF project helpers
# The build script passes the full chain via -D SDKCONFIG_DEFAULTS; only
# fill in the default chain when it did not (e.g. a plain `idf.py build`).
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    list(APPEND SDKCONFIG_DEFAULTS
                    "${CMAKE_SOURCE_DIR}/sdkconfig.defaults"
                    "${CMAKE_SOURCE_DIR}/sdkconfig.defaults.esp32c3")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define project
project(esp32App)