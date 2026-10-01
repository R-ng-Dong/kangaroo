/**
 * @file IOT_String.h
 * @brief String and conversion utilities - safe string operations, hex conversion, checksums
 *
 * Provides safe string manipulation functions and data conversion utilities.
 *
 * Part of iotcommon - foundation utilities with no IoT dependencies.
 */

#ifndef IOT_STRING_H
#define IOT_STRING_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * Byte Order Macros
 *============================================================================*/

/** Extract high byte from 16-bit value */
#define HIGH_BYTE(val)              ((uint8_t)(((val) >> 8) & 0xFF))

/** Extract low byte from 16-bit value */
#define LOW_BYTE(val)               ((uint8_t)((val) & 0xFF))

/** Combine two bytes into 16-bit value (big-endian: high byte first) */
#define BYTES_TO_U16_BE(high, low)  ((uint16_t)(((uint16_t)(high) << 8) | (uint16_t)(low)))

/** Combine two bytes into 16-bit value (little-endian: low byte first) */
#define BYTES_TO_U16_LE(low, high)  ((uint16_t)(((uint16_t)(high) << 8) | (uint16_t)(low)))

/** Combine four bytes into 32-bit value (big-endian) */
#define BYTES_TO_U32_BE(b3, b2, b1, b0) \
    ((uint32_t)(((uint32_t)(b3) << 24) | ((uint32_t)(b2) << 16) | ((uint32_t)(b1) << 8) | (uint32_t)(b0)))

/** Extract byte N from 32-bit value (0 = LSB, 3 = MSB) */
#define U32_BYTE(val, n)            ((uint8_t)(((val) >> ((n) * 8)) & 0xFF))

/*==============================================================================
 * Bit Manipulation Macros
 *============================================================================*/

/** Set bit at position */
#define BIT_SET(val, bit)           ((val) |= (1U << (bit)))

/** Clear bit at position */
#define BIT_CLEAR(val, bit)         ((val) &= ~(1U << (bit)))

/** Toggle bit at position */
#define BIT_TOGGLE(val, bit)        ((val) ^= (1U << (bit)))

/** Check if bit is set (returns 0 or 1) */
#define BIT_CHECK(val, bit)         (((val) >> (bit)) & 1U)

/** Create bitmask for single bit */
#define BIT_MASK(bit)               (1U << (bit))

/** Create bitmask for range of bits */
#define BIT_RANGE_MASK(high, low)   (((1U << ((high) - (low) + 1)) - 1) << (low))

/*==============================================================================
 * Safe String Functions
 *============================================================================*/

/**
 * @brief Safely copy string with guaranteed null termination
 *
 * Unlike strncpy, this ALWAYS null-terminates and doesn't pad with zeros.
 *
 * @param dest Destination buffer
 * @param src Source string
 * @param destSize Size of destination buffer (including space for null)
 * @return Number of characters copied (excluding null terminator)
 *
 * @example
 *     char buf[10];
 *     Str_SafeCopy(buf, "Hello World", sizeof(buf));  // buf = "Hello Wor"
 */
size_t Str_SafeCopy(char* dest, const char* src, size_t destSize);

/**
 * @brief Safely concatenate strings with guaranteed null termination
 *
 * @param dest Destination buffer (must be null-terminated)
 * @param src Source string to append
 * @param destSize Total size of destination buffer
 * @return Total length of resulting string (may be truncated)
 */
size_t Str_SafeCat(char* dest, const char* src, size_t destSize);

/**
 * @brief Get string length with maximum limit (safe strlen)
 *
 * @param str String to measure
 * @param maxLen Maximum length to check
 * @return Length of string, or maxLen if no null found within maxLen
 */
size_t Str_SafeLen(const char* str, size_t maxLen);

/*==============================================================================
 * Hex Conversion Functions
 *============================================================================*/

/**
 * @brief Convert binary data to lowercase hexadecimal string
 *
 * @param data Binary data to convert
 * @param dataLen Length of binary data in bytes
 * @param hexStr Output buffer for hex string
 * @param hexStrSize Size of output buffer (must be >= dataLen*2+1)
 * @return true on success, false if buffer too small or NULL params
 *
 * @example
 *     uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
 *     char hex[9];
 *     Str_BinToHex(data, 4, hex, sizeof(hex));  // hex = "deadbeef"
 */
bool Str_BinToHex(const uint8_t* data, size_t dataLen, char* hexStr, size_t hexStrSize);

/**
 * @brief Convert binary data to uppercase hexadecimal string
 *
 * @param data Binary data to convert
 * @param dataLen Length of binary data in bytes
 * @param hexStr Output buffer for hex string
 * @param hexStrSize Size of output buffer (must be >= dataLen*2+1)
 * @return true on success, false if buffer too small or NULL params
 */
bool Str_BinToHexUpper(const uint8_t* data, size_t dataLen, char* hexStr, size_t hexStrSize);

/**
 * @brief Convert binary data to hex string with dynamic allocation
 *
 * @param data Binary data to convert
 * @param dataLen Length of binary data
 * @param tag Log tag for allocation errors
 * @return Newly allocated hex string (caller must free), or NULL on failure
 */
char* Str_BinToHexAlloc(const uint8_t* data, size_t dataLen, const char* tag);

/**
 * @brief Convert hexadecimal string to binary data
 *
 * Accepts both uppercase and lowercase hex characters.
 *
 * @param hexStr Hexadecimal string (must have even length)
 * @param data Output buffer for binary data
 * @param dataSize Size of output buffer
 * @param outLen Actual bytes written (can be NULL if not needed)
 * @return true on success, false on invalid input or buffer too small
 *
 * @example
 *     uint8_t data[4];
 *     size_t len;
 *     Str_HexToBin("DEADBEEF", data, sizeof(data), &len);  // data = {0xDE,0xAD,0xBE,0xEF}, len = 4
 */
bool Str_HexToBin(const char* hexStr, uint8_t* data, size_t dataSize, size_t* outLen);

/**
 * @brief Check if string contains only valid hexadecimal characters
 *
 * @param str String to check
 * @return true if all characters are 0-9, a-f, or A-F
 */
bool Str_IsValidHex(const char* str);

/*==============================================================================
 * Checksum Functions
 *============================================================================*/

/**
 * @brief Calculate 8-bit additive checksum
 *
 * @param data Data to checksum
 * @param len Length of data
 * @return 8-bit checksum (sum of all bytes, wrapping on overflow)
 */
uint8_t Str_Checksum8(const uint8_t* data, size_t len);

/**
 * @brief Calculate 16-bit additive checksum
 *
 * @param data Data to checksum
 * @param len Length of data
 * @return 16-bit checksum
 */
uint16_t Str_Checksum16(const uint8_t* data, size_t len);

/**
 * @brief Calculate XOR checksum
 *
 * @param data Data to checksum
 * @param len Length of data
 * @return XOR of all bytes
 */
uint8_t Str_ChecksumXor(const uint8_t* data, size_t len);

/*==============================================================================
 * Integer to String Conversion
 *============================================================================*/

/**
 * @brief Convert unsigned integer to decimal string
 *
 * @param value Value to convert
 * @param str Output buffer
 * @param strSize Size of output buffer
 * @return Number of characters written (excluding null), or 0 on error
 */
size_t Str_U32ToDecimal(uint32_t value, char* str, size_t strSize);

/**
 * @brief Convert unsigned integer to hexadecimal string (no prefix)
 *
 * @param value Value to convert
 * @param str Output buffer
 * @param strSize Size of output buffer
 * @param minDigits Minimum digits (zero-padded), 0 for no padding
 * @return Number of characters written (excluding null), or 0 on error
 */
size_t Str_U32ToHex(uint32_t value, char* str, size_t strSize, uint8_t minDigits);

/*==============================================================================
 * Base64 Encoding/Decoding
 *============================================================================*/

/**
 * @brief Encode binary data to Base64 string
 *
 * @param input Binary data to encode
 * @param inputLen Length of input data
 * @param output Pointer to receive allocated Base64 string (caller must free)
 * @param outputLen Pointer to receive output length (can be NULL)
 * @return true on success, false on allocation failure
 *
 * @example
 *     char *b64 = NULL;
 *     size_t len;
 *     if (Str_Base64Encode(data, dataLen, &b64, &len)) {
 *         // use b64...
 *         free(b64);
 *     }
 */
bool Str_Base64Encode(const uint8_t* input, size_t inputLen, char** output, size_t* outputLen);

/**
 * @brief Calculate required buffer size for Base64 encoding
 *
 * @param inputLen Length of input data
 * @return Required buffer size including null terminator
 */
size_t Str_Base64EncodedSize(size_t inputLen);

/**
 * @brief Encode binary data to Base64 string (pre-allocated buffer)
 *
 * @param input Binary data to encode
 * @param inputLen Length of input data
 * @param output Output buffer (must be at least Str_Base64EncodedSize(inputLen))
 * @param outputSize Size of output buffer
 * @return Number of characters written, or 0 on error
 */
size_t Str_Base64EncodeBuffer(const uint8_t* input, size_t inputLen, char* output, size_t outputSize);

/**
 * @brief Decode Base64 string to binary data
 *
 * @param input Base64 string to decode
 * @param output Pointer to receive allocated binary data (caller must free)
 * @param outputLen Pointer to receive output length
 * @return true on success, false on invalid input or allocation failure
 */
bool Str_Base64Decode(const char* input, uint8_t** output, size_t* outputLen);

/**
 * @brief Calculate maximum decoded size for Base64 string
 *
 * @param inputLen Length of Base64 string
 * @return Maximum decoded size (actual may be less due to padding)
 */
size_t Str_Base64DecodedMaxSize(size_t inputLen);

#ifdef __cplusplus
}
#endif

#endif /* IOT_STRING_H */
