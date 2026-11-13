# Getting started: an SDK directly on iotcore (path B)

This page brings up a device on `iotcore` alone: your own stores, your own handlers, your own
reports. Most products should build on `sdkcontract` instead (path A), which does the store, Smart,
Record and report work for you; its
[getting-started](../../sdkcontract/docs/getting-started.md) has a complete buildable example. Read
this page when you are writing your own SDK, or to understand what the contract does underneath.

## 1. Bring up the platform, then your stores

```c
void app_main(void)
{
    if (IOT_InitPlatform() != IOT_OK)    // storage, network, BLE, timers: always first
        return;
    my_stores_init();                    // your Device store and anything else you keep
    my_hardware_init();                  // ready before core can deliver a command
```

`IOT_InitPlatform()` comes first: without it the platform's function tables are empty. Your stores
come up on the platform's storage ([how-to/own-stores.md](how-to/own-stores.md)).

## 2. Register a callback for every category, then start core

```c
    for (int c = IOT_EVENT_STATE; c <= IOT_EVENT_CATEGORY_LAST; c++)
        IOT_DevCoreRegisterEventCb((IOT_CoreEventCategory_t) c, my_on_event, NULL);

    if (IOT_CoreInit(MODEL_ID) != IOT_OK)   // the 16-character model id Rogo assigned
        return;
}
```

Register before `IOT_CoreInit()`: core delivers its first event (`DEV_EVENT_BOOTED`, or
`OS_EVENT_OTA_BOOT_STARTED` on an update boot) on your task, inside that call. An unprovisioned
device starts its setup advert; a provisioned one joins its network and the cloud. Optional core
settings (`IOT_CoreConfigureChangeWifi()`) go before `IOT_CoreInit()` too.

## 3. Route events by category

```c
static void my_on_event(void *arg, const IOT_CoreEvent_t *event)
{
    IOT_CoreEvent_t *e = (IOT_CoreEvent_t *) event;   // requests carry an answer you write
    switch (event->category)
    {
    case IOT_EVENT_STATE:   my_on_state(&e->evt.state);   break;
    case IOT_EVENT_DEVICE:  my_on_device(&e->evt.device); break;
    case IOT_EVENT_OS:      my_on_os(&e->evt.os);         break;
    default:                break;   // unhandled requests are answered "not supported"
    }
}
```

Every event's task, data and expected answer is in [events.md](events.md); the model behind them is
in [concepts.md](concepts.md#the-event-flow).

## 4. Answer a state command and a state query

```c
static void my_on_state(IOT_CoreStateEvent_t *st)
{
    switch (st->type)
    {
    case IOT_EVENT_TYPE_STATE_SET:
        // resolve the target, actuate, store, report (how-to/handle-state.md)
        st->data.setRequest.result = my_apply_command(&st->data.setRequest);
        break;
    case IOT_EVENT_TYPE_STATE_GET:
        my_answer_state_query(&st->data.getRequest);   // IOT_CoreRequestReportState()
        break;
    default:
        break;
    }
}
```

[how-to/handle-state.md](how-to/handle-state.md) has both in full, including the report every
change owes the cloud.

## 5. Claim what is yours

- **Reboot:** on `OS_EVENT_REBOOT_REQUESTED` arrange a restart a moment later and write
  `result = IOT_OK`. Core never restarts the chip for you.
- **Update:** on `OS_EVENT_OTA_REQUESTED` claim `IOT_CORE_OTA_CLAIM_NOW` or
  `IOT_CORE_OTA_CLAIM_AFTER_REBOOT`; on an update boot set `otaBootStarted.handled`.
- **Status:** answer `DEV_EVENT_DEVICE_STATUS_REQUEST` and send the status after each cloud connect
  ([how-to/device-status.md](how-to/device-status.md)).
- **Identify:** blink and write `result = IOT_OK`.
- **Factory reset:** your long press calls `IOT_CoreFactoryReset()`; your `DEV_EVENT_FACTORY_RESET`
  handler wipes your stores and restarts.

## 6. Report what the hardware did by itself

A button or a sensor changed state: store it and report it with
`IOT_CoreRequestReportStateChanged()` from a task of your own (never a timer callback or an ISR),
with `senderFrom = IOT_SENDER_DEVICE` ([how-to/handle-state.md](how-to/handle-state.md#a-change-your-hardware-made-by-itself)).

## 7. Flash and set it up

Build with the ESP-IDF settings in the platform's
[sdkconfig reference](../../iotPlatform/docs/sdkconfig-reference.md), flash, and set the device up
from the app. `DEV_EVENT_PROVISION_STARTED` comes when the phone connects, `_COMPLETED` when the
device is provisioned, then `DEV_EVENT_CLOUD_CONNECTED`.

## What is still yours on path B

Groups, joins and removals for a hub ([how-to/hub-and-sub-devices.md](how-to/hub-and-sub-devices.md)),
automations ([how-to/smart-engine.md](how-to/smart-engine.md)), history (the
[Record rulebook](../../sdkcontract/docs/record.md)), IR, mesh and service messages
([events.md](events.md)).
