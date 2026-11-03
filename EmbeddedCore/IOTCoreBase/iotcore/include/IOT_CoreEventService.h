#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"

/*
 * Service event: a service message from your backend, delivered to the SDK.
 *
 * The downlink half of IOT_CoreServiceMsg.h. Core hands the message over unread -- it is
 * never parsed or validated -- and acknowledges receipt to the sender with whatever the
 * SDK writes into `result`. Register for IOT_EVENT_SERVICE with IOT_DevCoreRegisterEventCb();
 * with no handler registered the sender is told the device does not support it.
 */

#ifdef __cplusplus
extern "C" {
#endif
typedef enum
{
    IOT_SERVICE_EVENT_MSG, ///< a JSON message in `json` / `jsonLen`
} IOT_EventTypeService_t;

typedef struct
{
    IOT_EventTypeService_t type;
    /* Input -- borrowed, valid only during the synchronous dispatch. Copy what you keep. */
    const char *json; ///< the message, NUL-terminated for convenience; see jsonLen
    uint16_t jsonLen; ///< its length in bytes, 1..IOT_SERVICE_MSG_PAYLOAD_MAX, excluding the NUL
    /* Output -- written by the SDK handler. IOT_OK acknowledges receipt; any error is
     * reported to the sender as a failure. Preset to IOT_ERR_NOT_SUPPORTED. */
    iot_err_t result;
} IOT_CoreServiceEvent_t;
#ifdef __cplusplus
}
#endif
