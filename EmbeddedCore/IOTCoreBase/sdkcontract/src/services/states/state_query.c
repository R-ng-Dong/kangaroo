/*
 * The contract's read-only STATE answers: GET, the history query, and the sensor-log empty reply.
 * See services/IOT_ContractStateQuery.h.
 */
#include "services/IOT_ContractStateQuery.h"

#include "IOT_Common.h"
#include "IOT_CoreReporter.h"
#include "IOT_CoreTimeRequest.h"
#include "IOT_Log.h"
#include "IOT_Record.h" /* ServeGet, GetCurrentBlocks */
#include "IOT_TimerMgmt.h"
#include "dao/IOT_DaoDeviceMgmt.h"

static const char *TAG = "ContractStateQuery";

iot_err_t IOT_ContractStateRespondGet(IOT_EventStateGetRequest_t *getData)
{
    if (getData == NULL)
        return IOT_ERR_INVALID_ARG;

    /* Name the EID being asked for: without it an empty reply is indistinguishable from a
     * healthy one in the log, and an app showing a blank device has no error to point at. */
    IOT_LOGW(TAG, "STATE event: GET_STATE for EID %04x", getData->eid);

    IOT_DaoDeviceElemsAttrsInfo_t elemsStateInfo = {0};
    iot_err_t err = IOT_DaoDeviceGetDeviceElemsState(getData->eid, &elemsStateInfo);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to get device elements state from DAO for EID %04x", getData->eid);
        return err;
    }

    /* IOT_OK with nothing in it still sends a valid but empty reply. Say so. */
    if (elemsStateInfo.numOfElem == 0)
        IOT_LOGW(TAG, "GET_STATE: EID %04x has NO stored elements, replying empty", getData->eid);

    for (int i = 0; i < elemsStateInfo.numOfElem; i++)
    {
        for (int j = 0; j < elemsStateInfo.elemList[i].numOfAttr; j++)
        {
            IOT_LOGI(TAG, "Elem:%04x Attr:%04x Sz:%u", elemsStateInfo.elemList[i].elemId,
                     elemsStateInfo.elemList[i].attrList[j].attrId, elemsStateInfo.elemList[i].attrList[j].attrSize);
            iot_log_hex_inline(LOG_LEVEL_INFO, elemsStateInfo.elemList[i].attrList[j].attrValue,
                               elemsStateInfo.elemList[i].attrList[j].attrSize, "  Val:");
        }
    }

    /* The time block is OPTIONAL; the state is not. A device that has not reached its time
     * server yet omits the block and still answers the question that was asked. The request is
     * judged by value with core's own predicate (IOT_CoreTimeRequested), not by pointer. */
    uint8_t *unixTime = getData->unixTime;
    if (IOT_CoreTimeRequested(unixTime) && !IOT_TimerIsTimeSynced())
    {
        IOT_LOGW(TAG,
                 "GET_STATE for EID %04x asked for the time but the clock is not synced; answering with state "
                 "and omitting the time block",
                 getData->eid);
        unixTime = NULL;
    }

    IOT_CoreStateResponse_t response = {
        .eid = getData->eid,
        .fullState = (devStateElemsInfo_t *) &elemsStateInfo,
        .ctx = getData->ctx,
        .unixTime = unixTime,
    };

    /* The app's first history checkpoint, handed over with the state rather than costing it a
     * second round trip. Record omits anything still provisional. */
    IOT_CoreRecordStateBlock_t recordBlocks[IOT_CORE_RECORD_STATE_BLOCKS_MAX];
    uint8_t recordBlockCount = 0;
    if (IOT_ContractRecordGetCurrentBlocks(getData->eid, recordBlocks, IOT_CORE_RECORD_STATE_BLOCKS_MAX,
                                           &recordBlockCount) == IOT_OK &&
        recordBlockCount > 0)
    {
        response.recordBlocks = recordBlocks;
        response.numRecordBlocks = recordBlockCount;
    }

    err = IOT_CoreRequestReportState(&response);
    if (err != IOT_OK)
        IOT_LOGE(TAG, "Failed to send state response");

    IOT_DaoDeviceFreeElemsInfo(&elemsStateInfo);
    return err;
}

iot_err_t IOT_ContractStateRespondRecord(IOT_EventStateRecordRequest_t *recordData)
{
    if (recordData == NULL)
        return IOT_ERR_INVALID_ARG;

    IOT_LOGI(TAG, "RECORD query: EID=%04x elem=%04x attr=%04x checkpoint=0x%08lx%08lx", recordData->eid,
             recordData->elemId, recordData->attrId, (unsigned long) (recordData->checkpoint >> 32),
             (unsigned long) (recordData->checkpoint & 0xFFFFFFFFu));

    /* Synchronous, inside the event callback: ctx.senderId points into core's message buffer and
     * is freed when the handler returns, so a reply built on another task would route through
     * freed memory. */
    const iot_err_t err = IOT_ContractRecordServeGet(recordData->eid, recordData->elemId, recordData->attrId,
                                                     recordData->checkpoint, &recordData->ctx);
    if (err != IOT_OK)
        IOT_LOGE(TAG, "Failed to send record response: %s", iot_err_to_name(err));
    return err;
}

iot_err_t IOT_ContractStateRespondSensorLogEmpty(IOT_EventStateSensorLogRequest_t *logData)
{
    if (logData == NULL)
        return IOT_ERR_INVALID_ARG;

    IOT_LOGI(TAG, "SENSOR_LOG query: EID=%04x elem=%04x attr=%04x year=%d day=%d (no sensor log kept)",
             logData->eid, logData->elemId, logData->attrId, logData->year, logData->dayOfYear);

    /* The value width is Record's, from the same device and element type its rules match on. */
    uint16_t elemType = 0;
    if (IOT_DaoDeviceGetElementType(logData->eid, logData->elemId, &elemType) != IOT_OK)
        IOT_LOGW(TAG, "No element type for EID %04x elem %04x; sensor log width falls back to the default",
                 logData->eid, logData->elemId);

    uint16_t deviceType = 0;
    IOT_DevCommonInfo_t commonInfo = {0};
    if (IOT_DaoDeviceGetCommonInfo(logData->eid, &commonInfo) == IOT_OK)
        deviceType = commonInfo.deviceType;

    const uint8_t stride = IOT_ContractRecordLogValueWidth(deviceType, elemType, logData->attrId);

    /* No entries: an empty log is an answer, not an error. */
    const IOT_CoreSensorLogResponse_t response = {
        .eid = logData->eid,
        .elemId = logData->elemId,
        .attrId = logData->attrId,
        .year = logData->year,
        .dayOfYear = logData->dayOfYear,
        .minutes = NULL,
        .values = NULL,
        .count = 0,
        .valueStride = stride,
        .logValueBytes = stride,
        .ctx = logData->ctx,
    };

    const iot_err_t err = IOT_CoreRequestSensorLogResponse(&response);
    if (err != IOT_OK)
        IOT_LOGE(TAG, "Failed to send sensor log response");
    return err;
}
