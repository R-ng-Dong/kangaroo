file(GLOB_RECURSE SRCS "core/*.c" "cloud/*.c" "water/*.c" "managers/*.c")

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES sdkcontract protocols app_context systems json
                       REQUIRES common nvs_flash
                       )
