/*
 * IOT_ContractStart() and the core event router it installs.
 *
 * The contract takes core's callback for every event category, so the request that changes
 * cloud-visible data reaches the contract first. Each category has its own route below; a
 * category the contract does not own yet forwards to the product's onCoreEvent unchanged.
 */
#include "IOT_Contract.h"
#include "IOT_ContractAttrsInternal.h"
#include "IOT_ContractStatus.h"
#include "IOT_ContractRestore.h"
#include "IOT_ContractInternal.h"
#include "IOT_ContractElements.h"
#include "IOT_ContractPending.h"
#include "IOT_ContractDeviceStatus.h"

#include <stddef.h>
#include "IOT_Core.h"
#include "IOT_CoreEventHandler.h"
#include "IOT_Log.h"
#include "IOT_Record.h"
#include "services/IOT_ContractDevice.h"
#include "services/IOT_ContractIr.h"
#include "services/IOT_ContractStateQuery.h"
#include "services/IOT_ContractStateSet.h"

static const char *TAG = "ContractRouter";

/* Copied at start, so the caller's struct may live on its stack. */
static IOT_ContractCallbacks_t s_cb;

typedef void (*ContractRouteFn)(const IOT_CoreEvent_t *event);

static void routeForward(const IOT_CoreEvent_t *event)
{
    if (s_cb.onCoreEvent != NULL)
        s_cb.onCoreEvent(event);
}

static void routeSmart(const IOT_CoreEvent_t *event)
{
    /* Core owns the event for the duration of the callback; the handler writes its result into it. */
    iot_err_t err = IOT_ContractHandleSmartEvent((IOT_CoreSmartEvent_t *) &event->evt.smart);
    if (err != IOT_OK)
        IOT_LOGW(TAG, "Smart event not handled: %d", (int) err);
}

static void routeState(const IOT_CoreEvent_t *event)
{
    /* The state command, the state query and the history query are the contract's. A sensor-log
     * request is the product's only when its Record append override keeps that history (and it
     * has a hook to answer with); otherwise the contract answers it empty. Core owns the event for
     * the duration of the callback; the handlers may rewrite the request in place. */
    IOT_CoreStateEvent_t *state = (IOT_CoreStateEvent_t *) &event->evt.state;
    iot_err_t err = IOT_OK;
    switch (state->type)
    {
    case IOT_EVENT_TYPE_STATE_SET:
        /* The contract's handling is the answer core sends to an acknowledged command. */
        err = IOT_ContractStateHandleSet(&state->data.setRequest, s_cb.apply);
        state->data.setRequest.result = err;
        break;
    case IOT_EVENT_TYPE_STATE_GET:
        err = IOT_ContractStateRespondGet(&state->data.getRequest);
        break;
    case IOT_EVENT_TYPE_STATE_RECORD:
        err = IOT_ContractStateRespondRecord(&state->data.recordRequest);
        break;
    case IOT_EVENT_TYPE_STATE_SENSOR_LOG:
        if (IOT_RecordHasAppendOverride() && s_cb.onCoreEvent != NULL)
            routeForward(event);
        else
            err = IOT_ContractStateRespondSensorLogEmpty(&state->data.sensorLogRequest);
        break;
    default:
        routeForward(event);
        break;
    }
    if (err != IOT_OK)
        IOT_LOGW(TAG, "State event %d not handled: %d", (int) state->type, (int) err);
}

static void routeOs(const IOT_CoreEvent_t *event)
{
    /* The product restarts the chip from its hook, so what the contract holds in RAM is written
     * out first. The OTA boot is not a restart request: the product claims it unchanged. */
    IOT_CoreOsEventType_t type = event->evt.os.type;
    if (type == OS_EVENT_REBOOT_REQUESTED || type == OS_EVENT_OTA_REQUESTED)
    {
        iot_err_t err = IOT_ContractPrepareRestart();
        if (err != IOT_OK)
            IOT_LOGW(TAG, "Flush before restart incomplete: %d", (int) err);
    }
    routeForward(event);
}

static void routeIr(const IOT_CoreEvent_t *event)
{
    /* The keyset is cloud data and the capability is the contract's answer; sending and
     * learning drive the product's hardware. */
    IOT_EventTypeIr_t type = event->evt.ir.type;
    if (type != IOT_IR_EVENT_STORE_KEYSET && type != IOT_IR_EVENT_QUERY_CAPABILITY)
    {
        routeForward(event);
        return;
    }
    /* Core owns the event for the duration of the callback; the result is written into it. */
    iot_err_t err = IOT_ContractHandleIrEvent((IOT_CoreIrEvent_t *) &event->evt.ir);
    if (err != IOT_OK)
        IOT_LOGW(TAG, "IR request not handled: %d", (int) err);
}

void RAL_ContractHandleDeviceEvent(IOT_CoreDeviceEvent_t *event)
{
        iot_err_t err = IOT_ContractHandleDeviceEvent(event, &s_cb);
        if (err != IOT_OK)
            IOT_LOGW(TAG, "Device event %d answered with %d", (int) event->type, (int) err);
}

static void routeDevice(const IOT_CoreEvent_t *event)
{
    /* Joins, removals, a local factory reset, group bind/unbind and the attribute set are store work the contract
     * answers itself; the product hears of them through its device hooks. On a cloud connect the
     * contract runs its own duties, then the product gets the event. A status request goes to the
     * product, and the contract answers it when the product did not. Everything else (connection,
     * provisioning, identification) is the product's, unchanged. Core owns the event for the
     * duration of the callback. */
    IOT_CoreDeviceEvent_t *device = (IOT_CoreDeviceEvent_t *) &event->evt.device;
    switch (device->type)
    {
    case DEV_EVENT_NEW_DEVICE_JOINED:
    case DEV_EVENT_DEVICE_REMOVED:
    case DEV_EVENT_FACTORY_RESET:
    case DEV_EVENT_GROUP_BIND:
    case DEV_EVENT_GROUP_UNBIND:
    case DEV_EVENT_DEVICE_ATTR_SET:
    {
        iot_err_t err = IOT_ContractHandleDeviceEvent(device, &s_cb);
        if (err != IOT_OK)
            IOT_LOGW(TAG, "Device event %d answered with %d", (int) device->type, (int) err);
        break;
    }
    case DEV_EVENT_DEVICE_STATUS_REQUEST:
        /* The product answers it if it wants to; one it leaves unclaimed the contract answers. */
        routeForward(event);
        IOT_ContractDeviceStatusAnswer(&device->data.deviceStatusRequest);
        break;
    case DEV_EVENT_CLOUD_CONNECTED:
        IOT_ContractOnCloudConnected();
        /* Values reported before provisioning are stored before the full state goes out, so it
         * carries them. A root that is not known yet has none to write. */
        {
            uint16_t rootEid = 0;
            if (IOT_CoreGetRootEid(&rootEid) == IOT_OK)
                IOT_ContractPendingFlush(rootEid);
        }
        /* Changes made while offline (local control, automations) reach the cloud. */
        IOT_ContractStateSyncSchedule();
        routeForward(event);
        break;
    default:
        routeForward(event);
        break;
    }
}

/* One route per category. A category left out (or appended to core later) forwards. */
static const ContractRouteFn s_routes[IOT_EVENT_CATEGORY_LAST + 1] = {
    [IOT_EVENT_STATE] = routeState,     [IOT_EVENT_DEVICE] = routeDevice,  [IOT_EVENT_MESH] = routeForward,
    [IOT_EVENT_SETTING] = routeForward, [IOT_EVENT_SMART] = routeSmart,    [IOT_EVENT_OS] = routeOs,
    [IOT_EVENT_IR] = routeIr,           [IOT_EVENT_SERVICE] = routeForward,
};

static void contractRouter(void *arg, const IOT_CoreEvent_t *event)
{
    (void) arg;
    if (event == NULL)
        return;
    /* First, so a subscriber sees the new status before the event's own work (the cloud-connected
     * full-state report, a forwarded event). An update request is the exception: it is a claim, so
     * the status takes it after the product has answered, and a declined one never shows UPDATING. */
    bool afterRoute = (event->category == IOT_EVENT_OS && event->evt.os.type == OS_EVENT_OTA_REQUESTED);
    if (!afterRoute)
        IOT_ContractStatusOnCoreEvent(event);
    int category = (int) event->category;
    ContractRouteFn route = NULL;
    if (category >= 0 && category <= (int) IOT_EVENT_CATEGORY_LAST)
        route = s_routes[category];
    if (route == NULL)
        route = routeForward;
    route(event);
    if (afterRoute)
        IOT_ContractStatusOnCoreEvent(event);
}

iot_err_t IOT_ContractStart(const IOT_ContractConfig_t *config, const IOT_ContractCallbacks_t *cb)
{
    if (config == NULL || config->modelId == NULL || cb == NULL || cb->apply == NULL)
        return IOT_ERR_INVALID_ARG;

    /* The product's attributes are registered by now; from here every task reads the table. */
    IOT_ContractAttrsFreeze();

    iot_err_t err = IOT_ContractInit(config);
    if (err != IOT_OK)
        return err;

    /* After init: the declared map replaces the config's Record resolver when it has elements. */
    IOT_ContractElementsDeclare(config->elementIds, config->elementCount);
    s_cb = *cb;

    // for (int category = IOT_EVENT_STATE; category <= (int) IOT_EVENT_CATEGORY_LAST; category++)
    // {
    //     err = IOT_DevCoreRegisterEventCb((IOT_CoreEventCategory_t) category, contractRouter, NULL);
    //     if (err != IOT_OK)
    //     {
    //         IOT_LOGE(TAG, "Core event category %d not registered (%d): is another callback registered?", category,
    //                  (int) err);
    //         return err;
    //     }
    // }

    err = IOT_CoreInit(config->modelId);
    if (err != IOT_OK)
        return err;

    /* Provisioning is known now (core read it while starting). The stored controls go back to the
     * hardware on core's own task, after anything already queued and before what comes next, so a
     * restore never interleaves with a command; on an update boot this still precedes the download. */
    IOT_ContractRestoreSchedule(&s_cb);
    return IOT_OK;
}
