/**
 * @file IOT_Memory.c
 * @brief Implementation of memory management utilities
 *
 * Part of iotcommon - foundation utilities with no IoT dependencies.
 */

#include "IOT_Memory.h"
#include "IOT_CommonLog.h"

static const char* TAG = "Common-Memory";

/*==============================================================================
 * Safe Allocation Functions
 *============================================================================*/

void* Mem_SafeMalloc(size_t size, const char* tag, const char* desc)
{
    const char* logTag = (tag != NULL) ? tag : TAG;
    const char* logDesc = (desc != NULL) ? desc : "unknown";

    if (size == 0) {
        COMMON_LOGW(logTag, "Attempted to allocate 0 bytes for %s", logDesc);
        return NULL;
    }

    if (size > COMMON_MAX_ALLOC_SIZE) {
        COMMON_LOGE(logTag, "Allocation size %lu exceeds maximum %d for %s",
                    (unsigned long)size, COMMON_MAX_ALLOC_SIZE, logDesc);
        return NULL;
    }

    void* ptr = malloc(size);
    if (ptr == NULL) {
        COMMON_LOGE(logTag, "Failed to allocate %lu bytes for %s", (unsigned long)size, logDesc);
    }

    return ptr;
}

void* Mem_SafeCalloc(size_t count, size_t size, const char* tag, const char* desc)
{
    const char* logTag = (tag != NULL) ? tag : TAG;
    const char* logDesc = (desc != NULL) ? desc : "unknown";

    if (count == 0 || size == 0) {
        COMMON_LOGW(logTag, "Attempted to allocate 0 bytes for %s", logDesc);
        return NULL;
    }

    // Check for multiplication overflow
    size_t totalSize = count * size;
    if (totalSize / count != size) {
        COMMON_LOGE(logTag, "Allocation overflow for %s (count=%lu, size=%lu)",
                    logDesc, (unsigned long)count, (unsigned long)size);
        return NULL;
    }

    if (totalSize > COMMON_MAX_ALLOC_SIZE) {
        COMMON_LOGE(logTag, "Allocation size %lu exceeds maximum %d for %s",
                    (unsigned long)totalSize, COMMON_MAX_ALLOC_SIZE, logDesc);
        return NULL;
    }

    void* ptr = calloc(count, size);
    if (ptr == NULL) {
        COMMON_LOGE(logTag, "Failed to allocate %lu bytes for %s", (unsigned long)totalSize, logDesc);
    }

    return ptr;
}

void* Mem_SafeRealloc(void* ptr, size_t size, const char* tag, const char* desc)
{
    const char* logTag = (tag != NULL) ? tag : TAG;
    const char* logDesc = (desc != NULL) ? desc : "unknown";

    if (size == 0) {
        COMMON_LOGW(logTag, "Realloc to 0 bytes for %s - freeing", logDesc);
        free(ptr);
        return NULL;
    }

    if (size > COMMON_MAX_ALLOC_SIZE) {
        COMMON_LOGE(logTag, "Realloc size %lu exceeds maximum %d for %s",
                    (unsigned long)size, COMMON_MAX_ALLOC_SIZE, logDesc);
        return NULL;
    }

    void* newPtr = realloc(ptr, size);
    if (newPtr == NULL) {
        COMMON_LOGE(logTag, "Failed to reallocate to %lu bytes for %s", (unsigned long)size, logDesc);
        // Note: original ptr is NOT freed on failure - caller must handle
    }

    return newPtr;
}

/*==============================================================================
 * String Duplication Functions
 *============================================================================*/

char* Mem_SafeStrdup(const char* src, const char* tag, const char* desc)
{
    const char* logTag = (tag != NULL) ? tag : TAG;
    const char* logDesc = (desc != NULL) ? desc : "unknown";

    if (src == NULL) {
        COMMON_LOGW(logTag, "Attempted to strdup NULL for %s", logDesc);
        return NULL;
    }

    size_t len = strlen(src);
    if (len > COMMON_MAX_STRING_LEN) {
        COMMON_LOGE(logTag, "String length %lu exceeds maximum %d for %s",
                    (unsigned long)len, COMMON_MAX_STRING_LEN, logDesc);
        return NULL;
    }

    char* dup = (char*)malloc(len + 1);
    if (dup == NULL) {
        COMMON_LOGE(logTag, "Failed to duplicate string of length %lu for %s", (unsigned long)len, logDesc);
        return NULL;
    }

    memcpy(dup, src, len + 1);
    return dup;
}

char* Mem_SafeStrndup(const char* src, size_t maxLen, const char* tag, const char* desc)
{
    const char* logTag = (tag != NULL) ? tag : TAG;
    const char* logDesc = (desc != NULL) ? desc : "unknown";

    if (src == NULL) {
        COMMON_LOGW(logTag, "Attempted to strndup NULL for %s", logDesc);
        return NULL;
    }

    // Find actual length (up to maxLen)
    size_t len = 0;
    while (len < maxLen && src[len] != '\0') {
        len++;
    }

    char* dup = (char*)malloc(len + 1);
    if (dup == NULL) {
        COMMON_LOGE(logTag, "Failed to duplicate string of length %lu for %s", (unsigned long)len, logDesc);
        return NULL;
    }

    memcpy(dup, src, len);
    dup[len] = '\0';
    return dup;
}

/*==============================================================================
 * Validation and Comparison Functions
 *============================================================================*/

bool Mem_IsValidPointer(const void* ptr, size_t alignment)
{
    if (ptr == NULL) {
        return false;
    }

    // Check alignment (alignment must be power of 2)
    if (alignment > 1) {
        uintptr_t addr = (uintptr_t)ptr;
        if ((addr & (alignment - 1)) != 0) {
            return false;
        }
    }

    return true;
}

bool Mem_SafeCompare(const void* a, const void* b, size_t size)
{
    if (a == NULL || b == NULL) {
        return false;
    }

    if (size == 0) {
        return true;
    }

    return memcmp(a, b, size) == 0;
}

bool Mem_SafeCopy(void* dest, size_t destSize, const void* src, size_t count)
{
    if (dest == NULL || src == NULL) {
        return false;
    }

    if (count > destSize) {
        return false;
    }

    memcpy(dest, src, count);
    return true;
}
