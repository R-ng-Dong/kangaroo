/**
 * @file IOT_CommonLog.c
 * @brief Implementation of minimal logging abstraction
 */

#include "IOT_CommonLog.h"
#include <stddef.h>

/*==============================================================================
 * Static Variables
 *============================================================================*/

static CommonLogCallback_t s_logCallback = NULL;

/*==============================================================================
 * Public Functions
 *============================================================================*/

void Common_SetLogCallback(CommonLogCallback_t callback)
{
    s_logCallback = callback;
}

void Common_LogError(const char* tag, const char* fmt, ...)
{
    if (s_logCallback != NULL) {
        va_list args;
        va_start(args, fmt);
        s_logCallback(COMMON_LOG_ERROR, tag, fmt, args);
        va_end(args);
    }
}

void Common_LogWarn(const char* tag, const char* fmt, ...)
{
    if (s_logCallback != NULL) {
        va_list args;
        va_start(args, fmt);
        s_logCallback(COMMON_LOG_WARN, tag, fmt, args);
        va_end(args);
    }
}

void Common_LogInfo(const char* tag, const char* fmt, ...)
{
    if (s_logCallback != NULL) {
        va_list args;
        va_start(args, fmt);
        s_logCallback(COMMON_LOG_INFO, tag, fmt, args);
        va_end(args);
    }
}
