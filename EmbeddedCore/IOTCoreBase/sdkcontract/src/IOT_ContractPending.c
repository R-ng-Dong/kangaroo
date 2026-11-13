#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "IOT_ContractPending.h"
#include "IOT_ContractCommit.h"
#include "IOT_ContractElements.h"
#include "IOT_Core.h"
#include "IOT_Contract.h"
#include "IOT_Log.h"
#include "IOT_Osal.h"

static const char *TAG = "ContractPending";

typedef struct
{
    bool used;
    uint16_t elemId;
    uint16_t attrId;
    uint8_t len;
    uint8_t value[IOT_CONTRACT_PENDING_VALUE_MAX];
} pending_slot_t;

static pending_slot_t s_slots[IOT_CONTRACT_PENDING_MAX];
static IOT_OsalMutexHandle_t s_mutex;

static bool s_flushed;

/* The lock is created by IOT_ContractPendingInit(), from IOT_ContractInit() while still
 * single-threaded. Creating it here is only a fallback for a report made before IOT_ContractStart():
 * no other task can touch the table then, so the creation cannot race. A failed create refuses the
 * hold rather than holding unlocked.
 *
 * The wait has no timeout. A flush holds the lock across up to IOT_CONTRACT_PENDING_MAX store
 * writes, which can outlast any fixed bound, and a Discard or Hold that gave up would break the
 * promise that a held value is never stored over a later report. Waiting cannot deadlock: the only
 * lock taken while this one is held is the Device store's (by the flush), and nothing holding the
 * store's lock comes back to this table. So the only failure left is a lock that cannot be created. */
static bool pendingLock(void)
{
    if (s_mutex == NULL)
    {
        s_mutex = IOT_OsalMutexCreate();
        if (s_mutex == NULL)
        {
            return false;
        }
    }
    return IOT_OsalMutexLock(s_mutex, UINT32_MAX) == IOT_OK;
}

static void pendingUnlock(void)
{
    IOT_OsalMutexUnlock(s_mutex);
}

iot_err_t IOT_ContractPendingHold(uint16_t elemId, uint16_t attrId, const uint8_t *value, uint16_t len)
{
    if (value == NULL || len == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }
    if (len > IOT_CONTRACT_PENDING_VALUE_MAX)
    {
        return IOT_ERR_INVALID_SIZE;
    }
    if (!pendingLock())
    {
        return IOT_ERR_NO_MEM;
    }
    if (s_flushed)
    {
        pendingUnlock();
        return IOT_ERR_INVALID_STATE;
    }

    pending_slot_t *target = NULL;
    pending_slot_t *freeSlot = NULL;
    for (int i = 0; i < IOT_CONTRACT_PENDING_MAX; i++)
    {
        pending_slot_t *slot = &s_slots[i];
        if (!slot->used)
        {
            if (freeSlot == NULL)
            {
                freeSlot = slot;
            }
        }
        else if (slot->elemId == elemId && slot->attrId == attrId)
        {
            target = slot;
            break;
        }
    }
    if (target == NULL)
    {
        target = freeSlot;
    }

    iot_err_t result = IOT_OK;
    if (target == NULL)
    {
        IOT_LOGW(TAG, "holding table full, report of element %u attribute %u dropped", (unsigned) elemId,
                 (unsigned) attrId);
        result = IOT_ERR_NO_MEM;
    }
    else
    {
        target->used = true;
        target->elemId = elemId;
        target->attrId = attrId;
        target->len = (uint8_t) len;
        memcpy(target->value, value, len);
    }
    pendingUnlock();
    return result;
}

void IOT_ContractPendingInit(void)
{
    if (s_mutex == NULL)
    {
        s_mutex = IOT_OsalMutexCreate();
    }
}

void IOT_ContractPendingReset(void)
{
    if (pendingLock())
    {
        memset(s_slots, 0, sizeof(s_slots));
        s_flushed = false;
        pendingUnlock();
    }
}

bool IOT_ContractPendingFlushed(void)
{
    bool flushed = false;
    if (pendingLock())
    {
        flushed = s_flushed;
        pendingUnlock();
    }
    return flushed;
}

void IOT_ContractPendingDiscard(uint16_t elemId, uint16_t attrId)
{
    if (!pendingLock())
    {
        return;
    }
    for (int i = 0; i < IOT_CONTRACT_PENDING_MAX; i++)
    {
        if (s_slots[i].used && s_slots[i].elemId == elemId && s_slots[i].attrId == attrId)
        {
            s_slots[i].used = false;
        }
    }
    pendingUnlock();
}

/* The whole drain runs under the pending lock, store writes included, so a report for a held key
 * cannot land between "take the slot" and "store it" (a report discards its key first, under the
 * same lock). Lock order is pending lock -> Device store, never the reverse: the report path
 * releases the pending lock before it calls IOT_ContractLocalChange(), and nothing under the store
 * calls back into this table, so it cannot deadlock. A concurrent Hold or Discard waits for the
 * whole drain, however long it takes. */
void IOT_ContractPendingFlush(uint16_t rootEid)
{
    if (!pendingLock())
    {
        return;
    }
    s_flushed = true;
    for (int i = 0; i < IOT_CONTRACT_PENDING_MAX; i++)
    {
        pending_slot_t *slot = &s_slots[i];
        if (!slot->used)
        {
            continue;
        }
        slot->used = false; /* stored or dropped, the slot is free either way */

        uint16_t cloudElemId = slot->elemId;
        if (!IOT_ContractElemDeviceToCloud(rootEid, slot->elemId, &cloudElemId))
        {
            IOT_LOGW(TAG, "held value of EID %04x element %u attribute %04x has no cloud element, dropped",
                     (unsigned) rootEid, (unsigned) slot->elemId, (unsigned) slot->attrId);
            continue;
        }
        IOT_ContractCommit_t commit = {
            .eid = rootEid,
            .elemIds = &cloudElemId,
            .numElems = 1,
            .attrId = slot->attrId,
            .value = slot->value,
            .valueLen = slot->len,
            .senderFrom = IOT_SENDER_DEVICE,
        };
        iot_err_t err = IOT_ContractCommitStoreOnly(&commit);
        if (err != IOT_OK)
        {
            IOT_LOGW(TAG, "held value of EID %04x element %u attribute %04x not stored (%d), dropped",
                     (unsigned) rootEid, (unsigned) slot->elemId, (unsigned) slot->attrId, (int) err);
        }
    }
    pendingUnlock();
}

uint8_t IOT_ContractPendingCount(void)
{
    uint8_t n = 0;
    if (!pendingLock())
    {
        return 0;
    }
    for (int i = 0; i < IOT_CONTRACT_PENDING_MAX; i++)
    {
        if (s_slots[i].used)
        {
            n++;
        }
    }
    pendingUnlock();
    return n;
}
