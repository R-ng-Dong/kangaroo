/**
 * @file IOT_CommonLog.h
 * @brief Minimal logging abstraction for iotcommon
 *
 * Provides optional logging that can be configured by higher layers.
 * By default, logging is disabled (no-op). Call Common_SetLogCallback()
 * to enable logging integration with IOT_Log or other logging systems.
 */

#ifndef IOT_COMMON_LOG_H
#define IOT_COMMON_LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * Log Levels
 *============================================================================*/

typedef enum {
    COMMON_LOG_ERROR = 0,
    COMMON_LOG_WARN  = 1,
    COMMON_LOG_INFO  = 2,
    COMMON_LOG_DEBUG = 3
} CommonLogLevel_t;

/*==============================================================================
 * Log Callback Type
 *============================================================================*/

/**
 * @brief Log callback function type
 *
 * @param level Log level (error, warn, info, debug)
 * @param tag Module tag (e.g., "Memory", "String")
 * @param fmt Printf-style format string
 * @param args Variable arguments
 */
typedef void (*CommonLogCallback_t)(CommonLogLevel_t level, const char* tag,
                                     const char* fmt, va_list args);

/*==============================================================================
 * Configuration
 *============================================================================*/

/**
 * @brief Set the log callback function
 *
 * Call this to enable logging. Pass NULL to disable.
 *
 * @param callback Log callback function, or NULL to disable logging
 *
 * @example
 *     // Integration with IOT_Log:
 *     void myLogCallback(CommonLogLevel_t level, const char* tag,
 *                        const char* fmt, va_list args) {
 *         char buf[256];
 *         vsnprintf(buf, sizeof(buf), fmt, args);
 *         switch(level) {
 *             case COMMON_LOG_ERROR: IOT_LOGE(tag, "%s", buf); break;
 *             case COMMON_LOG_WARN:  IOT_LOGW(tag, "%s", buf); break;
 *             default:               IOT_LOGI(tag, "%s", buf); break;
 *         }
 *     }
 *     Common_SetLogCallback(myLogCallback);
 */
void Common_SetLogCallback(CommonLogCallback_t callback);

/*==============================================================================
 * Internal Logging Functions (used by iotcommon modules)
 *============================================================================*/

/** @internal Log error message */
void Common_LogError(const char* tag, const char* fmt, ...);

/** @internal Log warning message */
void Common_LogWarn(const char* tag, const char* fmt, ...);

/** @internal Log info message */
void Common_LogInfo(const char* tag, const char* fmt, ...);

/*==============================================================================
 * Convenience Macros for iotcommon modules
 *============================================================================*/

#define COMMON_LOGE(tag, fmt, ...) Common_LogError(tag, fmt, ##__VA_ARGS__)
#define COMMON_LOGW(tag, fmt, ...) Common_LogWarn(tag, fmt, ##__VA_ARGS__)
#define COMMON_LOGI(tag, fmt, ...) Common_LogInfo(tag, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* IOT_COMMON_LOG_H */
