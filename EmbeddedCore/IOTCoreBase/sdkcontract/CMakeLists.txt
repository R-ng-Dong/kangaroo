# sdkcontract: the code behind the cloud/app system contract (DAOs and the services that keep the
# device consistent with what the cloud and app expect). Depends only on the public layers below it.
file(GLOB_RECURSE SRCS "${CMAKE_CURRENT_LIST_DIR}/src/*.c")

# Record on (the default): IOT_Record.c and its store. Record off (CONFIG_IOT_RECORD_ENABLED=n):
# IOT_ContractRecordOff.c instead, which answers every Record call and holds no static RAM.
# Only SRCS may follow CONFIG_: REQUIRES is expanded in the early pass, before the config is
# loaded, so that pass keeps the default source set.
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT CONFIG_IOT_RECORD_ENABLED)
    list(FILTER SRCS EXCLUDE REGEX "/src/IOT_Record\\.c$")
    list(FILTER SRCS EXCLUDE REGEX "/src/dao/IOT_DaoRecord\\.c$")
else()
    list(FILTER SRCS EXCLUDE REGEX "/src/IOT_ContractRecordOff\\.c$")
endif()

# The attribute table (defines/IOT_AttrDefines.h) belongs to the application, not to this component:
# it ships as copy-and-adapt reference material. The project says where its copy lives, as the
# directory that contains defines/IOT_AttrDefines.h, before project.cmake runs:
#   set(IOT_ATTR_DEFINES_DIR "<dir containing defines/IOT_AttrDefines.h>")
# (IDF also runs this file in an early requirements pass where project variables are not set.)
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT IOT_ATTR_DEFINES_DIR)
    message(FATAL_ERROR "sdkcontract: set IOT_ATTR_DEFINES_DIR to the directory that contains "
                        "defines/IOT_AttrDefines.h (your attribute table) before project.cmake")
endif()

if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT EXISTS "${IOT_ATTR_DEFINES_DIR}/defines/IOT_AttrDefines.h")
    message(FATAL_ERROR "sdkcontract: ${IOT_ATTR_DEFINES_DIR}/defines/IOT_AttrDefines.h does not exist; "
                        "IOT_ATTR_DEFINES_DIR must be the directory that contains defines/IOT_AttrDefines.h")
endif()

# include/ is the partner API. internal_include/ is the component's own; an application sees it
# only when its project sets SDKCONTRACT_EXPOSE_INTERNAL before project.cmake (unsupported, for
# test apps that drive the internals directly).
set(SDKCONTRACT_PUBLIC_DIRS include include/dao)
set(SDKCONTRACT_INTERNAL_DIRS internal_include internal_include/dao internal_include/services)
if(SDKCONTRACT_EXPOSE_INTERNAL)
    idf_component_register(SRCS ${SRCS}
                           INCLUDE_DIRS ${SDKCONTRACT_PUBLIC_DIRS} ${SDKCONTRACT_INTERNAL_DIRS}
                           REQUIRES iotplatform iotcommon iotcore)
else()
    idf_component_register(SRCS ${SRCS}
                           INCLUDE_DIRS ${SDKCONTRACT_PUBLIC_DIRS} ${SDKCONTRACT_INTERNAL_DIRS}
                        #    PRIV_INCLUDE_DIRS ${SDKCONTRACT_INTERNAL_DIRS}
                           REQUIRES iotplatform iotcommon iotcore)
endif()

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${IOT_ATTR_DEFINES_DIR}")
endif()
