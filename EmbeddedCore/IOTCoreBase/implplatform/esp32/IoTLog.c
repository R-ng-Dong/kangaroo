/**
 * @file IOT_Log.c
 * @brief Implementation of multi-level logging system
 */

#include "IOT_Log.h"
#include <stdarg.h>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #include <io.h>
    #define isatty _isatty
    #define STDOUT_FILENO 1
#elif defined(ESP32) || defined(ESP_PLATFORM)
    #include <sys/time.h>
    #include "freertos/FreeRTOS.h"
    #include "freertos/semphr.h"
    #include "esp_log.h"
    #include "esp_rom_sys.h"
    #include <unistd.h>
#else
    #include <sys/time.h>
    #include <unistd.h>
#endif

/* Thread safety configuration */
#if defined(ESP32) || defined(ESP_PLATFORM)
    #define LOG_ESP32_THREAD_SAFE
#endif

/* Global log configuration with default values */
log_config_t g_log_config = {
    .level = LOG_DEFAULT_LEVEL,
    .target = LOG_DEFAULT_TARGET,
    .file_handle = NULL,
    .filename = NULL,
    .show_timestamp = LOG_DEFAULT_TIMESTAMP,
    .show_level = LOG_DEFAULT_LEVEL_SHOW,
    .show_function = LOG_DEFAULT_FUNCTION,
    .show_line = LOG_DEFAULT_LINE,
    .show_color = LOG_DEFAULT_COLOR,
    .auto_flush = LOG_DEFAULT_AUTO_FLUSH
};

/* Thread-safe mutex with lazy initialization */
#ifdef LOG_ESP32_THREAD_SAFE
static SemaphoreHandle_t log_semaphore = NULL;
static int log_semaphore_initialized = 0;

static inline void log_ensure_init(void)
{
    if (!log_semaphore_initialized)
    {
        log_semaphore = xSemaphoreCreateMutex();
        log_semaphore_initialized = 1;
    }
}

#define LOG_LOCK() \
    do { \
        log_ensure_init(); \
        if (log_semaphore) xSemaphoreTake(log_semaphore, portMAX_DELAY); \
    } while (0)

#define LOG_UNLOCK() \
    do { \
        if (log_semaphore) xSemaphoreGive(log_semaphore); \
    } while (0)
#else
#define LOG_LOCK() do {} while (0)
#define LOG_UNLOCK() do {} while (0)
#endif

static log_tee_fn_t s_log_tee = NULL;

void log_set_tee(log_tee_fn_t fn)
{
    LOG_LOCK();
    s_log_tee = fn;
    LOG_UNLOCK();
}

static void log_write_console(const char *color_start, const char *prefix, const char *message, const char *color_end)
{
#if defined(ESP32) || defined(ESP_PLATFORM)
    esp_rom_printf("%s%s%s%s\n", color_start, prefix, message, color_end);
#else
    fprintf(stdout, "%s%s%s%s\n", color_start, prefix, message, color_end);
    if (g_log_config.auto_flush)
    {
        fflush(stdout);
    }
#endif
}

static void log_write_error_console(const char *color_start, const char *prefix, const char *message,
                                    const char *color_end)
{
#if defined(ESP32) || defined(ESP_PLATFORM)
    esp_rom_printf("%s%s%s%s\n", color_start, prefix, message, color_end);
#else
    fprintf(stderr, "%s%s%s%s\n", color_start, prefix, message, color_end);
    if (g_log_config.auto_flush)
    {
        fflush(stderr);
    }
#endif
}

/* Initialize logging system */
void log_init(void)
{
    /* Create semaphore for thread safety on ESP32 */
#ifdef LOG_ESP32_THREAD_SAFE
    if (log_semaphore == NULL)
    {
        log_semaphore = xSemaphoreCreateMutex();
    }
#endif

    LOG_LOCK();

    /* Initialize with default values */
    g_log_config.level = LOG_DEFAULT_LEVEL;
    g_log_config.target = LOG_DEFAULT_TARGET;
    g_log_config.file_handle = NULL;
    g_log_config.filename = NULL;
    g_log_config.show_timestamp = LOG_DEFAULT_TIMESTAMP;
    g_log_config.show_level = LOG_DEFAULT_LEVEL_SHOW;
    g_log_config.show_function = LOG_DEFAULT_FUNCTION;
    g_log_config.show_line = LOG_DEFAULT_LINE;
    g_log_config.show_color = LOG_DEFAULT_COLOR && isatty(STDOUT_FILENO);
    g_log_config.auto_flush = LOG_DEFAULT_AUTO_FLUSH;

    LOG_UNLOCK();
}

/* Cleanup logging system */
void log_cleanup(void)
{
    LOG_LOCK();

    if (g_log_config.file_handle && g_log_config.file_handle != stdout &&
        g_log_config.file_handle != stderr)
    {
        fclose(g_log_config.file_handle);
        g_log_config.file_handle = NULL;
    }

    if (g_log_config.filename)
    {
        free((void *)g_log_config.filename);
        g_log_config.filename = NULL;
    }

    LOG_UNLOCK();

#ifdef LOG_ESP32_THREAD_SAFE
    if (log_semaphore != NULL)
    {
        vSemaphoreDelete(log_semaphore);
        log_semaphore = NULL;
    }
#endif
}

/* Get timestamp in milliseconds (time since boot) */
long long get_timestamp_ms(void)
{
#if defined(_WIN32) || defined(_WIN64)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (long long)((t - 116444736000000000ULL) / 10000ULL);
#elif defined(ESP32) || defined(ESP_PLATFORM)
    return (long long)esp_log_timestamp();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
#endif
}

/* Set log level */
void log_set_level(log_level_t level)
{
    LOG_LOCK();
    if (level >= LOG_LEVEL_TRACE && level <= LOG_LEVEL_OFF)
    {
        g_log_config.level = level;
    }
    LOG_UNLOCK();
}

/* Set log target */
void log_set_target(log_target_t target)
{
    LOG_LOCK();
    g_log_config.target = target;
    LOG_UNLOCK();
}

/* Set log file */
void log_set_file(const char *filename)
{
    LOG_LOCK();

    /* Close existing file */
    if (g_log_config.file_handle && g_log_config.file_handle != stdout &&
        g_log_config.file_handle != stderr)
    {
        fclose(g_log_config.file_handle);
        g_log_config.file_handle = NULL;
    }

    if (g_log_config.filename)
    {
        free((void *)g_log_config.filename);
        g_log_config.filename = NULL;
    }

    if (filename)
    {
        /* Open new file */
        g_log_config.file_handle = fopen(filename, "a");
        if (g_log_config.file_handle)
        {
            g_log_config.filename = strdup(filename);
            g_log_config.target |= LOG_TARGET_FILE;
        }
        else
        {
            fprintf(stderr, "Failed to open log file: %s\n", filename);
        }
    }

    LOG_UNLOCK();
}

/* Close log file */
void log_close_file(void)
{
    LOG_LOCK();

    if (g_log_config.file_handle && g_log_config.file_handle != stdout &&
        g_log_config.file_handle != stderr)
    {
        fclose(g_log_config.file_handle);
        g_log_config.file_handle = NULL;
    }

    if (g_log_config.filename)
    {
        free((void *)g_log_config.filename);
        g_log_config.filename = NULL;
    }

    g_log_config.target &= ~LOG_TARGET_FILE;

    LOG_UNLOCK();
}

/* Enable/disable timestamp */
void log_enable_timestamp(int enable)
{
    LOG_LOCK();
    g_log_config.show_timestamp = enable;
    LOG_UNLOCK();
}

/* Enable/disable colors */
void log_enable_colors(int enable)
{
    LOG_LOCK();
    g_log_config.show_color = enable && isatty(STDOUT_FILENO);
    LOG_UNLOCK();
}

/* Enable/disable function information */
void log_enable_function_info(int enable)
{
    LOG_LOCK();
    g_log_config.show_function = enable;
    g_log_config.show_line = enable;
    LOG_UNLOCK();
}

/* Enable/disable auto flush */
void log_enable_auto_flush(int enable)
{
    LOG_LOCK();
    g_log_config.auto_flush = enable;
    LOG_UNLOCK();
}

/* Core logging function */


void log_write(log_level_t level, const char *file, int line, const char *func,
               const char *format, ...)
{
    if (!log_is_level_enabled(level))
    {
        return;
    }

    LOG_LOCK();

    va_list args;
    char message[256];
    const char *color_start = "";
    const char *color_end = "";

    /* Format the message */
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    /* Set colors for terminal output */
    if (g_log_config.show_color)
    {
        color_start = log_level_color(level);
        color_end = IOT_COLOR_RESET;
    }

    /* Build prefix in ESP-IDF format: I (timestamp) */
    char prefix[64] = "";
    if (g_log_config.show_level)
    {
        long long timestamp = get_timestamp_ms();
        snprintf(prefix, sizeof(prefix), "%s (%lld) ", log_level_string(level), timestamp);
    }

    /* Output to stdout */
    if (g_log_config.target & LOG_TARGET_STDOUT)
    {
        log_write_console(color_start, prefix, message, color_end);
    }

    /* Output to stderr for warnings/errors */
    if ((g_log_config.target & LOG_TARGET_STDERR) && level >= LOG_LEVEL_WARN)
    {
        log_write_error_console(color_start, prefix, message, color_end);
    }

    /* Output to file */
    if ((g_log_config.target & LOG_TARGET_FILE) && g_log_config.file_handle)
    {
        fprintf(g_log_config.file_handle, "%s%s\n", prefix, message);
        if (g_log_config.auto_flush)
        {
            fflush(g_log_config.file_handle);
        }
    }

    /* Tee the composed line (no color) to an optional sink, e.g. the TCP forwarder.
       Capture the pointer into a local ONCE so a concurrent log_set_tee() cannot
       null it out between the check and the call. */
    log_tee_fn_t tee = s_log_tee;
    if (tee)
    {
        char linebuf[320];
        int  n = snprintf(linebuf, sizeof(linebuf), "%s%s\n", prefix, message);
        if (n > 0)
        {
            size_t len = ((size_t)n < sizeof(linebuf)) ? (size_t)n : sizeof(linebuf) - 1;
            tee(linebuf, len);
        }
    }

    LOG_UNLOCK();
}

/* Hexdump logging */
void iot_log_hex_dump(log_level_t level, const void *data, size_t size, const char *prefix)
{
    if (!log_is_level_enabled(level) || !data)
    {
        return;
    }

    const unsigned char *bytes = (const unsigned char *)data;
    char hex_line[80];
    char ascii_line[20];

    log_write(level, __FILE__, __LINE__, __func__, "%s (size: %zu bytes)",
              prefix ? prefix : "HEXDUMP", size);

    for (size_t i = 0; i < size; i += 16)
    {
        memset(hex_line, 0, sizeof(hex_line));
        memset(ascii_line, 0, sizeof(ascii_line));

        char *hex_ptr = hex_line;
        char *ascii_ptr = ascii_line;

        for (size_t j = 0; j < 16 && (i + j) < size; j++)
        {
            unsigned char byte = bytes[i + j];

            sprintf(hex_ptr, "%02x ", byte);
            hex_ptr += 3;

            *ascii_ptr++ = (byte >= 32 && byte <= 126) ? byte : '.';
        }

        while (hex_ptr < hex_line + 48)
        {
            *hex_ptr++ = ' ';
        }

        log_write(level, __FILE__, __LINE__, __func__, "%08zx: %-48s |%s|", i, hex_line, ascii_line);
    }
}

/* Compact single-line hex: "prefix AA BB CC DD" (no offset, no ASCII sidebar) */
void iot_log_hex_inline(log_level_t level, const void *data, size_t size, const char *prefix)
{
    if (!log_is_level_enabled(level) || !data || size == 0)
    {
        return;
    }

    const unsigned char *bytes = (const unsigned char *)data;
    /* 3 chars per byte ("XX ") + prefix + null, cap at reasonable buffer */
    size_t max_bytes = (size > 64) ? 64 : size;
    char buf[64 * 3 + 1];
    char *ptr = buf;

    for (size_t i = 0; i < max_bytes; i++)
    {
        sprintf(ptr, "%02x ", bytes[i]);
        ptr += 3;
    }
    if (ptr > buf)
    {
        *(ptr - 1) = '\0'; /* trim trailing space */
    }

    if (size > 64)
    {
        log_write(level, __FILE__, __LINE__, __func__, "%s %s ... (%zu more)",
                  prefix ? prefix : "", buf, size - 64);
    }
    else
    {
        log_write(level, __FILE__, __LINE__, __func__, "%s %s",
                  prefix ? prefix : "", buf);
    }
}
