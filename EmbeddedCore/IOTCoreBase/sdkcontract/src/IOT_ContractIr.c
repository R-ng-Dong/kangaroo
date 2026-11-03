/*
 * The IR requests the contract owns: storing a device's keyset, and answering what IR this device
 * can do. Sending and learning drive the product's hardware and are forwarded by the router.
 */
#include "services/IOT_ContractIr.h"

#include <stddef.h>
#include "IOT_Common.h"
#include "IOT_Contract.h"
#include "IOT_Log.h"
#include "IOT_Memory.h"
#include "dao/IOT_DaoDeviceMgmt.h"
#include "dao/IOT_DaoStoresInternal.h"
#include "dao/IOT_DaoIRMgmt.h"

static const char *TAG = "ContractIr";

/* Upper bound on entries parsed from one keyset (a discrete remote has dozens); caps the
 * transient parse buffer against a malformed or oversized keyset. */
#define IR_KEYSET_MAX_ENTRIES 1024

/* Device protocol of an IR appliance whose keys are stored as protocol codes; any other IR
 * appliance stores compressed timing blobs. */
#define CONTRACT_IR_PROTOCOL_CODED 8

#define IR_CAP_MODE_TX_ONLY 0
#define IR_CAP_MODE_RX_ONLY 1
#define IR_CAP_MODE_TX_RX   2

/* Unconfigured answers as a device that declared no IR capability. */
static uint8_t s_capMode = IR_CAP_MODE_TX_ONLY;
static uint8_t s_capDetect = 0;

iot_err_t IOT_ContractIrConfigure(bool tx, bool rx, bool detect)
{
    s_capMode = rx ? (tx ? IR_CAP_MODE_TX_RX : IR_CAP_MODE_RX_ONLY) : IR_CAP_MODE_TX_ONLY;
    s_capDetect = detect ? 1 : 0;
    return IOT_OK;
}

/* Parse a keyset and persist it in one batched store. Entry pointers borrow from the keyset,
 * which stays valid for the synchronous store. */
static iot_err_t storeKeyset(uint16_t eid, const uint8_t *blob, uint16_t blobLen)
{
    if (blob == NULL || blobLen < 4)
        return IOT_ERR_INVALID_ARG;

    IOT_DevCommonInfo_t common = {0};
    bool isProtocol = false;
    if (IOT_DaoDeviceGetCommonInfo(eid, &common) == IOT_OK)
        isProtocol = (common.protocol == CONTRACT_IR_PROTOCOL_CODED);

    uint16_t maxEntries = blobLen / 4; /* smallest entry is a 4-byte header */
    if (maxEntries > IR_KEYSET_MAX_ENTRIES)
        maxEntries = IR_KEYSET_MAX_ENTRIES;

    IOT_IRKeysetEntry_t *entries =
        (IOT_IRKeysetEntry_t *) Mem_SafeMalloc(sizeof(IOT_IRKeysetEntry_t) * maxEntries, TAG, "irEntries");
    if (entries == NULL)
        return IOT_ERR_NO_MEM;

    uint16_t count = 0;
    uint16_t idx = 0;
    while (idx + 4 <= blobLen && count < maxEntries)
    {
        uint16_t dataName = BYTES_TO_U16_BE(blob[idx], blob[idx + 1]);
        uint16_t entryLen = BYTES_TO_U16_BE(blob[idx + 2], blob[idx + 3]) / 2;
        idx += 4;
        if ((uint32_t) idx + entryLen > blobLen)
        {
            IOT_LOGE(TAG, "Keyset: truncated entry %u len=%u at %u/%u", dataName, entryLen, idx, blobLen);
            break;
        }

        IOT_IRKeysetEntry_t *e = &entries[count++];
        e->dataName = dataName;
        if (dataName != IOT_IR_HEADER_DATANAME && isProtocol)
        {
            uint64_t code = 0;
            for (uint16_t i = 0; i < entryLen && i < 8; i++)
                code = (code << 8) | blob[idx + i];
            e->isProtocol = true;
            e->code = code;
            e->data = NULL;
            e->dataLen = 0;
        }
        else
        {
            e->isProtocol = false;
            e->code = 0;
            e->data = &blob[idx];
            e->dataLen = entryLen;
        }
        idx += entryLen;
    }

    iot_err_t err = (count > 0) ? IOT_DaoIRStoreKeyset(eid, entries, count) : IOT_ERR_INVALID_ARG;
    SAFE_FREE(entries);
    IOT_LOGI(TAG, "Keyset: EID=%04x parsed=%u proto=%d -> %s", eid, count, isProtocol, iot_err_to_name(err));
    return err;
}

iot_err_t IOT_ContractHandleIrEvent(IOT_CoreIrEvent_t *irEvent)
{
    if (irEvent == NULL)
        return IOT_ERR_INVALID_ARG;

    switch (irEvent->type)
    {
    case IOT_IR_EVENT_QUERY_CAPABILITY:
        irEvent->capMode = s_capMode;
        irEvent->capDetect = s_capDetect;
        return IOT_OK;
    case IOT_IR_EVENT_STORE_KEYSET:
        irEvent->result = storeKeyset(irEvent->eid, irEvent->data, irEvent->dataLen);
        return irEvent->result;
    default:
        return IOT_ERR_NOT_SUPPORTED;
    }
}
