#include "IOT_ErrorManager.h"

const char *iot_err_to_name(iot_err_t code)
{
    switch (code)
    {
    case IOT_OK:
        return "IOT_OK";
    case IOT_ERR_FAIL:
        return "IOT_ERR_FAIL";
    case IOT_ERR_NO_MEM:
        return "IOT_ERR_NO_MEM";
    case IOT_ERR_INVALID_ARG:
        return "IOT_ERR_INVALID_ARG";
    case IOT_ERR_INVALID_STATE:
        return "IOT_ERR_INVALID_STATE";
    case IOT_ERR_INVALID_SIZE:
        return "IOT_ERR_INVALID_SIZE";
    case IOT_ERR_NOT_FOUND:
        return "IOT_ERR_NOT_FOUND";
    case IOT_ERR_NOT_SUPPORTED:
        return "IOT_ERR_NOT_SUPPORTED";
    case IOT_ERR_TIMEOUT:
        return "IOT_ERR_TIMEOUT";
    case IOT_ERR_INVALID_RESPONSE:
        return "IOT_ERR_INVALID_RESPONSE";
    case IOT_ERR_INVALID_CRC:
        return "IOT_ERR_INVALID_CRC";
    case IOT_ERR_INVALID_VERSION:
        return "IOT_ERR_INVALID_VERSION";
    case IOT_ERR_INVALID_MAC:
        return "IOT_ERR_INVALID_MAC";
    case IOT_ERR_NOT_FINISHED:
        return "IOT_ERR_NOT_FINISHED";
    case IOT_ERR_NOT_ALLOWED:
        return "IOT_ERR_NOT_ALLOWED";
    case IOT_ERR_ROC_IN_PROGRESS:
        return "IOT_ERR_ROC_IN_PROGRESS";
    case IOT_ERR_OTA_BOOT_DEGRADED:
        return "IOT_ERR_OTA_BOOT_DEGRADED";
    default:
        return "Unknown error code";
    }
}
