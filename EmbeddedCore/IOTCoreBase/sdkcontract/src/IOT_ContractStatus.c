/*
 * The device status, for a status LED. Derived from flags by precedence, never from the order the
 * events arrived in: the cloud can connect a moment before provisioning completes, and a status
 * read from the last event would flash "online" and fall back.
 */
#include "IOT_ContractStatus.h"

#include <stdint.h>

#include "IOT_ContractEvents.h"
#include "IOT_Core.h"
#include "IOT_Osal.h"

/* Core delivers its events on its own task, except the ones caused by a call into core, which come
 * on the caller's task (BOOTED inside IOT_ContractStart(), FACTORY_RESET inside
 * IOT_CoreFactoryReset()). So the flags, the derivation and the raise happen under one lock: a
 * subscriber sees the changes in the order the flags changed, never a stale one last. */
static IOT_OsalMutexHandle_t s_lock = NULL;

static bool statusLock(void)
{
    if (s_lock == NULL)
        s_lock = IOT_OsalMutexCreate(); /* IOT_ContractStatusInit() normally made it already */
    return s_lock != NULL && IOT_OsalMutexLock(s_lock, UINT32_MAX) == IOT_OK;
}

static void statusUnlock(void)
{
    IOT_OsalMutexUnlock(s_lock);
}

void IOT_ContractStatusInit(void)
{
    if (s_lock == NULL)
        s_lock = IOT_OsalMutexCreate();
}

static IOT_ContractStatusInputs_t s_in;
/* Provisioning is read from core the first time the status is worked out (core has read its
 * configuration by then). Core's BOOTED is the first event of a normal boot, but an update boot
 * sends none, so the first event seen is not always BOOTED. After that the provisioning,
 * cancellation and removal events keep it current. */
static bool s_provisionedKnown = false;
static bool s_raisedOnce = false;
static IOT_ContractStatus_t s_status = IOT_CONTRACT_STATUS_UNPROVISIONED;

IOT_ContractStatus_t IOT_ContractStatusDerive(const IOT_ContractStatusInputs_t *in)
{
    if (in == NULL)
        return IOT_CONTRACT_STATUS_UNPROVISIONED;
    if (in->updating)
        return IOT_CONTRACT_STATUS_UPDATING;
    if (!in->provisioned)
        return in->pairing ? IOT_CONTRACT_STATUS_PAIRING : IOT_CONTRACT_STATUS_UNPROVISIONED;
    if (in->pairing && !in->cloudUp)
        return IOT_CONTRACT_STATUS_PAIRING; /* just set up; its first session is not up yet */
    if (!in->wifiUp)
        return IOT_CONTRACT_STATUS_NO_WIFI;
    if (!in->cloudUp)
        return IOT_CONTRACT_STATUS_NO_CLOUD;
    return IOT_CONTRACT_STATUS_ONLINE;
}

IOT_ContractStatus_t IOT_ContractGetStatus(void)
{
    return s_status;
}

/* Returns false for an event that says nothing about the status. */
static bool applyDeviceEvent(const IOT_CoreDeviceEvent_t *d)
{
    switch (d->type)
    {
    case DEV_EVENT_BOOTED: /* raises the first status even when no other event comes */
        s_in.provisioned = IOT_CoreIsProvisioned();
        return true;
    /* A setup is what a phone starts; each one ends in exactly one of the three events below.
     * Network and cloud events alone never start one (an unprovisioned device may be on Ethernet,
     * or still on the WiFi of a setup that failed). */
    case DEV_EVENT_PROVISION_STARTED:
        s_in.pairing = true;
        return true;
    case DEV_EVENT_PROVISION_COMPLETED:
        /* Set up, but still "being set up" until the first session is up (CLOUD_CONNECTED comes
         * just after), so a status LED goes from pairing straight to online. */
        s_in.provisioned = true;
        if (s_in.cloudUp)
            s_in.pairing = false;
        return true;
    case DEV_EVENT_PROVISION_FAILED: /* setup gave up: not set up */
    case DEV_EVENT_PROVISION_CANCELLED:
        s_in.pairing = false;
        return true;
    case DEV_EVENT_FACTORY_RESET:
        s_in.provisioned = false;
        s_in.pairing = false;
        return true;
    case DEV_EVENT_DEVICE_REMOVED:
        if (!d->data.deviceRemoved.isRootDevice)
            return false; /* a sub-device: this device is unchanged */
        s_in.provisioned = false;
        s_in.pairing = false;
        return true;
    case DEV_EVENT_NETWORK_CONNECTING:
    case DEV_EVENT_CLOUD_CONNECTING:
        return false;
    case DEV_EVENT_NETWORK_CONNECTED:
        s_in.wifiUp = true;
        return true;
    case DEV_EVENT_NETWORK_DISCONNECTED:
        s_in.wifiUp = false;
        s_in.cloudUp = false;
        if (s_in.provisioned)
            s_in.pairing = false; /* a completed setup lost its network: show that instead */
        return true;
    case DEV_EVENT_NETWORK_CONNECT_FAILED:
        /* An attempt that never came up: no link was lost, so nothing changes. */
        return false;
    case DEV_EVENT_CLOUD_CONNECTED:
        s_in.cloudUp = true;
        if (s_in.provisioned)
            s_in.pairing = false; /* the first session after the setup completed */
        return true;
    case DEV_EVENT_CLOUD_DISCONNECTED:
        s_in.cloudUp = false;
        return true;
    case DEV_EVENT_CLOUD_CONNECT_FAILED:
        /* An attempt that never came up: the cloud was already down, so nothing changes. */
        return false;
    default:
        return false;
    }
}

static void ensureProvisionedKnown(void)
{
    if (s_provisionedKnown)
        return;
    s_provisionedKnown = true;
    s_in.provisioned = IOT_CoreIsProvisioned();
}

static void raiseIfChanged(void)
{
    IOT_ContractStatus_t now = IOT_ContractStatusDerive(&s_in);
    if (s_raisedOnce && now == s_status)
        return;
    s_raisedOnce = true;
    s_status = now;
    IOT_ContractEventStatusChanged(now);
}

void IOT_ContractStatusOnCoreEvent(const IOT_CoreEvent_t *event)
{
    if (event == NULL)
        return;
    if (!statusLock())
        return;
    ensureProvisionedKnown();
    if (event->category == IOT_EVENT_DEVICE)
    {
        (void) applyDeviceEvent(&event->evt.device);
    }
    else if (event->category == IOT_EVENT_OS)
    {
        /* Both end in a restart, so the flag is never cleared. An update request counts only once
         * the product has claimed it (the router hands it over after the product's hook): an
         * unclaimed one is erased by core and nothing restarts, so the status does not move. */
        IOT_CoreOsEventType_t type = event->evt.os.type;
        IOT_CoreOtaClaim_t claim = event->evt.os.data.otaRequested.claim;
        if (type == OS_EVENT_OTA_BOOT_STARTED ||
            (type == OS_EVENT_OTA_REQUESTED &&
             (claim == IOT_CORE_OTA_CLAIM_NOW || claim == IOT_CORE_OTA_CLAIM_AFTER_REBOOT)))
            s_in.updating = true;
    }

    raiseIfChanged();
    statusUnlock();
}
