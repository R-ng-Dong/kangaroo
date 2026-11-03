#include "IOT_ContractSenderCheck.h"
#include "IOT_ContractInternal.h"

#include <stddef.h>
#include "IOT_CoreEventTypes.h"
#include "IOT_Log.h"
#include "IOT_Osal.h"

#define TAG "ContractSender"

/* An automation's output reaches IOT_ContractStateChanged() within one request round trip; a
 * person pressing the same button a second later is a real manual change. */
#define SENDER_CHECK_WINDOW_MS 1000
#define SENDER_CHECK_RING 4

typedef struct
{
    int64_t atUs;
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    bool valid;
} dispatch_note_t;

static dispatch_note_t s_ring[SENDER_CHECK_RING];
static uint8_t s_next;

/* The ring is touched from Smart's dispatch (which can run in a timer callback) and from the
 * state-change entry on another task. The critical sections hold a few word-sized copies, so no
 * blocking primitive is needed, and the log call stays outside them. */
void IOT_ContractSenderCheckNoteDispatch(uint16_t eid, uint16_t elemId, uint16_t attrId)
{
    int64_t now = IOT_OsalGetUptimeUs();

    IOT_OsalEnterCritical();
    s_ring[s_next] = (dispatch_note_t) {.atUs = now, .eid = eid, .elemId = elemId, .attrId = attrId, .valid = true};
    s_next = (uint8_t) ((s_next + 1) % SENDER_CHECK_RING);
    IOT_OsalExitCritical();
}

bool IOT_ContractSenderCheckIsSuspect(uint16_t eid, uint16_t elemId, uint16_t attrId, uint8_t senderFrom)
{
    /* An automation sender still consumes the matching note: the automation's change came back
     * correctly reported, so a real manual change on the same element shortly after it must not
     * be blamed on the automation. */
    bool automationSender = senderFrom >= IOT_SENDER_SCENARIO_UUID_V4;

    int64_t now = IOT_OsalGetUptimeUs();
    bool found = false;
    int64_t ageUs = 0;

    IOT_OsalEnterCritical();
    for (int i = 0; i < SENDER_CHECK_RING; i++)
    {
        dispatch_note_t *n = &s_ring[i];
        if (!n->valid || n->eid != eid || n->elemId != elemId || n->attrId != attrId)
            continue;
        ageUs = now - n->atUs;
        n->valid = false; /* consumed by its first match, expired or not */
        if (ageUs >= 0 && ageUs <= (int64_t) SENDER_CHECK_WINDOW_MS * 1000)
        {
            found = true;
            break;
        }
    }
    IOT_OsalExitCritical();

    if (automationSender)
        return false;

    if (found)
    {
        IOT_LOGW(TAG,
                 "State change for %04x/%04x arrived as a manual change %u ms after an automation set it "
                 "— is the request's sender passed to IOT_ContractStateChanged()?",
                 eid, elemId, (unsigned) (ageUs / 1000));
    }
    return found;
}
