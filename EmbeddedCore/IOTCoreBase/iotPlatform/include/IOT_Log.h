/**
 * @file IOT_Log.h
 * @brief Multi-level logging system with thread safety support
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Log levels */
typedef enum
{
    LOG_LEVEL_TRACE = 0,
    LOG_LEVEL_DEBUG = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_WARN = 3,
    LOG_LEVEL_ERROR = 4,
    LOG_LEVEL_FATAL = 5,
    LOG_LEVEL_OFF = 6
} log_level_t;

/* Log output targets */
typedef enum
{
    LOG_TARGET_STDOUT = 1,
    LOG_TARGET_STDERR = 2,
    LOG_TARGET_FILE = 4,
    LOG_TARGET_ALL = LOG_TARGET_STDOUT | LOG_TARGET_STDERR | LOG_TARGET_FILE
} log_target_t;

/* Log configuration structure */
typedef struct
{
    log_level_t level;
    log_target_t target;
    FILE *file_handle;
    const char *filename;
    int show_timestamp;
    int show_level;
    int show_function;
    int show_line;
    int show_color;
    int auto_flush;
} log_config_t;

/* ANSI color codes */
#define IOT_COLOR_RESET "\033[0m"
#define IOT_COLOR_RED "\033[31m"
#define IOT_COLOR_GREEN "\033[32m"
#define IOT_COLOR_YELLOW "\033[33m"
#define IOT_COLOR_BLUE "\033[34m"
#define IOT_COLOR_MAGENTA "\033[35m"
#define IOT_COLOR_CYAN "\033[36m"
#define IOT_COLOR_WHITE "\033[37m"
#define IOT_COLOR_GRAY "\033[90m"

/* Global log configuration */
extern log_config_t g_log_config;

/* Core logging functions */
void log_init(void);      /* Optional - auto-initializes on first use */
void log_cleanup(void);
void log_set_level(log_level_t level);
void log_set_target(log_target_t target);
void log_set_file(const char *filename);
void log_close_file(void);
void log_enable_timestamp(int enable);
void log_enable_colors(int enable);
void log_enable_function_info(int enable);
void log_enable_auto_flush(int enable);

/* Internal logging function */
void log_write(log_level_t level, const char *file, int line, const char *func,
               const char *format, ...);

/* Optional tee sink: duplicates each formatted log line (prefix + message + '\n',
   no color codes) to a consumer. NULL disables. Used by the TCP log forwarder. */
typedef void (*log_tee_fn_t)(const char *line, size_t len);
void log_set_tee(log_tee_fn_t fn);

/* Main logging macros - for end users */
#define LOG_TRACE(...) log_write(LOG_LEVEL_TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_DEBUG(...) log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFO(...) log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_WARN(...) log_write(LOG_LEVEL_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR(...) log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_FATAL(...) log_write(LOG_LEVEL_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

/* Library internal logging control - users can override this */
#ifndef IOT_LOG_LOCAL_LEVEL
#define IOT_LOG_LOCAL_LEVEL LOG_LEVEL_INFO  // Default: show INFO and above
#endif

/* Internal library logging macros - can be disabled at compile time */
#if IOT_LOG_LOCAL_LEVEL <= LOG_LEVEL_TRACE
#define IOT_LOG_TRACE(...) LOG_TRACE(__VA_ARGS__)
#else
#define IOT_LOG_TRACE(...) do {} while(0)
#endif

#if IOT_LOG_LOCAL_LEVEL <= LOG_LEVEL_DEBUG
#define IOT_LOG_DEBUG(...) LOG_DEBUG(__VA_ARGS__)
#else
#define IOT_LOG_DEBUG(...) do {} while(0)
#endif

#if IOT_LOG_LOCAL_LEVEL <= LOG_LEVEL_INFO
#define IOT_LOG_INFO(...) LOG_INFO(__VA_ARGS__)
#else
#define IOT_LOG_INFO(...) do {} while(0)
#endif

#if IOT_LOG_LOCAL_LEVEL <= LOG_LEVEL_WARN
#define IOT_LOG_WARN(...) LOG_WARN(__VA_ARGS__)
#else
#define IOT_LOG_WARN(...) do {} while(0)
#endif

#if IOT_LOG_LOCAL_LEVEL <= LOG_LEVEL_ERROR
#define IOT_LOG_ERROR(...) LOG_ERROR(__VA_ARGS__)
#else
#define IOT_LOG_ERROR(...) do {} while(0)
#endif

#if IOT_LOG_LOCAL_LEVEL <= LOG_LEVEL_FATAL
#define IOT_LOG_FATAL(...) LOG_FATAL(__VA_ARGS__)
#else
#define IOT_LOG_FATAL(...) do {} while(0)
#endif

/* IoT-style logging with tags (ESP-IDF format) */
#define IOT_LOGI(tag, fmt, ...) IOT_LOG_INFO("%s: " fmt, tag, ##__VA_ARGS__)
#define IOT_LOGW(tag, fmt, ...) IOT_LOG_WARN("%s: " fmt, tag, ##__VA_ARGS__)
#define IOT_LOGE(tag, fmt, ...) IOT_LOG_ERROR("%s: " fmt, tag, ##__VA_ARGS__)
#define IOT_LOGD(tag, fmt, ...) IOT_LOG_DEBUG("%s: " fmt, tag, ##__VA_ARGS__)

/* Conditional logging macros */
#define LOG_TRACE_IF(condition, ...) do { if (condition) LOG_TRACE(__VA_ARGS__); } while (0)
#define LOG_DEBUG_IF(condition, ...) do { if (condition) LOG_DEBUG(__VA_ARGS__); } while (0)
#define LOG_INFO_IF(condition, ...) do { if (condition) LOG_INFO(__VA_ARGS__); } while (0)
#define LOG_WARN_IF(condition, ...) do { if (condition) LOG_WARN(__VA_ARGS__); } while (0)
#define LOG_ERROR_IF(condition, ...) do { if (condition) LOG_ERROR(__VA_ARGS__); } while (0)
#define LOG_FATAL_IF(condition, ...) do { if (condition) LOG_FATAL(__VA_ARGS__); } while (0)

/* Hexdump logging */
void iot_log_hex_dump(log_level_t level, const void *data, size_t size, const char *prefix);
void iot_log_hex_inline(log_level_t level, const void *data, size_t size, const char *prefix);
#define LOG_HEXDUMP(level, data, size, prefix) iot_log_hex_dump(level, data, size, prefix)
#define LOG_HEX_INLINE(level, data, size, prefix) iot_log_hex_inline(level, data, size, prefix)

/* Performance/timing macros */
#define LOG_FUNCTION_ENTRY() LOG_TRACE("ENTER %s", __func__)
#define LOG_FUNCTION_EXIT() LOG_TRACE("EXIT %s", __func__)

/* Assertion-style logging */
#define LOG_ASSERT(condition, ...) \
    do { \
        if (!(condition)) { \
            LOG_FATAL("ASSERTION FAILED: %s - " __VA_ARGS__, #condition); \
            abort(); \
        } \
    } while (0)

/* Default initialization values */
#define LOG_DEFAULT_LEVEL LOG_LEVEL_DEBUG
// #define LOG_DEFAULT_LEVEL LOG_LEVEL_INFO
#define LOG_DEFAULT_TARGET LOG_TARGET_STDOUT
#define LOG_DEFAULT_TIMESTAMP 1
#define LOG_DEFAULT_LEVEL_SHOW 1
#define LOG_DEFAULT_FUNCTION 0
#define LOG_DEFAULT_LINE 0
#define LOG_DEFAULT_COLOR 1
#define LOG_DEFAULT_AUTO_FLUSH 0

/* Inline utility functions */
static inline int log_is_level_enabled(log_level_t level)
{
    return (level >= g_log_config.level && g_log_config.level != LOG_LEVEL_OFF);
}

static inline const char *log_level_string(log_level_t level)
{
    static const char *level_strings[] = {"V", "D", "I", "W", "E", "F", "O"};
    if (level >= LOG_LEVEL_TRACE && level <= LOG_LEVEL_OFF)
    {
        return level_strings[level];
    }
    return "?";
}

static inline const char *log_level_color(log_level_t level)
{
    static const char *level_colors[] = {
        IOT_COLOR_GRAY,    // TRACE
        IOT_COLOR_CYAN,    // DEBUG
        IOT_COLOR_GREEN,   // INFO
        IOT_COLOR_YELLOW,  // WARN
        IOT_COLOR_RED,     // ERROR
        IOT_COLOR_MAGENTA, // FATAL
        IOT_COLOR_RESET    // OFF
    };
    if (level >= LOG_LEVEL_TRACE && level <= LOG_LEVEL_OFF)
    {
        return level_colors[level];
    }
    return IOT_COLOR_RESET;
}

#ifdef __cplusplus
}
#endif