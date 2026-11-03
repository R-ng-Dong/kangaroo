#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"

/*
 * UMBRELLA HEADER — inbound events, core → SDK.
 *
 * Until 2026-10-14 this file held every event category in one 1000-line body. The structs now
 * live in one header per category (mirroring the outbound split of IOT_CoreReporter.h on
 * 2026-10-06); this file keeps what spans them all: the category enum, the IOT_CoreEvent_t
 * wrapper and callback registration. Including it still gives you everything, exactly as
 * before. Prefer the category header in new code that only needs one.
 */
#include "IOT_CoreEventTypes.h"   /* sender types, group constants, IOT_RequestContext_t, domain types */
#include "IOT_CoreEventState.h"
#include "IOT_CoreEventDevice.h"
#include "IOT_CoreEventMesh.h"
#include "IOT_CoreEventSetting.h"
#include "IOT_CoreEventSmart.h"
#include "IOT_CoreEventOs.h"
#include "IOT_CoreEventIr.h"
#include "IOT_CoreEventService.h"

/* ============================
 *  MAIN EVENT WRAPPER
 * ============================ */

#ifdef __cplusplus
extern "C" {
#endif
typedef enum
{
    IOT_EVENT_STATE,
    IOT_EVENT_DEVICE,
    IOT_EVENT_MESH,
    IOT_EVENT_SETTING,
    IOT_EVENT_SMART,
    IOT_EVENT_OS,
    IOT_EVENT_IR,
    IOT_EVENT_SERVICE,
} IOT_CoreEventCategory_t;

/** The last category above. Categories run from IOT_EVENT_STATE to this one with no gaps, so a
 *  loop to it registers every category. Moves when a category is appended. */
#define IOT_EVENT_CATEGORY_LAST IOT_EVENT_SERVICE

typedef struct
{
    IOT_CoreEventCategory_t category;
    union
    {
        IOT_CoreStateEvent_t state;
        IOT_CoreDeviceEvent_t device;
        IOT_CoreMeshEvent_t mesh;
        IOT_CoreSettingEvent_t setting;
        IOT_CoreSmartEvent_t smart;
        IOT_CoreOsEvent_t os;
        IOT_CoreIrEvent_t ir;
        IOT_CoreServiceEvent_t service;
    } evt;
} IOT_CoreEvent_t;

/* ============================
 *  CALLBACK
 * ============================ */

/**
 * @brief Event callback function type
 * @param arg User-defined argument passed during registration
 * @param event Pointer to the event structure
 */
typedef void (*IOT_CoreEventCb_t)(void *arg, const IOT_CoreEvent_t *event);

/**
 * @brief Register an event callback for a specific category
 * @param category Event category to register for
 * @param callback Callback function to be invoked when events occur
 * @param arg User argument to pass to the callback
 * @return IOT_OK on success, IOT_ERR_INVALID_ARG if parameters are invalid,
 *         IOT_ERR_INVALID_STATE if callback already registered for this category
 */
iot_err_t IOT_DevCoreRegisterEventCb(IOT_CoreEventCategory_t category, IOT_CoreEventCb_t callback, void *arg);

/**
 * @brief Unregister an event callback for a specific category
 * @param category Event category to unregister
 * @return IOT_OK on success, IOT_ERR_INVALID_ARG if category is invalid,
 *         IOT_ERR_NOT_FOUND if no callback is registered for this category
 */
iot_err_t IOT_DevCoreUnregisterEventCb(IOT_CoreEventCategory_t category);
#ifdef __cplusplus
}
#endif
