# Drive a device status LED

The contract works out what a status LED shows and raises `IOT_CONTRACT_EVT_STATUS_CHANGED` when it
changes. Subscribe before `IOT_ContractStart()`:

```c
static void app_on_contract_event(const IOT_ContractEvent_t *event, void *arg)
{
    (void) arg;
    if (event->type == IOT_CONTRACT_EVT_STATUS_CHANGED)
    {
        my_led_show(event->data.statusChanged.status);   /* short: set a pattern, return */
    }
}

/* in app_main, before IOT_ContractStart() */
(void) IOT_ContractSubscribe(app_on_contract_event, NULL);
```

## The statuses

| `IOT_ContractStatus_t` | Meaning |
|---|---|
| `IOT_CONTRACT_STATUS_UNPROVISIONED` | Not set up, nothing in progress |
| `IOT_CONTRACT_STATUS_PAIRING` | A phone connected to set the device up; setup not finished |
| `IOT_CONTRACT_STATUS_NO_WIFI` | Set up, WiFi not connected |
| `IOT_CONTRACT_STATUS_NO_CLOUD` | WiFi connected, the cloud is not |
| `IOT_CONTRACT_STATUS_ONLINE` | Set up and connected to the cloud |
| `IOT_CONTRACT_STATUS_UPDATING` | A firmware update is under way (ends in a restart) |

The status is derived by precedence — updating, then not set up / pairing, then no WiFi, then no
cloud, then online — never from the order events arrive in, so the cloud connecting a moment
before setup completes never flashes "online".

- `_PAIRING` lasts through a wrong WiFi password (the phone can retype) and, once setup completes,
  until the first cloud session is up, so the LED goes straight to `_ONLINE`. A setup that fails
  (nothing worked within the setup window) or is cancelled goes back to `_UNPROVISIONED`.
- `_UPDATING` starts when you claim an update request (after `onCoreEvent` returns; a request you
  leave unclaimed changes nothing) or at an update boot, and lasts until a restart: an update you
  run yourself must end in one, as a failed one does.

## When it is raised

Only when the status changes, plus once when `IOT_ContractStart()` has started core, so the LED is
right from boot. That first one comes on the task that called `IOT_ContractStart()` — subscribe
before the start to get it. Later ones come on core's event task, one at a time and in order (the
one for a factory reset you call, on your task before the call returns).

Read the current value any time, from any task, with `IOT_ContractGetStatus()`; before
`IOT_ContractStart()` has started core it is `IOT_CONTRACT_STATUS_UNPROVISIONED`.

The raw WiFi, cloud and setup events still reach `onCoreEvent` unchanged
([core-events.md](core-events.md)): use the status, the raw events, or both.

## Rules

- Subscribers must not block: they run on the core event task, which also applies commands. Set a
  pattern and let your own LED task or timer blink it.
- `IOT_ContractSubscribe()` is boot-only and takes up to `IOT_CONTRACT_EVENT_SUBSCRIBERS_MAX`
  subscribers (`IOT_ERR_NO_MEM` past that, `IOT_ERR_INVALID_ARG` for a NULL callback).
