#include "ble/IOT_BleAdvBuild.h"

#include <string.h>

/**
 * @brief Append one AD element, refusing to write past the end of the buffer
 *
 * @param buf     Destination buffer
 * @param bufSize Size of buf
 * @param pos     In/out write cursor
 * @param type    AD type byte
 * @param data    Element payload (may be NULL when dataLen is 0)
 * @param dataLen Element payload length
 *
 * @return true if the element was appended, false if it would not fit
 */
static bool appendAdElement(uint8_t *buf, uint8_t bufSize, uint8_t *pos, uint8_t type, const uint8_t *data,
                            uint8_t dataLen)
{
    /* [length][type][data...] — the length byte counts the type but not itself */
    uint32_t needed = (uint32_t) *pos + 2u + dataLen;
    if (needed > bufSize)
    {
        return false;
    }

    buf[(*pos)++] = (uint8_t) (dataLen + 1);
    buf[(*pos)++] = type;
    if (dataLen > 0)
    {
        memcpy(&buf[*pos], data, dataLen);
        *pos = (uint8_t) (*pos + dataLen);
    }
    return true;
}

iot_err_t IOT_BleAdvBuildPayload(const IOT_BleMgmtAdvData_t *adv, uint8_t *buf, uint8_t bufSize, uint8_t *outLen)
{
    if (adv == NULL || buf == NULL || outLen == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    uint8_t pos = 0;

    /* Flags */
    const uint8_t flags = IOT_BLE_AD_FLAGS_LE_GENERAL;
    if (!appendAdElement(buf, bufSize, &pos, IOT_BLE_AD_TYPE_FLAGS, &flags, 1))
    {
        return IOT_ERR_INVALID_ARG;
    }

    /* Complete list of 16-bit service UUIDs — stored big-endian, sent little-endian */
    if (adv->uuids != NULL && adv->numUuids > 0)
    {
        uint32_t uuidBytes = (uint32_t) adv->numUuids * 2u;
        if ((uint32_t) pos + 2u + uuidBytes > bufSize)
        {
            return IOT_ERR_INVALID_ARG;
        }

        buf[pos++] = (uint8_t) (uuidBytes + 1);
        buf[pos++] = IOT_BLE_AD_TYPE_UUID16_CMPL;
        for (uint8_t i = 0; i < adv->numUuids; i++)
        {
            buf[pos++] = adv->uuids[i][1];
            buf[pos++] = adv->uuids[i][0];
        }
    }

    /* Manufacturer Specific Data — company ID little-endian, then the payload */
    if (adv->manufactureData != NULL && adv->manufactureDataLen > 0)
    {
        uint32_t mfgBytes = (uint32_t) adv->manufactureDataLen + 2u;
        if ((uint32_t) pos + 2u + mfgBytes > bufSize)
        {
            return IOT_ERR_INVALID_ARG;
        }

        buf[pos++] = (uint8_t) (mfgBytes + 1);
        buf[pos++] = IOT_BLE_AD_TYPE_MANUFACTURER;
        buf[pos++] = (uint8_t) (adv->manufactureId & 0xFF);
        buf[pos++] = (uint8_t) ((adv->manufactureId >> 8) & 0xFF);
        memcpy(&buf[pos], adv->manufactureData, adv->manufactureDataLen);
        pos = (uint8_t) (pos + adv->manufactureDataLen);
    }

    *outLen = pos;
    return IOT_OK;
}

iot_err_t IOT_BleAdvBuildScanRsp(const IOT_BleMgmtAdvData_t *adv, uint8_t *buf, uint8_t bufSize, uint8_t *outLen)
{
    if (adv == NULL || buf == NULL || outLen == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    *outLen = 0;

    if (adv->name == NULL || adv->nameLen == 0)
    {
        return IOT_OK;
    }

    uint8_t pos = 0;
    if (!appendAdElement(buf, bufSize, &pos, IOT_BLE_AD_TYPE_NAME_CMPL, (const uint8_t *) adv->name, adv->nameLen))
    {
        return IOT_ERR_INVALID_ARG;
    }

    *outLen = pos;
    return IOT_OK;
}
