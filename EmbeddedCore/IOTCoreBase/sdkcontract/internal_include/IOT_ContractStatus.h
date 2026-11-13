#pragma once

/**
 * @file IOT_ContractStatus.h
 * @brief The device status: flags kept from core events, and the status derived from them.
 */

#include <stdbool.h>

#include "IOT_Contract.h"
#include "IOT_CoreEventHandler.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What the status is derived from. */
typedef struct
{
    bool provisioned; /**< The device is set up. */
    bool pairing;     /**< A setup a phone started has not ended, or it completed and its first
                           *   cloud session is not up yet. */
    bool wifiUp;      /**< WiFi connected. */
    bool cloudUp;     /**< The cloud connected. */
    bool updating;    /**< A firmware update is under way. */
} IOT_ContractStatusInputs_t;

/** The status for these inputs, by precedence. NULL: IOT_CONTRACT_STATUS_UNPROVISIONED. */
IOT_ContractStatus_t IOT_ContractStatusDerive(const IOT_ContractStatusInputs_t *in);

/** Create the status lock. IOT_ContractInit() calls it while still single-threaded. */
void IOT_ContractStatusInit(void);

/** Update the inputs from a core event and raise IOT_CONTRACT_EVT_STATUS_CHANGED when the status
 *  changed (or on the first event). Called by the router for every core event, before it is handled. */
void IOT_ContractStatusOnCoreEvent(const IOT_CoreEvent_t *event);

#ifdef __cplusplus
}
#endif
