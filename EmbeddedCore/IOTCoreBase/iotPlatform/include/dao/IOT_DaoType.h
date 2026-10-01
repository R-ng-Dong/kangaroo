#pragma once

#include <stdint.h>

typedef enum
{
    IOT_DAO_U8,
    IOT_DAO_U16,
    IOT_DAO_U32,
    IOT_DAO_U64,
    IOT_DAO_STRING,
    IOT_DAO_BLOB,
} IOT_DaoType;

/**
 * @brief One key/value entry for a batched storage write.
 *
 * Used by the batch-set APIs to write many keys under one namespace with a
 * single flash commit. Pointers are borrowed — the caller retains ownership for
 * the duration of the (blocking) batch call.
 */
typedef struct
{
    const char *key;   ///< Key name
    IOT_DaoType type;  ///< Value type
    const void *data;  ///< Pointer to the value (u8..u64 point at the scalar; blob/string at the bytes)
    uint16_t len;      ///< Byte length (used for BLOB)
} IOT_DaoBatchEntry_t;
