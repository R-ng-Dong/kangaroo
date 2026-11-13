/*
 * Record turned off (CONFIG_IOT_RECORD_ENABLED=n).
 *
 * The component's CMakeLists builds this file INSTEAD of IOT_Record.c and dao/IOT_DaoRecord.c, so
 * a product with no history pays none of Record's static RAM (the instance table, rules, overrides
 * and reply scratch). Every Record call the rest of the firmware makes still links and answers:
 * nothing is captured, nothing is stored, and a history request from the cloud gets the documented
 * "nothing to show" reply instead of silence, which the app would otherwise sit out as a timeout.
 *
 * Holds no static state on purpose -- that is the point of turning Record off.
 */
#include "IOT_Record.h"

#include <stddef.h>
#include "IOT_ContractRecord.h"
#include "IOT_CoreRecord.h"
#include "dao/IOT_DaoRecord.h"
#include "IOT_ContractAttrsInternal.h"

// ---- public API ------------------------------------------------------------------

/* Rules and overrides are validated exactly as with Record on, then dropped: a partner build
 * must not pass with Record off and fail once it is turned on. */
iot_err_t IOT_ContractRecordRegisterRule(const IOT_ContractRecordRule_t *rule)
{
    return IOT_RecordRuleIsValid(rule) ? IOT_OK : IOT_ERR_INVALID_ARG;
}

iot_err_t IOT_ContractRecordClearRules(void)
{
    return IOT_OK;
}

iot_err_t IOT_ContractRecordSetElementEnabled(uint16_t eid, uint16_t elemId, uint16_t attrId, bool enabled)
{
    (void) enabled;
    return IOT_RecordOverrideIdsAreValid(eid, elemId, attrId) ? IOT_OK : IOT_ERR_INVALID_ARG;
}

uint8_t IOT_ContractRecordLogValueWidth(uint16_t deviceType, uint16_t elemType, uint16_t attrId)
{
    (void) deviceType;
    (void) elemType;
    (void) attrId;
    return IOT_CONTRACT_RECORD_DEFAULT_LOG_VALUE_BYTES;
}

iot_err_t IOT_ContractRecordGetCurrentBlocks(uint16_t eid, IOT_CoreRecordStateBlock_t *outBlocks,
                                             uint8_t maxBlocks, uint8_t *outCount)
{
    (void) eid;
    (void) maxBlocks;
    if (outBlocks == NULL || outCount == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }
    *outCount = 0;
    return IOT_OK;
}

iot_err_t IOT_ContractRecordServeGet(uint16_t eid, uint16_t elemId, uint16_t attrId,
                                     uint64_t checkpoint, const IOT_RequestContext_t *ctx)
{
    if (ctx == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }
    /* The same reply IOT_Record.c sends for an instance it never recorded: ONE block echoing the
     * requested checkpoint, no entries, end of chain. Core rejects zero blocks, so an empty list
     * would mean no reply at all. On the stack: 26 bytes, and only for the call. */
    const IOT_CoreRecordBlock_t empty = {
        .checkpoint = checkpoint,
        .prevCheckpoint = 0,
        .fields = NULL,
        .fieldCount = 0,
    };
    const IOT_CoreRecordResponse_t response = {
        .eid = eid,
        .elemId = elemId,
        .attrId = attrId,
        .valueCount = IOT_AttrRecordValueCount(attrId),
        .blocks = &empty,
        .blockCount = 1,
        .ctx = *ctx,
    };
    return IOT_CoreRequestRecordResponse(&response);
}

// ---- internal Record (IOT_Record.h) -------------------------------------------------

iot_err_t IOT_RecordInit(void)
{
    return IOT_OK;
}

iot_err_t IOT_RecordSetAppendOverride(IOT_ContractRecordAppendOverrideFn fn)
{
    (void) fn;
    return IOT_OK;
}

bool IOT_RecordHasAppendOverride(void)
{
    return false; /* the override is ignored with Record off */
}

iot_err_t IOT_RecordSetElementIdResolver(IOT_ContractRecordElemIdResolverFn fn)
{
    (void) fn;
    return IOT_OK;
}

bool IOT_RecordShouldCapture(const IOT_ContractRecordEvent_t *event)
{
    (void) event;
    return false;
}

iot_err_t IOT_RecordAppend(const IOT_ContractRecordEvent_t *event)
{
    (void) event;
    return IOT_OK;
}

iot_err_t IOT_RecordFlushAll(void)
{
    return IOT_OK;
}

iot_err_t IOT_RecordFlushAllSync(void)
{
    return IOT_OK;
}

void IOT_RecordPushKick(void)
{
}

// ---- Record store (dao/IOT_DaoRecord.h) ---------------------------------------------

/* Nothing was stored while Record is off. Namespaces a previous Record-on firmware left behind
 * stay until Record is turned back on (or the partition is erased): removing them needs the
 * store this build leaves out. */
iot_err_t IOT_DaoRecordRemoveDevice(uint16_t eid)
{
    (void) eid;
    return IOT_OK;
}
