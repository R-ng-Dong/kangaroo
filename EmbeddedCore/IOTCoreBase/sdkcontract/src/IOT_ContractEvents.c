/*
 * The contract's events: the subscriber list and the raise calls. See IOT_ContractEvents.h.
 *
 * The list is filled at boot, before core starts delivering events, and only read afterwards, so
 * the raise path takes no lock. Every subscriber is called in subscription order with the same
 * const event.
 *
 * An event raised while the same task is inside a subscriber is dropped (logged), so a subscriber
 * that causes a contract action cannot recurse. The guard is per task, so another task's event
 * is still delivered meanwhile.
 */
#include "IOT_ContractEvents.h"

#include <stddef.h>
#include <string.h>
#include "IOT_Contract.h"
#include "IOT_ContractElements.h"
#include "IOT_Log.h"

static const char *TAG = "ContractEvents";

typedef struct
{
    IOT_ContractEventFn fn;
    void *arg;
} subscriber_t;

static subscriber_t s_subscribers[IOT_CONTRACT_EVENT_SUBSCRIBERS_MAX];
static uint8_t s_numSubscribers;

iot_err_t IOT_ContractSubscribe(IOT_ContractEventFn fn, void *arg)
{
    if (fn == NULL)
        return IOT_ERR_INVALID_ARG;
    if (s_numSubscribers >= IOT_CONTRACT_EVENT_SUBSCRIBERS_MAX)
    {
        IOT_LOGE(TAG, "Subscriber list full (%u)", (unsigned) IOT_CONTRACT_EVENT_SUBSCRIBERS_MAX);
        return IOT_ERR_NO_MEM;
    }
    s_subscribers[s_numSubscribers].fn = fn;
    s_subscribers[s_numSubscribers].arg = arg;
    s_numSubscribers++;
    return IOT_OK;
}

/* True while this task is inside a subscriber. Per task (thread-local), so the task that
 * delivers core events and the timer task can each be delivering at the same time. */
static _Thread_local bool s_delivering;

static void deliver(const IOT_ContractEvent_t *event)
{
    if (s_delivering)
    {
        IOT_LOGW(TAG, "Event %d raised from inside a subscriber: not delivered", (int) event->type);
        return;
    }
    s_delivering = true;
    for (uint8_t i = 0; i < s_numSubscribers; i++)
        s_subscribers[i].fn(event, s_subscribers[i].arg);
    s_delivering = false;
}

/* One element to the device's numbering; false when the product has no such element. */
static bool toDevice(uint16_t eid, uint16_t cloudElemId, uint16_t *outDeviceElemId)
{
    return IOT_ContractElemCloudToDevice(eid, cloudElemId, outDeviceElemId);
}

void IOT_ContractEventStateCommitted(uint16_t eid, const uint16_t *cloudElemIds, uint16_t numElems, uint16_t attrId,
                                     const uint8_t *value, uint16_t valueLen, uint8_t senderFrom,
                                     IOT_ContractCause_t cause)
{
    if (s_numSubscribers == 0 || cloudElemIds == NULL || numElems == 0)
        return;

    IOT_ContractEvent_t event;
    memset(&event, 0, sizeof(event));
    event.type = IOT_CONTRACT_EVT_STATE_COMMITTED;
    event.data.stateCommitted.eid = eid;
    event.data.stateCommitted.attrId = attrId;
    event.data.stateCommitted.value = value;
    event.data.stateCommitted.valueLen = valueLen;
    event.data.stateCommitted.source = IOT_ContractSourceFromSender(senderFrom);
    event.data.stateCommitted.cause = cause;

    /* The map is read once for the whole list. More elements than one event holds are split
     * over several events. */
    IOT_ContractElemMap_t map;
    IOT_ContractElemMapLoad(eid, &map);
    for (uint16_t i = 0; i < numElems; i++)
    {
        uint16_t deviceElemId;
        if (!IOT_ContractElemMapToDevice(&map, eid, cloudElemIds[i], &deviceElemId))
            continue;
        event.data.stateCommitted.elemIds[event.data.stateCommitted.numElems++] = deviceElemId;
        if (event.data.stateCommitted.numElems == IOT_CONTRACT_MAX_ELEMENTS)
        {
            deliver(&event);
            event.data.stateCommitted.numElems = 0;
        }
    }
    if (event.data.stateCommitted.numElems > 0)
        deliver(&event);
}

void IOT_ContractEventAutomationFired(uint16_t smid)
{
    if (s_numSubscribers == 0)
        return;
    IOT_ContractEvent_t event;
    memset(&event, 0, sizeof(event));
    event.type = IOT_CONTRACT_EVT_AUTOMATION_FIRED;
    event.data.automationFired.smid = smid;
    deliver(&event);
}

void IOT_ContractEventRecordBlockClosed(uint16_t eid, uint16_t cloudElemId, uint16_t attrId)
{
    if (s_numSubscribers == 0)
        return;
    IOT_ContractEvent_t event;
    memset(&event, 0, sizeof(event));
    event.type = IOT_CONTRACT_EVT_RECORD_BLOCK_CLOSED;
    event.data.recordBlockClosed.eid = eid;
    event.data.recordBlockClosed.attrId = attrId;
    if (!toDevice(eid, cloudElemId, &event.data.recordBlockClosed.elemId))
        return;
    deliver(&event);
}

void IOT_ContractEventRecordPushed(uint16_t eid, uint16_t cloudElemId, uint16_t attrId, uint16_t blockCount)
{
    if (s_numSubscribers == 0)
        return;
    IOT_ContractEvent_t event;
    memset(&event, 0, sizeof(event));
    event.type = IOT_CONTRACT_EVT_RECORD_PUSHED;
    event.data.recordPushed.eid = eid;
    event.data.recordPushed.attrId = attrId;
    event.data.recordPushed.blockCount = blockCount;
    if (!toDevice(eid, cloudElemId, &event.data.recordPushed.elemId))
        return;
    deliver(&event);
}

void IOT_ContractEventDeviceAdded(uint16_t eid)
{
    if (s_numSubscribers == 0)
        return;
    IOT_ContractEvent_t event;
    memset(&event, 0, sizeof(event));
    event.type = IOT_CONTRACT_EVT_DEVICE_ADDED;
    event.data.deviceAdded.eid = eid;
    deliver(&event);
}

void IOT_ContractEventDeviceRemoved(uint16_t eid, bool root)
{
    if (s_numSubscribers == 0)
        return;
    IOT_ContractEvent_t event;
    memset(&event, 0, sizeof(event));
    event.type = IOT_CONTRACT_EVT_DEVICE_REMOVED;
    event.data.deviceRemoved.eid = eid;
    event.data.deviceRemoved.root = root;
    deliver(&event);
}

void IOT_ContractEventMirrorRefused(uint16_t eid, uint16_t cloudElemId)
{
    if (s_numSubscribers == 0)
        return;
    IOT_ContractEvent_t event;
    memset(&event, 0, sizeof(event));
    event.type = IOT_CONTRACT_EVT_MIRROR_REFUSED;
    event.data.mirrorRefused.eid = eid;
    if (!toDevice(eid, cloudElemId, &event.data.mirrorRefused.elemId))
        return;
    deliver(&event);
}
