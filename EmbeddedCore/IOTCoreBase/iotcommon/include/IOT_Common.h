/**
 * @file IOT_Common.h
 * @brief Umbrella header for iotcommon - foundation utilities
 *
 * Include this single header to get all iotcommon utilities.
 *
 * iotcommon provides generic, reusable utilities with NO dependencies
 * on other IoT modules. It sits at the bottom of the dependency tree
 * and can be used by any layer.
 *
 * Modules included:
 * - IOT_Memory: Safe memory allocation, SAFE_FREE macros, bounds checking
 * - IOT_String: Safe string ops, hex conversion, base64, checksums
 * - IOT_CommonLog: Optional logging callback (disabled by default)
 *
 * @example
 *     #include "IOT_Common.h"
 *
 *     void myFunc(void) {
 *         // Memory utilities
 *         uint8_t* buf = Mem_SafeMalloc(256, "MyModule", "buffer");
 *         SAFE_FREE(buf);
 *
 *         // String utilities
 *         char hex[9];
 *         uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
 *         Str_BinToHex(data, 4, hex, sizeof(hex));
 *     }
 */

#ifndef IOT_COMMON_H
#define IOT_COMMON_H

#include "IOT_Memory.h"
#include "IOT_String.h"
#include "IOT_CommonLog.h"

#endif /* IOT_COMMON_H */
