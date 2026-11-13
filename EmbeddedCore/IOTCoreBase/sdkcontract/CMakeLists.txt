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

# include/ is the partner API. internal_include/ is the component's own; an application sees it
# only when its project sets SDKCONTRACT_EXPOSE_INTERNAL before project.cmake (unsupported, for
# test apps that drive the internals directly).
set(SDKCONTRACT_PUBLIC_DIRS include include/dao internal_include)
set(SDKCONTRACT_INTERNAL_DIRS internal_include/dao)
if(SDKCONTRACT_EXPOSE_INTERNAL)
    idf_component_register(SRCS ${SRCS}
                           INCLUDE_DIRS ${SDKCONTRACT_PUBLIC_DIRS} ${SDKCONTRACT_INTERNAL_DIRS}
                           REQUIRES iotplatform iotcommon iotcore)
else()
    idf_component_register(SRCS ${SRCS}
                           INCLUDE_DIRS ${SDKCONTRACT_PUBLIC_DIRS}
                           PRIV_INCLUDE_DIRS ${SDKCONTRACT_INTERNAL_DIRS}
                           REQUIRES iotplatform iotcommon iotcore)
endif()
