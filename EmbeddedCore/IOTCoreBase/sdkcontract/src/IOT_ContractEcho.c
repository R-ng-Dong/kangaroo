/*
 * Echo recognition for sub-devices. See IOT_ContractEcho.h.
 */
#include "IOT_ContractEcho.h"

#include <string.h>
#include "IOT_Log.h"
#include "IOT_Osal.h"

#define TAG "ContractEcho"

typedef struct
{
    int64_t atUs;
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    uint16_t len;
    uint8_t value[IOT_CONTRACT_ECHO_MAX_VALUE];
    uint8_t senderFrom;
    bool valid;
} echo_note_t;

static echo_note_t s_ring[IOT_CONTRACT_ECHO_RING];
static uint8_t s_next;

/* The ring is written by the state command and read by the sub-device report, which can run on
 * different tasks. The critical sections copy a few small fields, so no blocking primitive is
 * needed, and every log call stays outside them. */
void IOT_ContractEchoNote(uint16_t eid, const uint16_t *elemIds, uint16_t numElems, uint16_t attrId,
                          const uint8_t *value, uint16_t len, uint8_t senderFrom)
{
    if (elemIds == NULL || numElems == 0 || value == NULL || len == 0)
        return;
    if (len > IOT_CONTRACT_ECHO_MAX_VALUE)
    {
        IOT_LOGW(TAG, "EID %04x attr %04x: %u-byte value not remembered; its echo will be reported again", eid,
                 attrId, (unsigned) len);
        return;
    }

    int64_t now = IOT_OsalGetUptimeUs();
    for (uint16_t i = 0; i < numElems; i++)
    {
        IOT_OsalEnterCritical();
        echo_note_t *n = &s_ring[s_next];
        n->atUs = now;
        n->eid = eid;
        n->elemId = elemIds[i];
        n->attrId = attrId;
        n->len = len;
        memcpy(n->value, value, len);
        n->senderFrom = senderFrom;
        n->valid = true;
        s_next = (uint8_t) ((s_next + 1) % IOT_CONTRACT_ECHO_RING);
        IOT_OsalExitCritical();
    }
}

bool IOT_ContractEchoConsume(uint16_t eid, uint16_t elemId, uint16_t attrId, const uint8_t *value, uint16_t len,
                             uint8_t *outSenderFrom)
{
    if (value == NULL || len == 0 || len > IOT_CONTRACT_ECHO_MAX_VALUE)
        return false;

    int64_t now = IOT_OsalGetUptimeUs();
    bool found = false;
    uint8_t sender = 0;
    int64_t ageUs = 0;

    IOT_OsalEnterCritical();
    for (int i = 0; i < IOT_CONTRACT_ECHO_RING; i++)
    {
        echo_note_t *n = &s_ring[i];
        if (!n->valid || n->eid != eid || n->elemId != elemId || n->attrId != attrId)
            continue;
        int64_t age = now - n->atUs;
        if (age < 0 || age > (int64_t) IOT_CONTRACT_ECHO_WINDOW_MS * 1000)
        {
            n->valid = false; /* expired: can never match again */
            continue;
        }
        if (n->len != len || memcmp(n->value, value, len) != 0)
            continue; /* another value: the sub-device changed itself; the note may still be echoed */
        n->valid = false; /* consumed by its first match */
        found = true;
        sender = n->senderFrom;
        ageUs = age;
        break;
    }
    IOT_OsalExitCritical();
    (void) ageUs; /* read only by the log line, which a build may compile out */

    if (found)
    {
        IOT_LOGD(TAG, "EID %04x elem %04x attr %04x: echo of a change applied %u ms ago (sender %u)", eid, elemId,
                 attrId, (unsigned) (ageUs / 1000), (unsigned) sender);
        if (outSenderFrom != NULL)
            *outSenderFrom = sender;
    }
    return found;
}
