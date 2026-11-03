#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventTypes.h"

/*
 * IR event: an IR command dispatched to the SDK.
 *
 * Split out of IOT_CoreEventHandler.h on 2026-10-14. That header remains the umbrella
 * (category enum, IOT_CoreEvent_t, callback registration) and includes this file, so
 * existing includes keep working. Prefer this header in new code that only needs it.
 */

/* ============================
 *  IR EVENT
 * ============================ */
/**
 * @brief IR command dispatched to the SDK.
 *
 * The inline-EXEC path: iotcore populates this event with the payload (borrowed
 * pointers valid only for the synchronous dispatch) and dispatches it. The SDK-layer handler decompresses/transmits
 * and writes `result` back (read after IOT_DevCoreDispatchEvent returns, since
 * dispatch is synchronous). QUERY_CAPABILITY is how core answers the cloud when it
 * asks what IR this device can do: the SDK fills capMode/capDetect from the
 * app-declared capability.
 */

#ifdef __cplusplus
extern "C" {
#endif
typedef enum
{
    IOT_IR_EVENT_EXEC_RAW,          ///< raw timing pairs in `data`: send verbatim
    IOT_IR_EVENT_EXEC_RAW_ZIP,      ///< compressed timing data: decompress then send
    IOT_IR_EVENT_EXEC_PRTC,         ///< protocol id + payload: send via IR protocol encoder
    IOT_IR_EVENT_QUERY_CAPABILITY,  ///< cloud asked what IR we support: SDK fills capMode/capDetect
    IOT_IR_EVENT_STORE_KEYSET,      ///< packed keyset for `eid` in data/dataLen → IR DAO
    IOT_IR_EVENT_EXEC_AC,           ///< differential AC; brand in `protocol`, state in ac* fields
    IOT_IR_EVENT_LEARN_START,       ///< open a detect/learn window; the SDK answers LATER
} IOT_EventTypeIr_t;

typedef struct
{
    IOT_EventTypeIr_t type;
    /* Inputs — borrowed pointers, valid only during synchronous dispatch. */
    uint16_t eid;        ///< target sub-device EID (STORE_KEYSET)
    uint16_t protocol;   ///< IR protocol id (EXEC_PRTC)
    uint8_t *header;     ///< rawzip header (EXEC_RAW_ZIP)
    uint16_t headerLen;
    uint8_t *data;       ///< raw blob / zip data / prtc code bytes / packed keyset (STORE_KEYSET)
    uint16_t dataLen;
    /* Outputs — written by the SDK handler, read by iotcore after dispatch. */
    iot_err_t result;    ///< EXEC/STORE result reported back to the cloud
    uint8_t capMode;     ///< QUERY_CAPABILITY: 0=TX only, 1=RX only, 2=TX&RX
    uint8_t capDetect;   ///< QUERY_CAPABILITY: 0=no detect, 1=detect supported
    /* EXEC_AC (differential AC): brand is carried in `protocol` above; state below.
     * Raw fields (not IOT_AcState_t) so this core header stays free of the IR platform type. */
    uint8_t acPower;     ///< EXEC_AC: 0=off, 1=on
    uint8_t acMode;      ///< EXEC_AC: iot_attr_mode_t
    uint8_t acTemp;      ///< EXEC_AC: target temperature, degrees Celsius
    uint8_t acFan;       ///< EXEC_AC: iot_attr_fan_speed_t
    uint8_t acSwing;     ///< EXEC_AC: swing position/mode
    /* LEARN_START only. This is the one ASYNCHRONOUS event in this category: its
     * `result` means "the window was opened", not "a code was captured". The SDK
     * owns the window on a task of its own and reports the capture later through
     * IOT_CoreReportIrDetected(), so the reply needs the ORIGINAL requester's
     * routing — this dispatch is long over by then. */
    uint8_t learnMode;        ///< 0x01 = capture raw without decoding; otherwise decode
    IOT_RequestContext_t ctx; ///< routing for the deferred reply
} IOT_CoreIrEvent_t;
#ifdef __cplusplus
}
#endif
