/**
 * @file IOT_Memory.h
 * @brief Memory management utilities - safe allocation, deallocation, and manipulation
 *
 * Provides safe wrappers for memory operations with built-in error logging,
 * overflow protection, and null-safety.
 *
 * Part of iotcommon - foundation utilities with no IoT dependencies.
 */

#ifndef IOT_MEMORY_H
#define IOT_MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * Configuration
 *============================================================================*/

/** Maximum allowed allocation size for safety checks (64 KB default) */
#ifndef COMMON_MAX_ALLOC_SIZE
#define COMMON_MAX_ALLOC_SIZE        (64 * 1024)
#endif

/** Maximum allowed string length */
#ifndef COMMON_MAX_STRING_LEN
#define COMMON_MAX_STRING_LEN        512
#endif

/*==============================================================================
 * Safe Free Macros
 *============================================================================*/

/**
 * @brief Safe free macro - frees pointer and sets to NULL
 *
 * Prevents double-free vulnerabilities and dangling pointer access.
 * Safe to call with NULL pointer.
 *
 * @param ptr Pointer to free (can be NULL)
 *
 * @example
 *     uint8_t *buffer = malloc(100);
 *     SAFE_FREE(buffer);  // buffer is now NULL
 *     SAFE_FREE(buffer);  // Safe - does nothing
 */
#define SAFE_FREE(ptr) do {     \
    if ((ptr) != NULL) {        \
        free(ptr);              \
        (ptr) = NULL;           \
    }                           \
} while(0)

/**
 * @brief Safe free for const pointers
 *
 * Use with caution - only when you know the memory was dynamically allocated.
 *
 * @param ptr Const pointer to free
 */
#define SAFE_FREE_CONST(ptr) do {           \
    if ((ptr) != NULL) {                    \
        free((void *)(uintptr_t)(ptr));     \
        (ptr) = NULL;                       \
    }                                       \
} while(0)

/**
 * @brief Zero memory before freeing (for sensitive data like keys/passwords)
 *
 * @param ptr Pointer to free
 * @param size Size of memory to zero
 */
#define SECURE_FREE(ptr, size) do {     \
    if ((ptr) != NULL) {                \
        memset((ptr), 0, (size));       \
        free(ptr);                      \
        (ptr) = NULL;                   \
    }                                   \
} while(0)

/*==============================================================================
 * Allocation Macros
 *============================================================================*/

/**
 * @brief Allocate and zero-initialize memory (type-safe calloc wrapper)
 *
 * @param type Type to allocate
 * @param count Number of elements
 * @return Pointer to allocated memory or NULL
 */
#define MEM_CALLOC(type, count) \
    ((type *)calloc((count), sizeof(type)))

/**
 * @brief Allocate memory for a single object
 *
 * @param type Type to allocate
 * @return Pointer to allocated memory or NULL
 */
#define MEM_ALLOC(type) \
    ((type *)malloc(sizeof(type)))

/**
 * @brief Allocate memory for an array
 *
 * @param type Type of elements
 * @param count Number of elements
 * @return Pointer to allocated memory or NULL
 */
#define MEM_ALLOC_ARRAY(type, count) \
    ((type *)malloc(sizeof(type) * (count)))

/*==============================================================================
 * Utility Macros
 *============================================================================*/

/** Get array element count (compile-time, not for pointers) */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

/** Get minimum of two values */
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/** Get maximum of two values */
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

/** Clamp value between min and max */
#define CLAMP(val, min_val, max_val) \
    (((val) < (min_val)) ? (min_val) : (((val) > (max_val)) ? (max_val) : (val)))

/*==============================================================================
 * Function Declarations
 *============================================================================*/

/**
 * @brief Safely allocate memory with logging on failure
 *
 * Features:
 * - Validates size > 0 and < COMMON_MAX_ALLOC_SIZE
 * - Logs error on failure with tag and description
 *
 * @param size Size in bytes to allocate
 * @param tag Log tag for error messages (e.g., module name)
 * @param desc Description of allocation for error messages
 * @return Pointer to allocated memory, or NULL on failure
 *
 * @example
 *     uint8_t *buf = Mem_SafeMalloc(256, "MyModule", "packet buffer");
 *     if (buf == NULL) return IOT_ERR_NO_MEM;
 */
void* Mem_SafeMalloc(size_t size, const char* tag, const char* desc);

/**
 * @brief Safely allocate and zero-initialize memory with logging
 *
 * Features:
 * - Checks for multiplication overflow (count * size)
 * - Validates total size < COMMON_MAX_ALLOC_SIZE
 * - Returns zero-initialized memory
 *
 * @param count Number of elements
 * @param size Size of each element
 * @param tag Log tag for error messages
 * @param desc Description of allocation for error messages
 * @return Pointer to zero-initialized memory, or NULL on failure
 */
void* Mem_SafeCalloc(size_t count, size_t size, const char* tag, const char* desc);

/**
 * @brief Safely reallocate memory with logging
 *
 * Note: On failure, the original pointer is NOT freed.
 *
 * @param ptr Existing pointer (can be NULL for new allocation)
 * @param size New size in bytes
 * @param tag Log tag for error messages
 * @param desc Description of allocation for error messages
 * @return Pointer to reallocated memory, or NULL on failure
 */
void* Mem_SafeRealloc(void* ptr, size_t size, const char* tag, const char* desc);

/**
 * @brief Duplicate a string safely
 *
 * @param src Source string to duplicate (NULL-safe)
 * @param tag Log tag for error messages
 * @param desc Description for error messages
 * @return Duplicated string (caller must free), or NULL on failure
 */
char* Mem_SafeStrdup(const char* src, const char* tag, const char* desc);

/**
 * @brief Duplicate a string with maximum length
 *
 * @param src Source string to duplicate
 * @param maxLen Maximum characters to copy (excluding null terminator)
 * @param tag Log tag for error messages
 * @param desc Description for error messages
 * @return Duplicated string (caller must free), or NULL on failure
 */
char* Mem_SafeStrndup(const char* src, size_t maxLen, const char* tag, const char* desc);

/**
 * @brief Check if pointer is valid (non-null and optionally aligned)
 *
 * @param ptr Pointer to check
 * @param alignment Required alignment (must be power of 2, use 1 for no check)
 * @return true if valid, false otherwise
 */
bool Mem_IsValidPointer(const void* ptr, size_t alignment);

/**
 * @brief Safely compare memory regions (NULL-safe)
 *
 * @param a First memory region
 * @param b Second memory region
 * @param size Size to compare
 * @return true if equal, false if different or if either pointer is NULL
 */
bool Mem_SafeCompare(const void* a, const void* b, size_t size);

/**
 * @brief Safely copy memory with bounds checking
 *
 * @param dest Destination buffer
 * @param destSize Size of destination buffer
 * @param src Source buffer
 * @param count Number of bytes to copy
 * @return true on success, false if would overflow
 */
bool Mem_SafeCopy(void* dest, size_t destSize, const void* src, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* IOT_MEMORY_H */
