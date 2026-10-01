/**
 * @file IOT_String.c
 * @brief Implementation of string and conversion utilities
 *
 * Part of iotcommon - foundation utilities with no IoT dependencies.
 */

#include "IOT_String.h"
#include "IOT_Memory.h"
#include "IOT_CommonLog.h"
#include <string.h>

static const char* TAG = "Common-String";

/*==============================================================================
 * Lookup Tables
 *============================================================================*/

static const char HEX_CHARS_LOWER[] = "0123456789abcdef";
static const char HEX_CHARS_UPPER[] = "0123456789ABCDEF";

/*==============================================================================
 * Safe String Functions
 *============================================================================*/

size_t Str_SafeCopy(char* dest, const char* src, size_t destSize)
{
    if (dest == NULL || destSize == 0) {
        return 0;
    }

    if (src == NULL) {
        dest[0] = '\0';
        return 0;
    }

    size_t srcLen = strlen(src);
    size_t copyLen = (srcLen < destSize - 1) ? srcLen : destSize - 1;

    memcpy(dest, src, copyLen);
    dest[copyLen] = '\0';

    return copyLen;
}

size_t Str_SafeCat(char* dest, const char* src, size_t destSize)
{
    if (dest == NULL || destSize == 0) {
        return 0;
    }

    size_t destLen = strlen(dest);
    if (destLen >= destSize - 1) {
        // Buffer already full
        return destLen;
    }

    if (src == NULL) {
        return destLen;
    }

    size_t srcLen = strlen(src);
    size_t remaining = destSize - destLen - 1;
    size_t copyLen = (srcLen < remaining) ? srcLen : remaining;

    memcpy(dest + destLen, src, copyLen);
    dest[destLen + copyLen] = '\0';

    return destLen + copyLen;
}

size_t Str_SafeLen(const char* str, size_t maxLen)
{
    if (str == NULL) {
        return 0;
    }

    size_t len = 0;
    while (len < maxLen && str[len] != '\0') {
        len++;
    }
    return len;
}

/*==============================================================================
 * Hex Conversion Functions
 *============================================================================*/

bool Str_BinToHex(const uint8_t* data, size_t dataLen, char* hexStr, size_t hexStrSize)
{
    if (data == NULL || hexStr == NULL) {
        return false;
    }

    // Need 2 chars per byte plus null terminator
    size_t requiredSize = dataLen * 2 + 1;
    if (hexStrSize < requiredSize) {
        COMMON_LOGE(TAG, "Hex buffer too small: need %zu, have %zu", requiredSize, hexStrSize);
        return false;
    }

    for (size_t i = 0; i < dataLen; i++) {
        hexStr[i * 2] = HEX_CHARS_LOWER[(data[i] >> 4) & 0x0F];
        hexStr[i * 2 + 1] = HEX_CHARS_LOWER[data[i] & 0x0F];
    }
    hexStr[dataLen * 2] = '\0';

    return true;
}

bool Str_BinToHexUpper(const uint8_t* data, size_t dataLen, char* hexStr, size_t hexStrSize)
{
    if (data == NULL || hexStr == NULL) {
        return false;
    }

    size_t requiredSize = dataLen * 2 + 1;
    if (hexStrSize < requiredSize) {
        COMMON_LOGE(TAG, "Hex buffer too small: need %zu, have %zu", requiredSize, hexStrSize);
        return false;
    }

    for (size_t i = 0; i < dataLen; i++) {
        hexStr[i * 2] = HEX_CHARS_UPPER[(data[i] >> 4) & 0x0F];
        hexStr[i * 2 + 1] = HEX_CHARS_UPPER[data[i] & 0x0F];
    }
    hexStr[dataLen * 2] = '\0';

    return true;
}

char* Str_BinToHexAlloc(const uint8_t* data, size_t dataLen, const char* tag)
{
    if (data == NULL || dataLen == 0) {
        return NULL;
    }

    size_t hexStrLen = dataLen * 2 + 1;
    char* hexStr = (char*)Mem_SafeMalloc(hexStrLen, tag, "hex string");
    if (hexStr == NULL) {
        return NULL;
    }

    if (!Str_BinToHex(data, dataLen, hexStr, hexStrLen)) {
        SAFE_FREE(hexStr);
        return NULL;
    }

    return hexStr;
}

/**
 * @brief Convert single hex character to nibble value
 * @return 0-15 on success, -1 on invalid character
 */
static int hexCharToNibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool Str_HexToBin(const char* hexStr, uint8_t* data, size_t dataSize, size_t* outLen)
{
    if (hexStr == NULL || data == NULL) {
        return false;
    }

    size_t hexLen = strlen(hexStr);

    // Must have even length
    if (hexLen % 2 != 0) {
        COMMON_LOGE(TAG, "Hex string has odd length: %zu", hexLen);
        return false;
    }

    size_t binLen = hexLen / 2;
    if (binLen > dataSize) {
        COMMON_LOGE(TAG, "Binary buffer too small: need %zu, have %zu", binLen, dataSize);
        return false;
    }

    for (size_t i = 0; i < binLen; i++) {
        int high = hexCharToNibble(hexStr[i * 2]);
        int low = hexCharToNibble(hexStr[i * 2 + 1]);

        if (high < 0 || low < 0) {
            COMMON_LOGE(TAG, "Invalid hex character at position %zu", i * 2 + (high < 0 ? 0 : 1));
            return false;
        }

        data[i] = (uint8_t)((high << 4) | low);
    }

    if (outLen != NULL) {
        *outLen = binLen;
    }

    return true;
}

bool Str_IsValidHex(const char* str)
{
    if (str == NULL) {
        return false;
    }

    while (*str != '\0') {
        if (hexCharToNibble(*str) < 0) {
            return false;
        }
        str++;
    }

    return true;
}

/*==============================================================================
 * Checksum Functions
 *============================================================================*/

uint8_t Str_Checksum8(const uint8_t* data, size_t len)
{
    if (data == NULL || len == 0) {
        return 0;
    }

    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

uint16_t Str_Checksum16(const uint8_t* data, size_t len)
{
    if (data == NULL || len == 0) {
        return 0;
    }

    uint16_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

uint8_t Str_ChecksumXor(const uint8_t* data, size_t len)
{
    if (data == NULL || len == 0) {
        return 0;
    }

    uint8_t xorSum = 0;
    for (size_t i = 0; i < len; i++) {
        xorSum ^= data[i];
    }
    return xorSum;
}

/*==============================================================================
 * Integer to String Conversion
 *============================================================================*/

size_t Str_U32ToDecimal(uint32_t value, char* str, size_t strSize)
{
    if (str == NULL || strSize == 0) {
        return 0;
    }

    // Maximum digits for uint32_t is 10 (4294967295) + null
    char temp[11];
    int pos = 10;
    temp[pos] = '\0';

    if (value == 0) {
        temp[--pos] = '0';
    } else {
        while (value > 0 && pos > 0) {
            temp[--pos] = '0' + (value % 10);
            value /= 10;
        }
    }

    size_t len = 10 - pos;
    if (len >= strSize) {
        str[0] = '\0';
        return 0;
    }

    memcpy(str, &temp[pos], len + 1);
    return len;
}

size_t Str_U32ToHex(uint32_t value, char* str, size_t strSize, uint8_t minDigits)
{
    if (str == NULL || strSize == 0) {
        return 0;
    }

    // Maximum 8 hex digits for uint32_t + null
    char temp[9];
    int pos = 8;
    temp[pos] = '\0';

    if (value == 0) {
        temp[--pos] = '0';
    } else {
        while (value > 0 && pos > 0) {
            temp[--pos] = HEX_CHARS_LOWER[value & 0x0F];
            value >>= 4;
        }
    }

    // Pad with zeros if needed
    while ((8 - pos) < minDigits && pos > 0) {
        temp[--pos] = '0';
    }

    size_t len = 8 - pos;
    if (len >= strSize) {
        str[0] = '\0';
        return 0;
    }

    memcpy(str, &temp[pos], len + 1);
    return len;
}

/*==============================================================================
 * Base64 Encoding/Decoding
 *============================================================================*/

static const char BASE64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief Decode single Base64 character to 6-bit value
 * @return 0-63 on success, -1 on invalid character
 */
static int base64CharToValue(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return 0;  // Padding
    return -1;
}

size_t Str_Base64EncodedSize(size_t inputLen)
{
    // 4 output chars for every 3 input bytes, rounded up, plus null terminator
    return 4 * ((inputLen + 2) / 3) + 1;
}

size_t Str_Base64DecodedMaxSize(size_t inputLen)
{
    // 3 output bytes for every 4 input chars
    return (inputLen / 4) * 3;
}

size_t Str_Base64EncodeBuffer(const uint8_t* input, size_t inputLen, char* output, size_t outputSize)
{
    if (input == NULL || output == NULL || outputSize == 0) {
        return 0;
    }

    size_t requiredSize = Str_Base64EncodedSize(inputLen);
    if (outputSize < requiredSize) {
        return 0;
    }

    size_t i = 0, j = 0;
    while (i < inputLen) {
        size_t remain = inputLen - i;

        uint32_t a = input[i++];
        uint32_t b = (remain > 1) ? input[i++] : 0;
        uint32_t c = (remain > 2) ? input[i++] : 0;

        uint32_t triple = (a << 16) | (b << 8) | c;

        output[j++] = BASE64_CHARS[(triple >> 18) & 0x3F];
        output[j++] = BASE64_CHARS[(triple >> 12) & 0x3F];
        output[j++] = (remain > 1) ? BASE64_CHARS[(triple >> 6) & 0x3F] : '=';
        output[j++] = (remain > 2) ? BASE64_CHARS[triple & 0x3F] : '=';
    }

    output[j] = '\0';
    return j;
}

bool Str_Base64Encode(const uint8_t* input, size_t inputLen, char** output, size_t* outputLen)
{
    if (input == NULL || output == NULL) {
        return false;
    }

    *output = NULL;
    if (outputLen != NULL) {
        *outputLen = 0;
    }

    size_t encLen = Str_Base64EncodedSize(inputLen);
    char* encData = (char*)malloc(encLen);
    if (encData == NULL) {
        return false;
    }

    size_t written = Str_Base64EncodeBuffer(input, inputLen, encData, encLen);
    if (written == 0 && inputLen > 0) {
        free(encData);
        return false;
    }

    *output = encData;
    if (outputLen != NULL) {
        *outputLen = written;
    }

    return true;
}

bool Str_Base64Decode(const char* input, uint8_t** output, size_t* outputLen)
{
    if (input == NULL || output == NULL || outputLen == NULL) {
        return false;
    }

    *output = NULL;
    *outputLen = 0;

    size_t inputLen = strlen(input);

    // Base64 string must be multiple of 4
    if (inputLen == 0 || (inputLen % 4) != 0) {
        return false;
    }

    // Calculate output size (accounting for padding)
    size_t outLen = (inputLen / 4) * 3;
    if (input[inputLen - 1] == '=') outLen--;
    if (input[inputLen - 2] == '=') outLen--;

    uint8_t* decoded = (uint8_t*)malloc(outLen);
    if (decoded == NULL) {
        return false;
    }

    size_t i = 0, j = 0;
    while (i < inputLen) {
        int a = base64CharToValue(input[i++]);
        int b = base64CharToValue(input[i++]);
        int c = base64CharToValue(input[i++]);
        int d = base64CharToValue(input[i++]);

        if (a < 0 || b < 0 || c < 0 || d < 0) {
            free(decoded);
            return false;
        }

        uint32_t triple = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;

        if (j < outLen) decoded[j++] = (triple >> 16) & 0xFF;
        if (j < outLen) decoded[j++] = (triple >> 8) & 0xFF;
        if (j < outLen) decoded[j++] = triple & 0xFF;
    }

    *output = decoded;
    *outputLen = outLen;
    return true;
}
