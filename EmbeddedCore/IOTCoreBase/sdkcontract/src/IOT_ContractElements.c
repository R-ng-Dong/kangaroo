/*
 * The contract's element map: cloud element ids <-> the device's own element ids.
 *
 * Cloud element ids are opaque labels, not 1..N: a list may start anywhere and need not be
 * contiguous. A product numbers its own elements and matches on them, so the two schemes are
 * joined by POSITION, the only thing they share. Without the join, a device whose cloud ids were
 * 19/20 would ask its hardware for element 19 and the stored state would disagree with the
 * hardware.
 */
#include "IOT_ContractElements.h"

#include <stddef.h>
#include "IOT_Core.h"
#include "IOT_CoreEventTypes.h"
#include "IOT_Log.h"
#include "IOT_Record.h"
#include "dao/IOT_DaoDeviceMgmt.h"

static const char *TAG = "ContractElements";

static uint16_t s_elemIds[IOT_CONTRACT_MAX_ELEMENTS];
static uint8_t s_numElements;

bool IOT_ContractElemJoin(uint16_t id, const uint16_t *fromIds, uint8_t fromCount, const uint16_t *toIds,
                          uint8_t toCount, uint16_t *outId)
{
    *outId = id;
    if (fromIds == NULL || fromCount == 0 || toIds == NULL || toCount == 0)
        return false;

    for (uint8_t i = 0; i < fromCount; i++)
    {
        if (fromIds[i] != id)
            continue;
        /* The first match decides. Past the end of the other list there is no element at this
         * position: report it rather than hand over a different one. */
        if (i >= toCount)
            return false;
        *outId = toIds[i];
        return true;
    }
    return false;
}

/* Record's resolver shape: the device's own id -> the cloud's, unchanged when it has none. */
static uint16_t recordElemIdResolver(uint16_t rootEid, uint16_t deviceElemId)
{
    uint16_t cloudElemId = deviceElemId;
    (void) IOT_ContractElemDeviceToCloud(rootEid, deviceElemId, &cloudElemId);
    return cloudElemId;
}

void IOT_ContractElementsDeclare(const uint16_t *ids, uint8_t count)
{
    if (ids == NULL)
        count = 0;
    if (count > IOT_CONTRACT_MAX_ELEMENTS)
    {
        IOT_LOGE(TAG, "Element count %u exceeds max %u -- clamping", (unsigned) count,
                 (unsigned) IOT_CONTRACT_MAX_ELEMENTS);
        count = IOT_CONTRACT_MAX_ELEMENTS;
    }

    for (uint8_t i = 0; i < count; i++)
        s_elemIds[i] = ids[i];
    s_numElements = count;

    if (count == 0)
        return;

    for (uint8_t i = 0; i < count; i++)
        IOT_LOGI(TAG, "  elem[%u]: id=%u", (unsigned) i, (unsigned) s_elemIds[i]);
    (void) IOT_RecordSetElementIdResolver(recordElemIdResolver);
}

const uint16_t *IOT_ContractElementsDeclared(uint8_t *outCount)
{
    if (outCount != NULL)
        *outCount = s_numElements;
    return s_elemIds;
}

/* Translation applies only to the root device, with elements declared and the cloud's list read.
 * Otherwise the id passes through (map->translate false): a sub-device's ids are its hub's, and
 * with nothing to join against, inventing an id would write state onto some other element. */
void IOT_ContractElemMapLoad(uint16_t eid, IOT_ContractElemMap_t *map)
{
    map->translate = false;
    map->cloudCount = 0;

    uint16_t rootEid = 0;
    if (s_numElements == 0 || IOT_CoreGetRootEid(&rootEid) != IOT_OK || rootEid != eid)
        return;

    IOT_DaoDeviceElemsAttrsInfo_t elems = {0};
    if (IOT_DaoDeviceGetDeviceElemsState(eid, &elems) == IOT_OK && elems.elemList != NULL)
    {
        for (uint16_t i = 0; i < elems.numOfElem && map->cloudCount < IOT_CONTRACT_MAX_ELEMENTS; i++)
            map->cloudIds[map->cloudCount++] = elems.elemList[i].elemId;
    }
    IOT_DaoDeviceFreeElemsInfo(&elems);
    map->translate = (map->cloudCount > 0);
}

bool IOT_ContractElemMapToDevice(const IOT_ContractElemMap_t *map, uint16_t eid, uint16_t cloudElemId,
                                 uint16_t *outDeviceElemId)
{
    *outDeviceElemId = cloudElemId;
    if (!map->translate)
        return true;

    bool found =
        IOT_ContractElemJoin(cloudElemId, map->cloudIds, map->cloudCount, s_elemIds, s_numElements, outDeviceElemId);
    if (!found)
    {
        IOT_LOGW(TAG, "eid %04x: cloud element %u has no declared device element (%u declared) -- skipped",
                 (unsigned) eid, (unsigned) cloudElemId, (unsigned) s_numElements);
    }
    return found;
}

bool IOT_ContractElemCloudToDevice(uint16_t eid, uint16_t cloudElemId, uint16_t *outDeviceElemId)
{
    IOT_ContractElemMap_t map;
    IOT_ContractElemMapLoad(eid, &map);
    return IOT_ContractElemMapToDevice(&map, eid, cloudElemId, outDeviceElemId);
}

bool IOT_ContractElemDeviceToCloud(uint16_t eid, uint16_t deviceElemId, uint16_t *outCloudElemId)
{
    *outCloudElemId = deviceElemId;

    IOT_ContractElemMap_t map;
    IOT_ContractElemMapLoad(eid, &map);
    if (!map.translate)
        return true;

    bool found =
        IOT_ContractElemJoin(deviceElemId, s_elemIds, s_numElements, map.cloudIds, map.cloudCount, outCloudElemId);
    if (!found)
    {
        IOT_LOGW(TAG, "eid %04x: device element %u has no cloud element (%u in the cloud's model) -- id kept",
                 (unsigned) eid, (unsigned) deviceElemId, (unsigned) map.cloudCount);
    }
    return found;
}

/* The public read: the same device -> cloud join as a local change, without its warning, since a
 * product may look its elements up on every read. */
uint16_t IOT_ContractElementStoreId(uint16_t eid, uint16_t elemId)
{
    IOT_ContractElemMap_t map;
    IOT_ContractElemMapLoad(eid, &map);
    if (!map.translate)
        return elemId;

    uint16_t storeId = elemId;
    (void) IOT_ContractElemJoin(elemId, s_elemIds, s_numElements, map.cloudIds, map.cloudCount, &storeId);
    return storeId;
}

IOT_ContractSource_t IOT_ContractSourceFromSender(uint8_t senderFrom)
{
    /* Cloud senders, split by who acted. */
    switch (senderFrom)
    {
    case IOT_SENDER_APP:
    case IOT_SENDER_IOS:
    case IOT_SENDER_ANDROID:
        return IOT_CONTRACT_SOURCE_APP_LOCAL;

    case IOT_SENDER_DEVICE:
    case IOT_SENDER_DEVICE_SYNC:
        return IOT_CONTRACT_SOURCE_DEVICE;

    case IOT_SENDER_CLOUD:
    case IOT_SENDER_THIRD_PARTY:
        return IOT_CONTRACT_SOURCE_CLOUD;

    case IOT_SENDER_GOOGLE_VOICE:
    case IOT_SENDER_ALEXA:
    case IOT_SENDER_OLLI:
        return IOT_CONTRACT_SOURCE_VOICE;

    case IOT_SENDER_AUTOMATION:
    case IOT_SENDER_AUTOMATION_UUID_V4:
    case IOT_SENDER_SCENARIO_UUID_V4:
        return IOT_CONTRACT_SOURCE_AUTOMATION;

    case IOT_SENDER_SCHEDULE:
    case IOT_SENDER_SCHEDULE_UUID_V4:
    case IOT_SENDER_COUNTDOWN:
        return IOT_CONTRACT_SOURCE_SCHEDULE;

    case IOT_SENDER_CONTROL:
    default:
        return IOT_CONTRACT_SOURCE_OTHER;
    }
}
