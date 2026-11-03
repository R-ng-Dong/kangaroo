# IoTCoreEmbedded API Reference

**Updated**: 2026-10-30

This document covers the public iotcore headers in `iotcore/include/`. That directory is
the whole public API: everything in it is something your SDK may include, and nothing
outside it is. `iotcore/include/README.md` maps every header by role.

| Role | Header | What it holds | Section |
|---|---|---|---|
| Engine lifecycle | `IOT_Core.h` | init/deinit, provisioning, WiFi and cloud session, boot counter, config dump, work queue | §1 |
| Engine lifecycle | `IOT_CoreOta.h` | the OTA surface (included by `IOT_Core.h`) | §1, *OTA* |
| Engine lifecycle | `IOT_ProvisionTypes.h` | **deprecated** forwarding header — `IOT_ProvisionMode_t` is now in `IOT_Core.h` | §1 |
| Capability declaration | `IOT_CoreHubRole.h`, `IOT_CoreHubRoleFlags.h` | hub-role declaration and Zigbee hub lifecycle | §2 |
| Inbound — core calls you | `IOT_CoreEventHandler.h` | **umbrella**: category enum, `IOT_CoreEvent_t`, callback registration, `IOT_EVENT_CATEGORY_LAST`; includes the nine below | §3 |
| Inbound | `IOT_CoreEventTypes.h` | `IOT_SENDER_*`, group/sentinel constants, `IOT_RequestContext_t`, domain types | §3 |
| Inbound | `IOT_CoreEventState.h`, `IOT_CoreEventDevice.h`, `IOT_CoreEventMesh.h`, `IOT_CoreEventSetting.h`, `IOT_CoreEventSmart.h`, `IOT_CoreEventOs.h`, `IOT_CoreEventIr.h`, `IOT_CoreEventService.h` | one event category each; `IOT_CoreEventService.h` delivers messages from your own backend | §3 |
| Outbound — you call core | `IOT_CoreReporter.h` | **umbrella**: declares nothing; includes `IOT_CoreState.h`, `IOT_CoreSmart.h`, `IOT_CoreSensorLog.h`, `IOT_CoreIr.h` | §4 |
| Outbound | `IOT_CoreState.h` | GET_STATE answers, state-change reports, device status, DEVICE command response | §4 |
| Outbound | `IOT_CoreSmart.h` | trigger reports, sync-on-off, condition groups, SMART command responses | §4 |
| Outbound | `IOT_CoreSensorLog.h` | legacy (V1) sensor-log responses — **not available in this library** | §4 |
| Outbound | `IOT_CoreRecord.h` | Record (V2) responses and pushes — the replacement for sensor log. **Not** included by the umbrella | §4 |
| Outbound | `IOT_CoreIr.h` | IR keyset request and IR learn results | §4 |
| Outbound | `IOT_CoreServiceMsg.h` | partner business messages — publish your own opaque payload on a channel you name | §5 |
| Shared | `IOT_CoreTimeRequest.h` | `IOT_CoreTimeRequested()` — did a GET_STATE ask for the time? | §4 |
| Diagnostics (dev only) | `IOT_CoreHeapProbe.h` | heap-fragmentation sampling — **no-ops in this library** | §6 |

**Your existing includes keep working.** `IOT_CoreEventHandler.h` and `IOT_CoreReporter.h`
are now umbrellas over the per-domain headers, and `IOT_Core.h` still includes
`IOT_CoreOta.h`, so code written against the earlier single headers compiles unchanged. No
struct, field or signature changed in the split. In new code, prefer the specific header
you need.

Every header compiles on its own — include just the one you need, in any order, with no
prerequisite header — and every header is safe to include from C++: all declarations sit
inside an `extern "C"` block, so a C++ `main` or C++ SDK links against `libiotcore.a`
without wrapping the includes yourself.

---

## 1. IOT_Core.h

### Overview

Initialization and lifecycle control for the iotcore engine.
Call `IOT_CoreInit()` once at startup. The core automatically starts provisioning
if the device is not yet provisioned.

---

### `IOT_CoreInit()`

```c
iot_err_t IOT_CoreInit(const char *modelId);
```

Initialize the iotcore engine with the device's model ID.

If the device is not yet provisioned, provisioning starts automatically (BLE advertising + LAN config).
If already provisioned, the core connects to WiFi and MQTT.

**Parameters:**
- `modelId` — the 16-character, NUL-terminated model ID Rogo device management assigned
  you. Read during init only, so a stack buffer is fine.

> **This is a string, not bytes, and it is not always hex.** Model IDs such as
> `"RAPLWL0000000001"` are valid. Pass the text exactly as issued — do not convert it to
> a byte array, and do not parse it yourself. A wrong model ID does not fail loudly: the
> cloud looks up a different model record, and the device joins with the wrong element
> and attribute set.

**Returns:** `IOT_OK` on success. See *OTA* below for the extra return values on the
OTA-only boot path.

> **A failed init is not retryable in place.** If `IOT_CoreInit()` fails during storage
> bootstrap or network start (the OTA path's not-provisioned case included), core is left
> partially initialised, and a second `IOT_CoreInit()` call fails with
> `IOT_ERR_INVALID_STATE` rather than starting over. Reboot, or call `IOT_CoreDeinit()`
> first, before retrying. A failure in the earlier work-queue or crypto-init steps leaves
> core cleanly uninitialised, and a retry can start directly.

---

### `IOT_CoreDeinit()`

```c
iot_err_t IOT_CoreDeinit(void);
```

Tear down and free all iotcore resources. Call before a controlled shutdown.

---

### `IOT_CoreFactoryReset()`

```c
iot_err_t IOT_CoreFactoryReset(void);
```

Reset core-owned config (provision state, WiFi, MQTT, certs, etc.).
Dispatches `DEV_EVENT_DEVICE_REMOVED` (`isRoot=true`) so the SDK can reset its own DAOs.
The SDK event handler is expected to restart the chip.

**Returns:** `IOT_OK` on success, `IOT_ERR_DATA_NOT_FOUND` if not provisioned

---

### `IOT_CoreIsProvisioned()`

```c
bool IOT_CoreIsProvisioned(void);
```

Non-blocking check whether the device has been provisioned (cached in RAM).

---

### `IOT_CoreGetRootEid()`

```c
iot_err_t IOT_CoreGetRootEid(uint16_t *eid);
```

Get the root device's Element ID.

**Returns:** `IOT_OK` on success, `IOT_ERR_DATA_NOT_FOUND` if not provisioned

### Root device identity

```c
iot_err_t IOT_CoreGetRootNwkAddr(uint16_t *addr);   // mesh unicast address assigned at provisioning
iot_err_t IOT_CoreGetRootGroup(uint16_t *group);    // group address assigned at provisioning
iot_err_t IOT_CoreGetRootDeviceId(uint8_t *outDevId, uint16_t outSize, uint16_t *outLen);
```

`IOT_CoreGetRootDeviceId()` copies the 12-byte device ID as provisioned. It copies at most
`outSize` bytes and reports how many landed in `outLen` (which may be NULL) — a short
buffer truncates rather than failing.

---

### Change-WiFi BLE

> 🚧 **IN DEVELOPMENT — NOT READY. Do not use yet.** These APIs are present so the
> library links, but the feature is unfinished/unvalidated and may change. SDK
> writers should not enable it for now.
>
> **BLE-radio constraint (when it ships):** this feature uses **BLE advertising**,
> which conflicts with **BLE Mesh** (both contend for the BLE radio/advertiser).
> It cannot run while BLE Mesh is active or has occupied the advertiser — a device
> must **release/stop or disable BLE Mesh before** calling
> `IOT_CoreStartChangeWifiBle()`, and may re-enable mesh afterward.

Lets a provisioned device re-advertise over BLE so a paired app can scan for and
set new WiFi credentials without a factory reset.

```c
// Start "change WiFi" BLE advertisement (provisioned device only).
iot_err_t IOT_CoreStartChangeWifiBle(void);

// Stop the change-WiFi BLE advertisement. Safe to call when not running.
// Call when cloud connectivity is restored to release the BLE stack.
iot_err_t IOT_CoreStopChangeWifiBle(void);
```

- `IOT_CoreStartChangeWifiBle()` returns `IOT_ERR_INVALID_STATE` if the device
  is not provisioned or the core is not initialized.
- `IOT_CoreStopChangeWifiBle()` returns `IOT_OK` even if change-WiFi BLE was not
  running.

```c
typedef struct {
    bool     bleFallbackEnabled;  // Auto-start BLE when WiFi is down. Default: true
    uint32_t bleFallbackDelayMs;  // EXTRA delay once an outage is confirmed. Default: 0
} IOT_CoreChangeWifiConfig_t;

// The defaults that apply when IOT_CoreConfigureChangeWifi() is never called.
#define IOT_CORE_CHANGE_WIFI_CONFIG_DEFAULT \
    { .bleFallbackEnabled = true, .bleFallbackDelayMs = 0u }

// Optional. Must be called BEFORE IOT_CoreInit(). Defaults apply if not called.
iot_err_t IOT_CoreConfigureChangeWifi(const IOT_CoreChangeWifiConfig_t *config);
```

`IOT_CoreConfigureChangeWifi()` returns `IOT_ERR_INVALID_ARG` if `config` is NULL
and `IOT_ERR_INVALID_STATE` if called after `IOT_CoreInit()`. Start from
`IOT_CORE_CHANGE_WIFI_CONFIG_DEFAULT` and change only the field you need.

When `bleFallbackEnabled` is true, core starts change-WiFi BLE automatically once a
WiFi outage is **confirmed**, and stops it on reconnect. A single disconnect is not an
outage: transient drops (an AP roam, a missed beacon) are filtered by counting
consecutive failed reconnect attempts, so the advert starts only when the AP is known
to be gone — a few seconds into a genuine outage on the standard reconnect backoff.

> **`bleFallbackDelayMs` is not the debounce.** It is an *extra* delay applied after the
> outage is already confirmed, and it defaults to `0`. If you set your own value, note
> that it now stacks on top of the failure threshold rather than replacing it, so a large
> value holds the advert back further than it used to. Leave it at the default unless you
> deliberately want the advert delayed.

---

### Provisioning Control

```c
// Select provisioning mode (call before IOT_CoreStartProvision)
iot_err_t IOT_CoreSetProvisionMode(IOT_ProvisionMode_t mode);

// Persist and restore the POSIX timezone string used by local schedule logic
iot_err_t IOT_CoreSetTimeZone(const char *timeZone);
iot_err_t IOT_CoreGetTimeZone(char *outTimeZone, size_t outSize);

// Enter provisioning mode (BLE advertising + LAN config)
iot_err_t IOT_CoreStartProvision(void);

// Exit provisioning mode
iot_err_t IOT_CoreStopProvision(void);
```

```c
typedef enum {
    IOT_PROVISION_BLE_LAN = 0, // BLE config + LAN discovery (default)
    IOT_PROVISION_LAN_ONLY,    // LAN config only, no BLE (not yet implemented)
    IOT_PROVISION_AP_MODE,     // Device as WiFi AP (not yet implemented)
} IOT_ProvisionMode_t;
```

> **`IOT_ProvisionMode_t` now lives in `IOT_Core.h`**, next to the function that takes it.
> `IOT_ProvisionTypes.h` remains only as a deprecated forwarding header that includes
> `IOT_Core.h`, so an existing `#include "IOT_ProvisionTypes.h"` still compiles. It is
> scheduled for removal one release after this one — switch that include to `IOT_Core.h`.

`IOT_CoreSetProvisionMode()` returns `IOT_ERR_NOT_SUPPORTED` for the modes not yet
implemented (only `IOT_PROVISION_BLE_LAN` is) and `IOT_ERR_INVALID_ARG` for a value
outside the enum.

`IOT_CoreInit()` calls `IOT_CoreStartProvision()` automatically when unprovisioned.
Call it again manually only to re-enter provisioning after the device is already provisioned.
`IOT_CoreStartProvision()` returns `IOT_ERR_INVALID_STATE` before `IOT_CoreInit()` and is a
no-op (`IOT_OK`) when already provisioning; `IOT_CoreStopProvision()` is a no-op when not
provisioning.

`IOT_CoreSetTimeZone()` stores a POSIX TZ string such as `"ICT-7"` in core config and applies it to the platform timer layer.
`IOT_CoreGetTimeZone()` reads the persisted value into a caller-provided buffer. UTC timestamp APIs remain UTC.
It returns `IOT_ERR_DATA_NOT_FOUND` when no timezone has been set, and
`IOT_ERR_INVALID_ARG` / `IOT_ERR_INVALID_SIZE` for a bad output buffer.

---

### Mesh

Core no longer starts or joins a mesh network. It parses the cloud's mesh messages,
hands the payloads to your SDK as events, and answers the cloud when it asks whether
this device acts as a mesh hub — that is all. The transport lifecycle (join, keys,
node management) belongs to the SDK, because whether a device plays the hub role is a
product decision core cannot make. See **Hub roles** below.

---

### Boot Counter

```c
iot_err_t IOT_CoreGetBootCounter(uint8_t *count); // BLOCKING
iot_err_t IOT_CoreSetBootCounter(uint8_t count);  // BLOCKING
iot_err_t IOT_CoreSetBootCounterAsync(uint8_t count); // NON-BLOCKING (safe from timer callbacks)
```

Used by the SDK for boot loop detection and recovery.

---

### Connectivity

```c
bool      IOT_CoreIsCloudConnected(void);
iot_err_t IOT_CoreSetWifiCredentials(const char *ssid, const char *pwd);
iot_err_t IOT_CoreCloudConnect(void);
iot_err_t IOT_CoreCloudDisconnect(void);
iot_err_t IOT_CoreConnectStoredWifi(void);
```

`IOT_CoreIsCloudConnected()` answers whether the cloud channel is *genuinely
usable right now* — the MQTT session is up **and** every standard topic
subscription has been confirmed. That is a stricter bar than "the TCP session
opened", and the distinction matters: anything published before this point (a
status report, say) has no subscriber yet and is **silently lost**. It is
non-blocking and just reads a flag the MQTT callbacks maintain, so it is cheap to
poll from a boot-time wait loop. It returns false before the first connect and after
any disconnect.

`IOT_CoreSetWifiCredentials()` stores new credentials and reconnects, for callers
outside the protocol path such as a service console. It routes through the
change-WiFi service, which tears the cloud session down *before* switching networks,
so it is safe to call on a device that is currently online. `IOT_OK` means the change
was accepted; the connection result arrives later as a DEVICE event, not as the return
value. No message-layer acknowledgement is sent, because there is no originating
message to acknowledge.

`IOT_CoreCloudConnect()` starts an MQTT connection from stored configuration and
returns as soon as the attempt begins (`IOT_ERR_FAIL` if there is no MQTT client). Pair
it with `IOT_CoreIsCloudConnected()` to learn when the session is actually usable.
`IOT_CoreCloudDisconnect()` drops the session but leaves the client allocated, so a later
`IOT_CoreCloudConnect()` reuses it; disconnecting when nothing is connected is a harmless
no-op.

> If you were previously driving WiFi or the cloud session by declaring internal
> functions yourself, switch to these three — they are the supported way to do it.

`IOT_CoreConnectStoredWifi()` associates using the credentials already in
storage, for a boot that skips the normal LAN bring-up but still needs the
network. It returns once the association has been *requested* — use the network
events to learn when it is actually up.

> Do **not** call `IOT_CoreConnectStoredWifi()` on the OTA boot path. That path
> already takes the full LAN bring-up itself (mDNS / TCP server / MQTT included,
> which is what associates), so a second call races another association against a
> link already coming up. There is no production caller today; it stays public for
> a future boot mode that wants connectivity without the rest of LAN bring-up.

---

### Config diagnostics

```c
typedef void (*IOT_CoreConfigDumpCb_t)(const char *name, const void *data,
                                       uint16_t len, void *user);

iot_err_t IOT_CoreConfigDumpSafe(IOT_CoreConfigDumpCb_t cb, void *user);
```

Dumps the config keys core considers **safe to display**, for diagnostic tooling
such as a service console. Your callback runs once per safe key — in an
unspecified but stable order, and whether or not the key currently holds a value
(`data == NULL`, `len == 0` when unset). You own the formatting and nothing else.

"Safe" is core's judgement, not yours: no credential, URL or certificate is ever
handed to the callback. Deciding which values may be exposed belongs with the
code that knows which keys are encrypted, not with whatever tool is printing
them — so there is deliberately no way to ask for the rest.

Do not retain `data` past the call; it is valid only for that callback's
duration. Returns `IOT_ERR_INVALID_ARG` when `cb` is NULL.

---

### Core work queue

```c
iot_err_t IOT_CoreDeferWork(void (*handler)(void *), void *arg);
iot_err_t IOT_CoreGetWorkStats(IOT_CoreWorkStats_t *out);
```

Every inbound BLE/LAN/MQTT message and every deferred job runs on one core service task,
fed by a fixed-size queue that **drops work when it is full**.

`IOT_CoreDeferWork()` runs `handler(arg)` on that task. Use it from driver callbacks —
a BLE-mesh or Zigbee stack task, a timer — whose stacks are too small for an NVS write or
an MQTT publish and which must not block: copy what you need, hand it here, and return
immediately. It exists so an SDK implementing a hub transport does not have to stand up a
worker task of its own. On `IOT_ERR_NO_MEM` the queue was full and the job was **not**
queued — you still own `arg` and must free it.

`IOT_CoreGetWorkStats()` snapshots the queue's counters. It is safe from any task, costs
nothing, and returns `IOT_ERR_INVALID_ARG` for a NULL `out` or `IOT_ERR_INVALID_STATE`
before `IOT_CoreInit()` has created the queue. Log it beside your heap numbers when
diagnosing lost commands.

```c
typedef struct {
    uint32_t enqueued;                /* items accepted since boot */
    uint32_t dropped;                 /* items rejected because the queue was full, since boot */
    uint32_t handled;                 /* items that finished running */
    uint32_t totalHandlerMs;          /* summed run time; divide by `handled` for the average */
    uint32_t maxHandlerMs;            /* longest single item */
    void (*slowestHandler)(void *);   /* the handler behind maxHandlerMs (resolve with addr2line) */
    uint16_t queueLen;                /* queue capacity */
    uint16_t pending;                 /* items queued plus the one running, right now */
    uint16_t peakPending;             /* highest `pending` seen since boot */
    uint16_t burstDrops;              /* drops since the queue was last empty */
    uint32_t publishCount;            /* MQTT publishes since boot, from any task */
    uint32_t publishTotalMs;          /* summed publish time: lock wait plus TLS write */
    uint32_t publishMaxMs;            /* longest single publish */
} IOT_CoreWorkStats_t;
```

---

### Raising an event yourself — `IOT_CoreDispatchEvent()`

```c
iot_err_t IOT_CoreDispatchEvent(const IOT_CoreEvent_t *event);
```

Delivers `event` to whatever callback is registered for its category (§3), exactly as if
core had raised it. This is the supported way for your own SDK-side services to emit
events your application handlers see. `event` must not be NULL; `IOT_OK` means the event
was accepted for dispatch.

---

### OTA

> The OTA declarations now live in **`IOT_CoreOta.h`**. `IOT_Core.h` still includes it, so
> existing code compiles unchanged; include `IOT_CoreOta.h` directly in new code.

Core owns the OTA *protocol*; your SDK owns the *download*. Core parses the
cloud's command, acknowledges it, persists the request and raises
`OS_EVENT_OTA_REQUESTED` (§3). It never downloads on its own.

> ### ⚠️ Changed in this release — read this before upgrading
>
> Core now decides for itself whether a boot exists to run a deferred OTA, and
> announces it with a **new event, `OS_EVENT_OTA_BOOT_STARTED`, that you must
> explicitly claim** by setting `data.otaBootStarted.handled = true`.
>
> **An existing `IOT_EVENT_OS` handler does not claim it.** A handler that only
> knows `OS_EVENT_REBOOT` and falls through its `default:` case still returns
> `IOT_OK`, so core cannot tell it apart from no handler at all. In both cases
> core **discards the pending OTA request and reboots to a normal boot** — the
> device is fine and keeps working, but *that update never happens*, silently.
>
> If you use deferred downloads (path B below), you must add the new case. If
> you only ever download inside `OS_EVENT_OTA_REQUESTED` (path A), nothing
> changes for you — no request is ever left pending across a boot, so this event
> never fires.

#### Two ways to run the download — pick one

**Start with path A.** Path B exists only for applications that cannot free
enough contiguous heap while running.

| | **A — download during the event** | **B — defer to an OTA-only boot** |
|---|---|---|
| Where you download | Inside the `OS_EVENT_OTA_REQUESTED` handler | Inside the `OS_EVENT_OTA_BOOT_STARTED` handler, on a later boot |
| Where the URL/token come from | The event payload (`d->url`, …) | `IOT_CoreOtaGetPending()` |
| Who decides the boot is an OTA boot | — | **Core**, from the pending record |
| Reboot **before** downloading | No | Yes — you trigger it yourself |
| Use it when | Dropping BLE frees enough. **Most devices.** | Your app holds large long-lived allocations it cannot release while running |

```c
/* PATH A — the common case. The payload is valid for this dispatch only. */
case OS_EVENT_OTA_REQUESTED: {
    const IOT_CoreOsEventOtaRequestedData_t *d = &osEvent->data.otaRequested;
    IOT_CoreChangeWifiSuspendBle();
    IOT_CoreBleDeinit();
    return IOT_AppManagerStartUpdateProcess(d->url, d->cert, d->certLen,
                                            d->authToken, d->authTokenLen,
                                            NULL, IOT_CoreOtaPlatformProgressCb);
}

/* PATH B — in the handler: just restart. Leave the request pending; core finds
 * it on the next boot. Do NOT call IOT_CoreOtaClearPending() here — that
 * DECLINES the update rather than deferring it. */
case OS_EVENT_OTA_REQUESTED:
    IOT_ApplicationRestartChip();
    return IOT_OK;
```

#### Path B: the OTA boot, and how to claim it

You no longer keep your own "this is an OTA boot" flag. On every boot,
`IOT_CoreInit()` checks the pending record itself. If a download is still owed,
core runs an **OTA-only boot** instead of the normal one:

1. BLE services are skipped, and BLE is released (`IOT_CoreBleDeinit()`) if your
   platform init already brought it up — the device does **not** enter
   provisioning.
2. WiFi and MQTT come up as usual, so download progress and any failure still
   reach the cloud.
3. Once the network is up, core dispatches `OS_EVENT_OTA_BOOT_STARTED`.

Your handler claims the download, then **skips the rest of its own
initialisation** — the heavy services are exactly what the download needs the
heap for:

```c
case OS_EVENT_OTA_BOOT_STARTED:
    osEvent->data.otaBootStarted.handled = true;   /* REQUIRED — claim it */
    app_start_deferred_ota();                      /* your download, see below */
    return IOT_OK;
```

```c
/* In your SDK init, after IOT_CoreInit() returns: if this was an OTA boot, do
 * not start mDNS, the TCP server, the scheduler, or anything else large. */
static void app_start_deferred_ota(void)
{
    if (IOT_CoreOtaGetAttempts() >= APP_OTA_MAX_ATTEMPTS) {
        IOT_CoreOtaClearPending();      /* give up; answer the cloud */
        IOT_ApplicationRestartChip();   /* ONLY if that returned IOT_OK */
        return;
    }
    IOT_CoreOtaRecordAttempt();         /* BEFORE the attempt, never after */

    IOT_CoreOtaRequest_t req;
    if (IOT_CoreOtaGetPending(&req) == IOT_OK) {
        IOT_AppManagerStartUpdateProcess(req.url, req.cert, req.certLen,
                                         req.authToken, req.authTokenLen,
                                         NULL, IOT_CoreOtaPlatformProgressCb);
        IOT_CoreOtaFreePending(&req);
    }
}
```

**Why the flag exists, and what happens without it.** Core requires *both* a
successful dispatch *and* `handled == true`. A dispatch that returns `IOT_OK`
proves only that some `IOT_EVENT_OS` callback is registered — not that anyone is
running this download. So core treats "no handler" and "a handler that ignored
this event" identically: it discards the pending request and reboots itself.

That self-reboot is deliberate, and it is the safe outcome. Falling through
un-rebooted would be far worse: BLE has already been released with nothing left
to bring it back, so every later boot would find the same record pending, take
the OTA path again, and never have BLE again — permanently losing provisioning
*and* the change-WiFi fallback. Core trades that for "this OTA request did not
happen", which is recoverable.

**Count attempts, or a device that dies mid-download boots into OTA forever.**
Core persists the counter; the budget is your policy — core has no opinion on
it. Call `IOT_CoreOtaRecordAttempt()` **before** starting the attempt, so a crash
mid-download still leaves the count advanced.

**Retire the old pattern.** If you previously deferred downloads with your own
NVS flag plus a check early in `app_main()`, delete it. Two mechanisms racing to
own the same pending record is worse than either alone: core has already released
BLE and connected before your check runs.

```c
typedef struct {
    char *url;              // full URL including scheme
    char *authToken;        // bearer token, or NULL
    uint16_t authTokenLen;
    char *cert;             // CA cert PEM, or NULL when none is provisioned
    uint16_t certLen;
    char targetVersion[32]; // empty until the download reaches the writing stage
    uint8_t autoUpdate;
} IOT_CoreOtaRequest_t;

bool      IOT_CoreOtaIsPending(void);
iot_err_t IOT_CoreOtaGetPending(IOT_CoreOtaRequest_t *out); // BLOCKING
void      IOT_CoreOtaFreePending(IOT_CoreOtaRequest_t *req);
iot_err_t IOT_CoreOtaClearPending(void);
iot_err_t IOT_CoreOtaDiscardPending(void);
uint8_t   IOT_CoreOtaGetAttempts(void);
iot_err_t IOT_CoreOtaRecordAttempt(void);
void      IOT_CoreOtaPlatformProgressCb(uint8_t status, uint8_t progress, void *user_data);
```

| Function | Use it when |
|---|---|
| `IOT_CoreOtaIsPending()` | You want to know whether a download is still **owed**, before committing to a boot mode. Safe to call before `IOT_CoreInit()` — it reads only the pending record, not the encrypted URL, and bootstraps the config DAO itself. True *only* while a download is still outstanding; a request that already succeeded, failed or was declined returns false. |
| `IOT_CoreOtaGetPending()` | **Path B only.** Re-reads the request from storage after the dispatch that announced it is over. Returns `IOT_ERR_NOT_FOUND` when nothing is pending. Release with `IOT_CoreOtaFreePending()`. On path A you never call it — the event payload already has everything. |
| `IOT_CoreOtaFreePending()` | Frees the buffers `IOT_CoreOtaGetPending()` allocated. Memory hygiene only — it has **no** protocol meaning and does not cancel anything. |
| `IOT_CoreOtaClearPending()` | You decided **not** to update at all. Core notifies the cloud so the request is answered rather than left hanging, and resets the attempt counter with it. **Not a tidy-up call and not a way to defer.** If the notice cannot be sent, the request is *deferred*, not dropped — record and counter both stay for a later boot with connectivity. **Check the return value**: only `IOT_OK` means the record actually changed, and rebooting on anything else repeats the same give-up forever. |
| `IOT_CoreOtaDiscardPending()` | A notify attempt is known to be pointless — e.g. init failed early enough that the network can never come up on this boot. Erases the record and counter unconditionally, without ever contacting the cloud. Using `IOT_CoreOtaClearPending()` there would fail its send and leave the request deferred rather than gone, re-entering the same failure on every boot. |
| `IOT_CoreOtaGetAttempts()` | Reading how many OTA boots this request has already spent, to enforce your own give-up budget. `0` means a fresh transaction. |
| `IOT_CoreOtaRecordAttempt()` | Immediately **before** each download attempt on an OTA boot. |
| `IOT_CoreOtaPlatformProgressCb()` | Always, unless you have a reason not to. Hand it to the platform downloader and you have no protocol bookkeeping at all: core maps each platform status to the right notification, records the target version, marks the request applied so the next boot verifies it, and drops it on failure. |

`cert` is re-read from storage on every `IOT_CoreOtaGetPending()` call, so you
never have to persist it. Note that `authToken` may have **expired** if the
request sat across a long reboot — report a 401 as a failure so the cloud
reissues rather than retrying silently.

There is no "success" call, on either path. Success is proven by the running
version after the post-write reboot, not claimed by the downloader. Sending is best-effort: with MQTT down
(an OTA-only boot, say) the durable state is still updated and the outcome is
reported after the next boot that has a connection.

> **Two different reboots, and only one is yours.** Path B's reboot happens
> *before* the download, and you choose to take it. The reboot *after* the image
> is written is not optional and not yours — it is how the new firmware starts
> running, and it happens on both paths. "The next boot verifies it" above always
> refers to that second one.

#### `IOT_CoreInit()` return values on the OTA path

| Return | Meaning |
|---|---|
| `IOT_ERR_INVALID_STATE` | The device is not provisioned. An OTA request cannot reach an unprovisioned device, so this should not occur in practice; there is deliberately no BLE fallback here. |
| `IOT_ERR_OTA_BOOT_DEGRADED` | The request was unclaimed **and** core could not even discard the pending record (e.g. an NVS erase failure). Rebooting into that same unresolved state would repeat forever, so core tried to bring BLE back up in place instead. That recovery is best-effort — check the logs, not this value, to know whether BLE actually returned. |

A **normal** boot (no pending record) behaves exactly as before: the call takes
no new parameter, and a caller that never sees an OTA request notices no
difference.

#### Freeing heap for the download

```c
iot_err_t IOT_CoreBleDeinit(void);
iot_err_t IOT_CoreChangeWifiSuspendBle(void);
iot_err_t IOT_CoreChangeWifiResumeBle(void);
```

A TLS download needs a single **contiguous** ~16.7 KB block. A device reporting
80 KB free but a fragmented heap still fails, right after the download starts.
BLE typically holds tens of KB in one piece, so `IOT_CoreBleDeinit()` is the
cheapest remedy; aim for a largest free block of ~25 KB.

Deinitialise through core, not the platform stack directly, or core keeps
believing BLE is up. `IOT_CoreBleDeinit()` refuses with `IOT_ERR_INVALID_STATE` while a
mesh hub role (§2) owns the radio; on the OTA-only boot core logs that and carries on in
OTA mode rather than failing the boot. Suspend the change-WiFi BLE fallback **first**: otherwise a
WiFi drop mid-download tries to advertise on a stack you just removed. Resume it
when the download ends without a reboot.

> Do **not** try to fix the allocation failure by lowering
> `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` below 16384. That trades the failure for a
> mid-download read error once the server sends a record larger than the new cap.

---

## 2. IOT_CoreHubRole.h

> 🚧 **IN DEVELOPMENT — NOT READY. Do not use yet.** This header ships so the
> library links, but the hub-role feature (mesh/Zigbee hub, plus the Zigbee
> sub-device events in `IOT_CoreEventHandler.h`) is unfinished/unvalidated and the
> API may change. SDK writers should ignore this section for now.

### Overview

Startup APIs for **hub-role** devices — devices that manage sub-devices over mesh
and/or Zigbee. Each API owns the full platform lifecycle for its transport
(initialisation, event dispatch, and provisioning where applicable).

These are optional. On a leaf device (or any platform where no driver is
registered) they return `IOT_ERR_NOT_SUPPORTED` — callers should treat this as a
non-fatal skip.

**Call order** (in your SDK's init):

```c
IOT_CoreInit(modelId);
IOT_CoreSetHubRole(IOT_HUB_ROLE_MESH, true);   /* if your SDK runs a mesh hub  */
IOT_CoreHubZigbeeStart();  // returns IOT_ERR_NOT_SUPPORTED if no Zigbee driver
```

`IOT_CoreHubRole.h` includes `IOT_CoreHubRoleFlags.h`, so one include gives you both.

The mesh *transport* is yours: this distribution does not ship a mesh platform
implementation, and core neither starts nor joins a mesh network. What core provides is
the protocol side — parsing the cloud's mesh messages into MESH events (§3), the
`IOT_CoreDeferWork()` hop off small driver stacks (§1), and answering the cloud's
hub-capability question from the role you declare.

### Mesh Hub — now owned by your SDK

**`IOT_CoreHubMeshStart()` / `IOT_CoreHubMeshStop()` no longer exist.** Core does not
initialise the mesh platform, join a network, or manage nodes. It parses the cloud's
mesh messages, hands the payloads to you as events, and answers the cloud's
hub-capability question using the role you declared.

You own the transport lifecycle. Core owns the protocol. The split exists because a
compiled-in mesh radio says only that the hardware is there — a plain mesh node has
that without being a hub — so whether this device *is* a hub is a product decision
core cannot make for you.

Declare the role with `IOT_CoreHubRoleFlags.h`:

```c
void IOT_CoreSetHubRole(IOT_HubRole_t role, bool active);   /* MESH or ZIGBEE */
bool IOT_CoreHasHubRole(IOT_HubRole_t role);
```

**Declare the role at the point your SDK commits to playing it — not after the
transport is fully up.** The cloud only sends the mesh network and application keys
once it believes the device is a hub, and the device cannot complete a join without
those keys. Waiting for a completed join before claiming the role deadlocks.

`IOT_CoreSetHubRole()` is idempotent, roles are independent, and an out-of-range role
is ignored.


### Zigbee Hub

```c
iot_err_t IOT_CoreHubZigbeeStart(void);
bool      IOT_CoreHubZigbeeIsActive(void);
void      IOT_CoreHubZigbeeStop(void);
```

The Zigbee hub lifecycle is still owned by core; it is expected to move to the SDK the
same way the mesh lifecycle did.

`IOT_CoreHubZigbeeIsActive()` is true once `IOT_CoreHubZigbeeStart()` has succeeded. Like
the mesh role, it records your decision to play the hub role, not a platform capability —
a Zigbee transport compiled into the build says only that the radio exists.

`IOT_CoreHubZigbeeStart()`:
- Initialises the Zigbee coordinator (`IOT_ZigbeeCoordinatorInit`).
- Registers internal callbacks that translate coordinator events into
  `IOT_EVENT_DEVICE` events:
  - `onJoined`      → `DEV_EVENT_ZIGBEE_DEVICE_JOINING`
  - `onInterviewed` → `DEV_EVENT_ZIGBEE_DEVICE_INTERVIEWED` (known model) or
    `DEV_EVENT_ZIGBEE_DEVICE_UNKNOWN_MODEL` (unknown model)
  - `onLeft`        → `DEV_EVENT_ZIGBEE_DEVICE_LEFT`

**Returns:** `IOT_OK`, `IOT_ERR_NOT_SUPPORTED` if no coordinator driver, or a
coordinator init error.

`IOT_CoreHubZigbeeStop()` calls `IOT_ZigbeeCoordinatorDeinit()`; no-op with a
warning if the Zigbee hub was never started.

---

## 3. IOT_CoreEventHandler.h

### Overview

Defines all event types dispatched by iotcore to the SDK layer.
Register a callback with `IOT_DevCoreRegisterEventCb()`, then switch on
`event->category` and the nested type field.

`IOT_CoreEventHandler.h` is an **umbrella**. It declares only what spans every category —
the category enum, the `IOT_CoreEvent_t` wrapper and callback registration — and includes
one header per category. Including it still gives you everything; in new code that needs
only one category, include that header instead.

| Header | Holds |
|---|---|
| `IOT_CoreEventTypes.h` | `IOT_SENDER_*`, group and sentinel constants, `IOT_RequestContext_t`, the element-state domain types |
| `IOT_CoreEventState.h` | STATE events — GET/SET requests, sensor-log and Record queries |
| `IOT_CoreEventDevice.h` | DEVICE events — provisioning, WiFi/cloud lifecycle, join/removal, groups, attributes, Zigbee |
| `IOT_CoreEventMesh.h` | MESH events — node reports, relayed messages, key delivery |
| `IOT_CoreEventSetting.h` | SETTING events — WiFi changed |
| `IOT_CoreEventSmart.h` | SMART events — triggers, schedules, sync-on-off, condition groups, bind/unbind |
| `IOT_CoreEventOs.h` | OS events — reboot, OTA requested, OTA boot started |
| `IOT_CoreEventIr.h` | the IR event |
| `IOT_CoreEventService.h` | SERVICE events — a message from your own backend (the downlink half of §5) |

---

### Callback Registration

```c
// Register
iot_err_t IOT_DevCoreRegisterEventCb(IOT_CoreEventCategory_t category,
                                      IOT_CoreEventCb_t callback,
                                      void *arg);

// Unregister
iot_err_t IOT_DevCoreUnregisterEventCb(IOT_CoreEventCategory_t category);

// Callback signature
typedef void (*IOT_CoreEventCb_t)(void *arg, const IOT_CoreEvent_t *event);
```

One callback per category. `IOT_DevCoreRegisterEventCb()` returns `IOT_ERR_INVALID_ARG`
for invalid parameters and `IOT_ERR_INVALID_STATE` if a callback is already registered for
that category; `IOT_DevCoreUnregisterEventCb()` returns `IOT_ERR_NOT_FOUND` when none is.
The callback typedef has C linkage, so a C++ callback is valid.

**Event categories:**

```c
typedef enum {
    IOT_EVENT_STATE,   // GET_STATE, SET_STATE, SENSOR_LOG, RECORD
    IOT_EVENT_DEVICE,  // Provisioning, join, remove, WiFi/MQTT lifecycle, group bind, Zigbee sub-devices
    IOT_EVENT_MESH,    // Mesh: nodes, messages and keys handed to your SDK
    IOT_EVENT_SETTING, // WiFi config change
    IOT_EVENT_SMART,   // Automation: triggers, schedules, bind/unbind
    IOT_EVENT_OS,      // Reboot, OTA
    IOT_EVENT_IR,      // IR commands for your IR hardware
    IOT_EVENT_SERVICE, // A service message from your own backend (§3, SERVICE Events)
} IOT_CoreEventCategory_t;

/* The last category above. Categories run from IOT_EVENT_STATE to it with no gaps. */
#define IOT_EVENT_CATEGORY_LAST IOT_EVENT_SERVICE

typedef struct {
    IOT_CoreEventCategory_t category;
    union {
        IOT_CoreStateEvent_t   state;
        IOT_CoreDeviceEvent_t  device;
        IOT_CoreMeshEvent_t    mesh;
        IOT_CoreSettingEvent_t setting;
        IOT_CoreSmartEvent_t   smart;
        IOT_CoreOsEvent_t      os;
        IOT_CoreIrEvent_t      ir;
        IOT_CoreServiceEvent_t service;
    } evt;
} IOT_CoreEvent_t;
```

**Typical event handler structure:**

```c
static void eventCallback(void *arg, const IOT_CoreEvent_t *event)
{
    switch (event->category)
    {
    case IOT_EVENT_STATE:
        handleState(&event->evt.state);
        break;
    case IOT_EVENT_DEVICE:
        handleDevice(&event->evt.device);
        break;
    // ...
    default:
        break;   // a category added in a later release lands here
    }
}
```

**Registering every category in one loop — `IOT_EVENT_CATEGORY_LAST`.** It names the last
value of `IOT_CoreEventCategory_t` (today `IOT_EVENT_SERVICE`), and the categories run from
`IOT_EVENT_STATE` to it with no gaps. An SDK that sends every category to one dispatcher can
loop to it, and then picks up a category appended in a later release without a code change:

```c
for (int c = IOT_EVENT_STATE; c <= IOT_EVENT_CATEGORY_LAST; c++)
{
    iot_err_t err = IOT_DevCoreRegisterEventCb((IOT_CoreEventCategory_t)c, eventCallback, NULL);
    if (err != IOT_OK)
    {
        /* handle it: a category that is already registered returns IOT_ERR_INVALID_STATE */
    }
}
```

The macro moves when a category is appended, so keep a `default:` in your `switch`. An
event `switch` without one may warn about `IOT_EVENT_SERVICE`, which was appended in this
release.

---

### Request Context

Declared in `IOT_CoreEventTypes.h`.

```c
typedef enum {
    IOT_REQUEST_PROTOCOL_NONE = 0x00, // Unset — a ctx built without .protocol; responses fall back to MQTT
    IOT_REQUEST_PROTOCOL_BLE  = 0x01,
    IOT_REQUEST_PROTOCOL_TCP  = 0x02,
    IOT_REQUEST_PROTOCOL_MQTT = 0x03,
} IOT_RequestProtocol_t;

typedef struct {
    IOT_RequestProtocol_t protocol; // Transport the request arrived on
    uint8_t  senderFrom;    // IOT_SENDER_* (who sent the request)
    uint8_t  senderSize;    // Length of senderId (0 if NULL)
    uint8_t *senderId;      // Identity bytes (voice user ID, app UUID, etc.)
    uint16_t tcpPort;       // TCP port for direct routing (0 if N/A)
    uint16_t tid;           // Transaction ID echoed in responses
    uint8_t *requestId;     // Cloud correlation ID (NULL if none)
    uint16_t requestIdLen;  // 0 if no requestId
    bool     requiresResponse; // false = sender expects no response; Reporter APIs no-op silently
} IOT_RequestContext_t;
```

**SDK usage is pass-through only.** When building a report or response, copy the ctx directly:

```c
report.ctx = setData->ctx;              // cloud-initiated SET: route reply back to sender
report.ctx = (IOT_RequestContext_t){0}; // locally-initiated: no reply routing
```

The SDK should not unpack and re-pack individual fields.
The one exception is `senderFrom` — read it for policy decisions
(e.g. skip re-triggering automations when `senderFrom == IOT_SENDER_AUTOMATION`).

**Sender type constants:**

| Constant | Value | Description |
|----------|-------|-------------|
| `IOT_SENDER_UNKNOWN` | 0x00 | Unknown / local |
| `IOT_SENDER_CONTROL` | 0x01 | Physical button / local control |
| `IOT_SENDER_APP` | 0x0D | Mobile app |
| `IOT_SENDER_CLOUD` | 0x0E | Cloud |
| `IOT_SENDER_IOS` | 0x0F | iOS app |
| `IOT_SENDER_ANDROID` | 0x10 | Android app |
| `IOT_SENDER_DEVICE` | 0x11 | Another device |
| `IOT_SENDER_DEVICE_SYNC` | 0x12 | Another device, synchronisation |
| `IOT_SENDER_SCENARIO_UUID_V4` | 0x13 | Scenario (UUID-identified) |
| `IOT_SENDER_AUTOMATION` | 0x14 | Automation (smid-based) |
| `IOT_SENDER_AUTOMATION_UUID_V4` | 0x15 | Automation (UUID-identified) |
| `IOT_SENDER_SCHEDULE` | 0x16 | Scheduled automation |
| `IOT_SENDER_SCHEDULE_UUID_V4` | 0x17 | Schedule (UUID-identified) |
| `IOT_SENDER_COUNTDOWN` | 0x18 | Countdown timer |
| `IOT_SENDER_GOOGLE_VOICE` | 0x20 | Google Assistant |
| `IOT_SENDER_ALEXA` | 0x21 | Amazon Alexa |
| `IOT_SENDER_OLLI` | 0x22 | Olli voice |
| `IOT_SENDER_THIRD_PARTY` | 0x29 | Third-party integration |

---

### STATE Events

Declared in `IOT_CoreEventState.h`. Access via `event->evt.state`.

```c
typedef enum {
    IOT_EVENT_TYPE_STATE_SET,        // Cloud/app SET command
    IOT_EVENT_TYPE_STATE_GET,        // Cloud/app GET state request
    IOT_EVENT_TYPE_STATE_SENSOR_LOG, // Legacy (V1) sensor log query
    IOT_EVENT_TYPE_STATE_RECORD,     // Record (V2) history query — appended, numbering above unchanged
} IOT_EventTypeState_t;

typedef struct {
    IOT_EventTypeState_t type;
    union {
        IOT_EventStateGetRequest_t       getRequest;
        IOT_EventStateSetRequest_t       setRequest;
        IOT_EventStateSensorLogRequest_t sensorLogRequest;
        IOT_EventStateRecordRequest_t    recordRequest;
    } data;
} IOT_CoreStateEvent_t;
```

#### SET_STATE — `IOT_EventStateSetRequest_t`

Dispatched when cloud sends a SET command. The SDK must:
1. Resolve the target to device EID(s) using `targetType` and the `target` union
2. For each resolved device: update DAO state, actuate hardware, call `IOT_CoreRequestReportStateChanged()`

```c
typedef struct {
    IOT_SetTargetType_t targetType;
    union {
        struct { uint16_t eid; } device;
        struct { uint16_t groupEid; } group;      // > IOT_GROUP_EID_BASE — 0xC000 itself is LOCATION
        struct {
            uint8_t *devId;
            uint16_t devIdLen;
            uint16_t groupEid; // group override (0 if none)
        } voice;
    } target;
    dev_set_info_t devSetInfo;     // Elements, attrId, attrValue to apply
    IOT_RequestContext_t ctx;
    uint8_t targetDeviceType;      // Device type filter (0 = all)
} IOT_EventStateSetRequest_t;
```

```c
typedef enum {
    IOT_SET_TARGET_DEVICE,   // Single device  — use target.device.eid
    IOT_SET_TARGET_LOCATION, // All devices in this location — no target field needed;
                             //   SDK expands via its device DAO
    IOT_SET_TARGET_GROUP,    // Specific group — use target.group.groupEid (> 0xC000)
    IOT_SET_TARGET_VOICE,    // Voice — resolve target.voice.devId to eid via DAO
} IOT_SetTargetType_t;
```

Target type is derived from the EID range in the message, not the protocol
field: `eid < 0xC000` → DEVICE, `eid == 0xC000` → LOCATION, `eid > 0xC000` →
GROUP. VOICE is a separate path (cloud addresses by `devId`); when a voice
command carries a `groupEid`, core resolves it to LOCATION or GROUP instead.

> **Note:** `devSetInfo.eid` is unused for SET events — always use `targetType` + `target` union.

```c
typedef struct { uint16_t elemId; } elem_set_t;

typedef struct {
    uint16_t numOfElems;
    uint16_t eid;          // UNUSED for SET events
    elem_set_t *elemList;
    uint16_t attrId;
    uint16_t attrSize;
    uint8_t *attrValue;
} dev_set_info_t;
```

#### GET_STATE — `IOT_EventStateGetRequest_t`

```c
typedef struct {
    uint16_t eid;
    IOT_RequestContext_t ctx;
    uint8_t *unixTime; // 8-byte big-endian epoch ms, or NULL — see below
} IOT_EventStateGetRequest_t;
```

SDK reads current state from DAO and calls `IOT_CoreRequestReportState()`.

> **Did the caller ask for the time? Use `IOT_CoreTimeRequested(unixTime)`**
> (`IOT_CoreTimeRequest.h`), not `unixTime != NULL`. The cloud sends eight zero bytes to
> mean "not asking", so a non-NULL pointer is not a request. Core judges it with the same
> predicate, so using it keeps your side and core's in agreement — if they disagree, the
> app shows an empty screen and nothing is logged anywhere. If you only pass `unixTime`
> through to `IOT_CoreRequestReportState()`, you get the right behaviour for free.
>
> A device whose clock is not yet synchronised still answers GET_STATE: core omits the
> optional time part, logs a warning, and sends the state.

#### SENSOR_LOG — `IOT_EventStateSensorLogRequest_t`

> **Legacy (Record V1).** This library cannot answer it —
> `IOT_CoreRequestSensorLogResponse()` sends nothing and returns `IOT_ERR_NOT_SUPPORTED`
> (§4). Record V2 (`IOT_EVENT_TYPE_STATE_RECORD`, below) is the supported history path.

```c
typedef struct {
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    uint16_t year;
    uint16_t dayOfYear;
    IOT_RequestContext_t ctx;
} IOT_EventStateSensorLogRequest_t;
```

#### RECORD — `IOT_EventStateRecordRequest_t`

```c
typedef struct {
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    uint64_t checkpoint;    // epoch MILLISECONDS of the block asked for; 0 = the current block
    IOT_RequestContext_t ctx;
} IOT_EventStateRecordRequest_t;
```

A Record V2 history query. The app asks for one *block* of history by its checkpoint and
walks backwards from there. `checkpoint == 0` means both "I have never seen a checkpoint"
(the first request after GET_STATE) and "give me the open block" — there is no third
meaning, and no real block ever carries checkpoint 0 because 0 is also the end-of-chain
sentinel.

Answer it **synchronously, inside your event callback**, with
`IOT_CoreRequestRecordResponse()` (§4). `ctx.senderId` points into a buffer that is freed
when your handler returns, so a reply built later on another task routes through freed
memory.

---

### DEVICE Events

Declared in `IOT_CoreEventDevice.h`. Access via `event->evt.device`.

```c
typedef enum {
    DEV_EVENT_NONE,

    /* Lifecycle — no cloud response. */
    DEV_EVENT_IDENTIFICATION,        // Provisioning started (BLE connected)
    DEV_EVENT_PROVISION_CANCELLED,   // BLE disconnected before provisioning completed
    DEV_EVENT_PROVISION_COMPLETED,   // Device successfully provisioned
    DEV_EVENT_BOOTED,                // Device booted (provisioned or not)
    DEV_EVENT_WIFI_CONNECTED,        // WiFi got IP address
    DEV_EVENT_WIFI_DISCONNECTED,     // WiFi disconnected
    DEV_EVENT_CLOUD_CONNECTED,       // MQTT broker connected
    DEV_EVENT_CLOUD_DISCONNECTED,    // MQTT broker disconnected
    DEV_EVENT_WIFI_CONNECTING,       // WiFi connecting (pre-IP)
    DEV_EVENT_CLOUD_CONNECTING,      // MQTT connecting
    DEV_EVENT_ERROR,                 // Internal error

    /* GET — the status report itself is the response. */
    DEV_EVENT_DEVICE_STATUS_REQUEST, // Cloud requested device status

    /* DAO-completed — call IOT_CoreRespondDeviceCmd() after your DAO work. */
    DEV_EVENT_NEW_DEVICE_JOINED,     // New sub-device sync (3 phases)
    DEV_EVENT_DEVICE_REMOVED,        // Device removed from network
    DEV_EVENT_GROUP_BIND,            // Bind device to group
    DEV_EVENT_GROUP_UNBIND,          // Unbind device from group
    DEV_EVENT_DEVICE_ATTR_SET,       // Set sub-device attribute in DAO

    // Zigbee sub-device events (hub role only). No EID at this stage —
    // the EID is assigned later via a cloud round-trip.
    DEV_EVENT_ZIGBEE_DEVICE_JOINING,       // Device joined network, interview pending
    DEV_EVENT_ZIGBEE_DEVICE_INTERVIEWED,   // Interview succeeded, known model
    DEV_EVENT_ZIGBEE_DEVICE_UNKNOWN_MODEL, // Interview succeeded, unknown model
    DEV_EVENT_ZIGBEE_DEVICE_LEFT,          // Device left the network
} IOT_EventTypeDevice_t;
```

> Enum order is not stable across releases — always switch on the names, never on
> numeric values. Every DAO-completed DEVICE event carries a `ctx`; pass it (and
> `cmdData2` where present) to `IOT_CoreRespondDeviceCmd()` unchanged. That call is a
> silent no-op when `ctx.requiresResponse` is false, so you can call it unconditionally.

`DEV_EVENT_PROVISION_CANCELLED` fires when the app drops the BLE link *before* the
device has been registered with the cloud. Once provisioning is waiting only on the cloud,
the phone routinely closes the link believing it is done, and that drop is not a
cancellation — a successful provisioning emits only `DEV_EVENT_PROVISION_COMPLETED`.

**Lifecycle payloads:**

```c
typedef struct { uint8_t statusCode; } IOT_CoreDeviceEventProvisionCompletedData_t; // 1 completed, 0 failed
typedef struct { int errorCode; }      IOT_CoreDeviceEventErrorData_t;
typedef struct {
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
} IOT_CoreDeviceEventWifiConnectedData_t;
typedef struct { uint8_t reason; }     IOT_CoreDeviceEventWifiDisconnectedData_t;   // IOT_NwkErrorReason_t (IOT_NetworkMgmt.h)
```

Access via `event->evt.device.data.provisionCompleted` / `.error` / `.wifiConnected` /
`.wifiDisconnected` (`.identification` carries no data).

#### DEV_EVENT_NEW_DEVICE_JOINED — 3 phases

Core dispatches this event three times per device join:

```c
typedef enum {
    IOT_DEV_JOIN_PHASE_DEVICE_INFO,  // Identity + common info — save to DAO
    IOT_DEV_JOIN_PHASE_ELM_INFO,     // Element list — initialize state in DAO
    IOT_DEV_JOIN_PHASE_SYNC_COMPLETE // Sync done — persist mesh key if present
} IOT_DevJoinPhase_t;

typedef struct {
    uint16_t elemId;
    uint16_t elemType;
    uint8_t numOfAttr;
    uint16_t *attrIdList; // attribute IDs only; sizes/values come from your attribute table
} IOT_DevJoinElmInfo_t;

typedef struct {
    IOT_DevJoinPhase_t phase;
    IOT_RequestContext_t ctx;   // pass through to IOT_CoreRespondDeviceCmd
    uint8_t cmdData2;           // pass through to IOT_CoreRespondDeviceCmd
    union {
        struct {
            uint8_t devId[12];
            uint8_t mac[6];
            uint8_t macType;
            uint16_t eid;
            uint16_t rootEid;
            uint16_t protocol;
            uint16_t nwkAddr;
            uint16_t manufacturer;
            uint16_t deviceType;
            uint16_t eleNum;
            uint16_t groupId;
        } deviceInfo;
        struct {
            uint16_t eid;
            uint8_t numOfElms;
            IOT_DevJoinElmInfo_t *elmList;
        } elmInfo;
        struct {
            uint16_t eid;
            uint8_t *meshKey;     // 18-byte TLV, PLAINTEXT — see below. NULL if no mesh key
            uint16_t meshKeyLen;  // 0 if no mesh key
        } syncComplete;
    } data;
} IOT_CoreDeviceEventNewDeviceJoinedData_t;
```

Respond with `IOT_CoreRespondDeviceCmd()` (`cmdType = IOT_DEVICE_CMD_SYNC_DEV_JOINED`)
for the DEVICE_INFO and ELM_INFO phases. Core sets `ctx.requiresResponse = false` for
SYNC_COMPLETE, so calling it there is a harmless no-op.

> **`syncComplete.meshKey` is an 18-byte TLV, and it is not encrypted.** The layout is
> block type, length, then the 16-byte mesh device key. Reading it as a bare 16-byte key
> misplaces every byte. Earlier documentation called it an "encrypted" key — it is
> handed to you in the clear, because your SDK is the consumer and needs the plaintext to
> join. Do not try to decrypt it.

#### DEV_EVENT_DEVICE_REMOVED

```c
typedef struct {
    uint16_t eid;
    bool isRootDevice;          // true = factory reset path (SDK should also reset itself and reboot)
    IOT_RequestContext_t ctx;
    uint8_t cmdData2;
} IOT_CoreDeviceEventDeviceRemovedData_t;
```

For a sub-device, remove it from your DAO, then call `IOT_CoreRespondDeviceCmd()` with
`cmdType = IOT_DEVICE_CMD_SYNC_DEV_REMOVED`, passing `cmdData2` and `ctx` through. For the
root device (`isRootDevice == true`), core has already answered the cloud before its
factory reset; reset your own DAOs and restart the chip.

#### DEV_EVENT_GROUP_BIND

```c
typedef struct {
    uint16_t eid;
    uint16_t newGroupId;
    uint16_t oldGroupId;    // 0 if no old group (element-list bind)
    uint8_t *elemData;      // Element bindings [elemId(2B)]... NULL = all elements
    uint16_t elemDataLen;   // 0 if no element bindings
    IOT_RequestContext_t ctx;
    uint8_t cmdData2;       // Protocol metadata echoed by core
} IOT_CoreDeviceEventGroupBindData_t;
```

Save the group mapping in your DAO, then call `IOT_CoreRespondDeviceCmd()` with `cmdType = IOT_DEVICE_CMD_BIND_GRP` (or `IOT_DEVICE_CMD_BIND_GRP_ELMS` when `elemData` is present). Pass `cmdData2` and `ctx` through from the event unchanged.

#### DEV_EVENT_GROUP_UNBIND

```c
typedef struct {
    uint16_t eid;
    uint16_t groupId;       // Group to unbind from
    IOT_RequestContext_t ctx;
    uint8_t cmdData2;       // Protocol metadata echoed by core
} IOT_CoreDeviceEventGroupUnbindData_t;
```

Remove the group mapping from your DAO, then call `IOT_CoreRespondDeviceCmd()` with `cmdType = IOT_DEVICE_CMD_UNBOUND_GRP`. Pass `cmdData2` and `ctx` through from the event unchanged.

#### DEV_EVENT_DEVICE_ATTR_SET

```c
typedef struct {
    uint16_t eid;
    uint16_t elm;
    uint16_t attrId;
    const uint8_t *attrData;
    uint16_t attrDataLen;
    IOT_RequestContext_t ctx;
} IOT_CoreDeviceEventAttrSetData_t;
```

Persist the attribute value in your DAO, then call `IOT_CoreRespondDeviceCmd()` with `cmdType = IOT_DEVICE_CMD_DEVICE_ATTR` and the event `ctx`.

#### Zigbee Sub-Device Events

> 🚧 **IN DEVELOPMENT — NOT READY.** These events belong to the hub role (see
> section 2) and are not ready for use. SDK writers should ignore them for now.

Dispatched only on hub-role devices after `IOT_CoreHubZigbeeStart()`. There is no
EID yet at this stage — the cloud assigns one later. Size constants mirror the
Zigbee coordinator interface:

```c
#define IOT_CORE_ZIGBEE_IEEE_SIZE    8
#define IOT_CORE_ZIGBEE_MODEL_SIZE  32
#define IOT_CORE_ZIGBEE_VENDOR_SIZE 32
```

```c
// DEV_EVENT_ZIGBEE_DEVICE_JOINING
typedef struct {
    uint8_t ieeeAddr[IOT_CORE_ZIGBEE_IEEE_SIZE]; // EUI-64 (big-endian)
    char    friendlyName[IOT_CORE_ZIGBEE_MODEL_SIZE];
} IOT_CoreDeviceEventZigbeeJoiningData_t;

// DEV_EVENT_ZIGBEE_DEVICE_INTERVIEWED / DEV_EVENT_ZIGBEE_DEVICE_UNKNOWN_MODEL
typedef struct {
    uint8_t ieeeAddr[IOT_CORE_ZIGBEE_IEEE_SIZE];
    char    friendlyName[IOT_CORE_ZIGBEE_MODEL_SIZE];
    char    model[IOT_CORE_ZIGBEE_MODEL_SIZE];
    char    vendor[IOT_CORE_ZIGBEE_VENDOR_SIZE];
} IOT_CoreDeviceEventZigbeeInterviewedData_t;

// DEV_EVENT_ZIGBEE_DEVICE_LEFT
typedef struct {
    uint8_t ieeeAddr[IOT_CORE_ZIGBEE_IEEE_SIZE];
} IOT_CoreDeviceEventZigbeeLeftData_t;
```

Access via `event->evt.device.data.zigbeeJoining` / `.zigbeeInterviewed` /
`.zigbeeLeft`.

---

### MESH Events

Declared in `IOT_CoreEventMesh.h`. Dispatched under `IOT_EVENT_MESH`; the hub role is
declared with `IOT_CoreSetHubRole(IOT_HUB_ROLE_MESH, true)` (§2). Core parses; your SDK,
which owns the mesh transport, acts.

```c
typedef enum {
    MESH_EVENT_NONE = 0,
    MESH_EVENT_NODE_JOINED,   // A mesh node joined the network
    MESH_EVENT_NODE_LEFT,     // A mesh node left the network
    MESH_EVENT_MESSAGE,       // Inbound message from a mesh sub-node
    MESH_EVENT_JOINED,        // The hub itself joined the mesh network
    MESH_EVENT_KEYS_RECEIVED, // Mesh network/application key delivered by the cloud
} IOT_EventTypeMesh_t;

typedef struct {
    uint16_t nodeAddr;       // Mesh unicast address of the node
} IOT_CoreMeshEventNodeData_t;

typedef struct {
    uint32_t       opcode;   // normalised by the platform to an IOT_MESH_OPCODE_* value
    uint16_t       srcAddr;  // mesh element address of the sender
    uint16_t       payloadLen;
    const uint8_t *payload;  // dispatcher-owned — valid only during the callback
} IOT_CoreMeshEventMsgData_t;

typedef enum {
    IOT_MESH_KEY_NETWORK = 0,
    IOT_MESH_KEY_APPLICATION = 1,
} IOT_MeshKeyKind_t;

typedef struct {
    IOT_MeshKeyKind_t kind;     // network or application key
    uint8_t           keyIndex; // key index exactly as the cloud sent it
    const uint8_t    *key;      // dispatcher-owned — valid only during the callback
    uint16_t          keyLen;
} IOT_CoreMeshEventKeysData_t;

typedef struct {
    IOT_EventTypeMesh_t type;
    union {
        IOT_CoreMeshEventNodeData_t node;    // NODE_JOINED / NODE_LEFT
        IOT_CoreMeshEventMsgData_t  message; // MESSAGE
        IOT_CoreMeshEventKeysData_t keys;    // KEYS_RECEIVED
    } data;
} IOT_CoreMeshEvent_t;
```

- **`MESH_EVENT_MESSAGE`** — no stack-specific framing reaches you; the opcode is already
  normalised. Copy `payload` if you keep it.
- **`MESH_EVENT_JOINED`** — raised after every successful hub join, whatever triggered it
  (boot, provisioning completion, keys arriving later). Respond by pushing your node cache
  to the platform: that data lives in your device DAO, and core cannot build it.
- **`MESH_EVENT_KEYS_RECEIVED`** — core parses the key off the cloud's message and hands it
  over; persisting it and (re)attempting the join are yours, because the mesh hub is SDK
  logic and core must not own the keys it joins with. Core does not interpret `keyIndex`
  — you decide how many subnets you represent. Copy `key` before returning.

---

### SETTING Events

Declared in `IOT_CoreEventSetting.h`.

```c
typedef enum {
    SETTING_EVENT_WIFI_CHANGED,
} IOT_EventTypeSetting_t;

// SETTING_EVENT_WIFI_CHANGED
typedef struct {
    const char *ssid; // SSID the device just joined
} IOT_CoreSettingWifiChangedData_t;
```

Access via `event->evt.setting.data.wifiChanged`. Dispatched exactly once per **proven**
WiFi change — after the new credentials connected successfully and were stored — on every
transport the change can arrive by. A failed connect rolls back and dispatches nothing,
and storing credentials without joining dispatches nothing. The payload carries the SSID
only (plain text, NUL-terminated); the password never leaves core. `ssid` is core-owned
and valid only during your callback — copy it to keep it.

---

### SMART Events

Declared in `IOT_CoreEventSmart.h`. Access via `event->evt.smart`; the payload union
member for each event is named after it (`data.bindTrigger`, `data.activeByTrigger`,
`data.unboundTrigger`, `data.activeBySyncOnOff`, …).

```c
typedef enum {
    /* Broadcast to every device in the location — only the device that owns the SMID
     * acts. Send nothing if you hold no such SMID. */
    SMART_EVENT_ACTIVE_BY_USER,             // Manually triggered (scene run)
    SMART_EVENT_ACTIVE_BY_TRIGGER,          // Trigger condition met
    SMART_EVENT_ACTIVE_BY_SCHEDULE,         // Schedule fired
    SMART_EVENT_REMOVE_ANNOUNCE,            // Automation deleted
    SMART_EVENT_SCHEDULE_REMOVE_ANNOUNCE,   // Schedule deleted
    SMART_EVENT_TRIGGER_UPDATE_ANNOUNCE,    // Trigger definition updated
    SMART_EVENT_SCHEDULE_UPDATE_ANNOUNCE,   // Schedule updated

    /* DAO-completed — call IOT_CoreRespondSmartCmd() after your DAO work. */
    SMART_EVENT_TRIGGER_MODE,               // Trigger mode changed
    SMART_EVENT_ENABLE_DISABLE,             // Enable/disable a smart entity by SMID  ✅ ready
    SMART_EVENT_BIND_TRIGGER,               // Bind trigger to device
    SMART_EVENT_UNBOUND_TRIGGER,            // Unbind trigger
    SMART_EVENT_BIND_CMD,                   // Bind command to automation
    SMART_EVENT_UNBOUND_CMD,                // Unbind command

    /* DAO-completed — call IOT_CoreRespondSmartCheckSync() after your DAO read. */
    SMART_EVENT_CHECK_SYNC,                 // Cloud requesting sync check

    /* Device-to-device — no response call. */
    SMART_EVENT_ACTIVE_BY_SYNC_ON_OFF,      // Sync-on-off broadcast (subSmartType 71)  ✅ ready
    SMART_EVENT_REPORT_COND_STATE,          // Cond-group peer status report (subSmartType 2048)  ✅ ready
    SMART_EVENT_SYNC_COND_REQUEST,          // Cond-group reboot re-sync query (subSmartType 2048)  ✅ ready
    SMART_EVENT_COND_NOT_MEMBER,            // Cond-group "not a member" reply (subSmartType 2048)  ✅ ready
} IOT_EventTypeSmart_t;
```

> Enum order is not stable across releases — always use the names, not numeric values.

> **"Not mine" is success, not failure.** The broadcast events above reach every device
> in the account, and most of them are not the target. A device that holds no binding for
> the SMID should quietly do nothing and report nothing went wrong. Keep a genuine failure
> on an SMID you *do* own (a storage error, say) as a real error.

> **`SMART_EVENT_ACTIVE_BY_TRIGGER`: the trigger key is optional.** Some senders omit it;
> the event still reaches you, with `triggerKey` **all zeros**. Treat all-zero as "not
> supplied", not as "owned by EID 0", and match on `smid`.

**Trigger mode** (`SMART_EVENT_TRIGGER_MODE`, `data.triggerMode`) carries one of four
sub-types:

```c
typedef enum {
    IOT_CORE_SMART_TRIGGER_MODE_NONE,
    IOT_CORE_SMART_TRIGGER_MODE_SET_TRIGGER = 1,                  // data.setTrigger { minuteDisable, smid }
    IOT_CORE_SMART_TRIGGER_MODE_SCHEDULE = 2,                     // data.schedule { smid }
    IOT_CORE_SMART_TRIGGER_MODE_ENABLE_DISABLE_AUTOMATION = 10,   // data.enableDisableAutomation { eid }
    IOT_CORE_SMART_TRIGGER_MODE_ENABLE_DISABLE_ALL_AUTOMATION = 255
} IOT_CoreSmartTriggerModeType_t;
```

Implement **all four** — leaving one as "not implemented" and answering `IOT_OK` makes the
app show a state the device ignores. For "all automations", sweep every trigger SMID you
hold; do not abort the sweep on a missing SMID, and treat zero automations as success.
(Per-SMID enable/disable normally arrives as `SMART_EVENT_ENABLE_DISABLE`.)

**Enable/disable** (`data.enableDisable`) carries `smid`, `enabled` (1 = enable,
0 = disable) and `ctx`. Answer with `IOT_CoreRespondSmartCmd()`, setting `smid` — see
*SMART Command Response* below.

**Bind trigger** (`data.bindTrigger`, `IOT_CoreSmartEventBindTriggerData_t`) carries the
trigger key, the trigger element info (`smid`, `smartType`, `subSmartType`, `eid`,
`elmId`, `condition`, `cfm`), the primary and secondary attribute values, the mixed
trigger, the time window (`timeStart`, `timeStop`, `weekDay` — bit0 = Sunday … bit6 =
Saturday, `0x00` = every day), `isDeviceUtc` / `timeZoneFraction`, `ctx`, and — for
cond-groups — `hasCondGroup`, `condIndex`, `condCount`, `fireCount`, `condVersion`.

**Schedule update** (`data.scheduleUpdateAnnounce`) carries `smid` and an array of
`scheduleTimeCount` minute-of-week values in **UTC**; core frees the array after dispatch,
so copy it.
> `SMART_EVENT_ENABLE_DISABLE` and `SMART_EVENT_ACTIVE_BY_SYNC_ON_OFF` are **ready** to
> handle (see the Sync On/Off section for how to consume the payload and how to
> broadcast). `SMART_EVENT_REPORT_COND_STATE` / `SMART_EVENT_SYNC_COND_REQUEST` /
> `SMART_EVENT_COND_NOT_MEMBER` back the **distributed cond-group** (AND/OR, K-of-N)
> automation and are now **ready** — see the Cond-Group section for the payload fields,
> the merge rules, and the reporter calls.

All SMART events carry a `smid` (Smart automation ID) identifying the automation.
SDK must persist trigger/schedule/command bindings in its own DAO.

---

### OS Events

```c
typedef enum {
    OS_EVENT_REBOOT,           // core requests a system reboot (e.g. after factory reset)
    OS_EVENT_OTA_REQUESTED,    // the cloud asked for a firmware update
    OS_EVENT_OTA_BOOT_STARTED, // this boot exists only to run a deferred download
} IOT_CoreOsEventType_t;

typedef struct {
    IOT_CoreOsEventType_t type;
    union {
        IOT_CoreOsEventOtaRequestedData_t otaRequested;     // valid for OS_EVENT_OTA_REQUESTED
        IOT_CoreOsEventOtaBootStartedData_t otaBootStarted; // valid for OS_EVENT_OTA_BOOT_STARTED
    } data;
} IOT_CoreOsEvent_t;
```

> ⚠️ Register a callback for `IOT_EVENT_OS`. Without it the firmware-update
> request never reaches your SDK and the device silently stops accepting OTAs —
> core hands the download to you and has no fallback of its own.

> ⚠️ **Registering the callback is no longer sufficient for
> `OS_EVENT_OTA_BOOT_STARTED`** — that one must be *claimed* by setting a flag.
> A `default:` case that ignores it looks exactly like no handler at all to
> core. See below.

#### OS_EVENT_OTA_BOOT_STARTED — `IOT_CoreOsEventOtaBootStartedData_t`

```c
typedef struct {
    bool handled;  // set true to CLAIM the download; read by core after dispatch returns
} IOT_CoreOsEventOtaBootStartedData_t;
```

Dispatched when `IOT_CoreInit()` found a pending OTA request on entry and ran an
**OTA-only boot** rather than the normal one. By the time you receive it, BLE has
already been released and WiFi + MQTT are already up. This is the deferred-download
path — see §1, *OTA* for the full flow and the attempt-counter API.

**You must set `handled = true` to claim it.** Core sets it to `false` before
dispatching and reads it back immediately afterwards (dispatch is synchronous).
Returning `IOT_OK` is *not* a claim: the same `IOT_OK` comes back from a callback
registered for an entirely different OS event — `OS_EVENT_REBOOT`, which every
partner already handles — that fell through its `default:` case ignoring this one.
Core therefore requires **both** a successful dispatch **and** `handled == true`.

Leaving it false — no handler at all, or a handler that never sets it — is treated
identically in both cases: core discards the pending request and reboots into a
normal boot. Nothing is bricked, but **the update silently does not happen**.

```c
case OS_EVENT_OTA_BOOT_STARTED:
    osEvent->data.otaBootStarted.handled = true;   /* claim it, then... */
    app_start_deferred_ota();                      /* ...and skip your heavy init */
    return IOT_OK;
```

> Core releases BLE on this path with nothing left to bring it back, which is why
> it reboots rather than continuing when nobody claims the download: falling
> through would leave every later boot taking the same OTA path with no BLE,
> permanently losing provisioning and the change-WiFi fallback.

#### OS_EVENT_OTA_REQUESTED — `IOT_CoreOsEventOtaRequestedData_t`

```c
typedef struct {
    const char *url;        // full URL including scheme. Never NULL.
    const char *authToken;  // bearer token, or NULL when the server needs none
    uint16_t authTokenLen;  // 0 when authToken is NULL
    const char *cert;       // CA cert PEM provisioned for HTTPS, or NULL if unset
    uint16_t certLen;       // 0 when cert is NULL
    uint8_t autoUpdate;     // cloud's auto-update preference, already persisted by core
} IOT_CoreOsEventOtaRequestedData_t;
```

Core parses the cloud's update command, acknowledges it and persists the
request — but it does **not** download. Only your SDK knows what heap the
application is holding, and that is what decides whether the download can
succeed at all.

The pointers are **borrowed**: valid only for the duration of the synchronous
dispatch. To act on the request later — after a reboot, say — call
`IOT_CoreOtaGetPending()` rather than stashing them.

The simplest correct handler passes the fields straight to the platform
downloader together with core's own progress callback:

```c
case OS_EVENT_OTA_REQUESTED: {
    const IOT_CoreOsEventOtaRequestedData_t *d = &osEvent->data.otaRequested;

    // A TLS download needs a single CONTIGUOUS block of ~16.7 KB. Releasing the
    // BLE stack usually frees tens of KB in one piece; without it a device with
    // plenty of free-but-fragmented heap fails as soon as the download starts.
    IOT_CoreChangeWifiSuspendBle();   // mute the fallback BEFORE removing the stack
    IOT_CoreBleDeinit();

    iot_err_t err = IOT_AppManagerStartUpdateProcess(d->url, d->cert, d->certLen,
                                                     d->authToken, d->authTokenLen,
                                                     NULL, IOT_CoreOtaPlatformProgressCb);
    if (err != IOT_OK) {
        IOT_CoreChangeWifiResumeBle();
        IOT_CoreOtaClearPending();    // answer the cloud instead of leaving it waiting
    }
    break;
}
```

See `IOT_CoreOta.h` (§1, *OTA*) for the pending-request API and the deferred
download pattern, and `examples/esp32-basic/main/main.c` for a worked handler (there it is
the `onCoreEvent` callback of `sdkcontract/`, which receives the OS events unchanged).

---

### IR Events

Declared in `IOT_CoreEventIr.h`. Dispatched under `IOT_EVENT_IR`; access via
`event->evt.ir`.

Core owns the IR *protocol* — parsing the cloud's IR commands and framing the replies.
The IR *hardware* is yours: this distribution does not ship an IR platform driver, so you
implement transmit/receive against your own hardware (the platform interface is in
`iotPlatform/include/ir/IOT_IRControl.h`) and connect it to these events.

```c
typedef enum {
    IOT_IR_EVENT_EXEC_RAW,          // raw timing pairs in `data`: send verbatim
    IOT_IR_EVENT_EXEC_RAW_ZIP,      // compressed timing data: decompress, then send
    IOT_IR_EVENT_EXEC_PRTC,         // protocol id + payload: send via an IR protocol encoder
    IOT_IR_EVENT_QUERY_CAPABILITY,  // cloud asked what IR this device supports: fill capMode/capDetect
    IOT_IR_EVENT_STORE_KEYSET,      // packed keyset for `eid` in data/dataLen: store it
    IOT_IR_EVENT_EXEC_AC,           // AC state command: brand in `protocol`, state in ac* fields
    IOT_IR_EVENT_LEARN_START,       // open a detect/learn window; you answer LATER
} IOT_EventTypeIr_t;

typedef struct {
    IOT_EventTypeIr_t type;
    /* Inputs — borrowed, valid only during the synchronous dispatch. */
    uint16_t eid;        // target sub-device EID (STORE_KEYSET)
    uint16_t protocol;   // IR protocol id (EXEC_PRTC), AC brand (EXEC_AC)
    uint8_t *header;     // compressed-data header (EXEC_RAW_ZIP)
    uint16_t headerLen;
    uint8_t *data;       // raw blob / compressed data / protocol code bytes / packed keyset
    uint16_t dataLen;
    /* Outputs — written by your handler, read by core after dispatch returns. */
    iot_err_t result;    // EXEC/STORE result reported back to the cloud
    uint8_t capMode;     // QUERY_CAPABILITY: 0 = TX only, 1 = RX only, 2 = TX & RX
    uint8_t capDetect;   // QUERY_CAPABILITY: 0 = no detect, 1 = detect supported
    /* EXEC_AC state */
    uint8_t acPower;     // 0 = off, 1 = on
    uint8_t acMode;      // iot_attr_mode_t
    uint8_t acTemp;      // target temperature, °C
    uint8_t acFan;       // iot_attr_fan_speed_t
    uint8_t acSwing;     // swing position/mode
    /* LEARN_START only */
    uint8_t learnMode;        // 0x01 = capture raw without decoding; otherwise decode
    IOT_RequestContext_t ctx; // routing for the deferred reply
} IOT_CoreIrEvent_t;
```

Dispatch is synchronous: do the work, write `result` (and the capability fields for
`QUERY_CAPABILITY`) before returning, and copy any input you need afterwards.

**`IOT_IR_EVENT_LEARN_START` is the one asynchronous event in this category.** Its `result`
means "the window was opened", not "a code was captured". Run the learn window on a task
of your own — never block the event callback or a timer callback for the length of a
capture — and report the outcome later with `IOT_CoreReportIrDetected()` or
`IOT_CoreReportIrLearnFailed()` (§4), passing a copy of this event's `ctx`. The copy must
be deep: `ctx.senderId` and `ctx.requestId` point into buffers that are gone once the
callback returns, so copy the bytes they reference too. A handler that
does not know `LEARN_START` falls through to `default`, and core answers the learn command
as unsupported, so existing handlers keep working.

---

### SERVICE Events

Declared in `IOT_CoreEventService.h`. Dispatched under `IOT_EVENT_SERVICE`; access via
`event->evt.service`. This is the downlink half of the service messages in §5: a message
your own backend sent to this device.

```c
typedef enum {
    IOT_SERVICE_EVENT_MSG,   // a JSON message in `json` / `jsonLen`
} IOT_EventTypeService_t;

typedef struct {
    IOT_EventTypeService_t type;
    /* Input — borrowed, valid only during the synchronous dispatch. Copy what you keep. */
    const char *json;    // the message, NUL-terminated for convenience; see jsonLen
    uint16_t jsonLen;    // its length in bytes, 1..IOT_SERVICE_MSG_PAYLOAD_MAX, excluding the NUL
    /* Output — written by your handler. */
    iot_err_t result;    // preset to IOT_ERR_NOT_SUPPORTED
} IOT_CoreServiceEvent_t;
```

- **The content is yours.** Core hands the message over unread; it never parses or validates
  it. `json` is NUL-terminated and `jsonLen` bytes long. An empty message, or one longer than
  `IOT_SERVICE_MSG_PAYLOAD_MAX`, is refused before your handler runs.
- **Your `result` is the acknowledgement.** Write `IOT_OK` to tell the sender the device took
  the message; any error tells the sender the device refused it. If you write nothing, or
  register no `IOT_EVENT_SERVICE` callback at all, the sender is told the device does not
  support service messages (`result` is preset to `IOT_ERR_NOT_SUPPORTED`).
- **Do not block in the handler.** It runs on core's event task, and every other cloud and
  app command waits behind it. `json` is valid only during the call: copy it, hand the copy
  to a task of your own, and return.
- **No reply path.** To answer, publish with `IOT_CoreServiceMsgPublish()` (§5) on a channel
  of your own.

```c
static void on_service_event(void *arg, const IOT_CoreEvent_t *event)
{
    IOT_CoreServiceEvent_t *svc = (IOT_CoreServiceEvent_t *)&event->evt.service;
    if (svc->type != IOT_SERVICE_EVENT_MSG)
        return;
    /* my_service_queue_copy(): your own code — copies json/jsonLen and hands it to your task */
    svc->result = my_service_queue_copy(svc->json, svc->jsonLen);
}

IOT_DevCoreRegisterEventCb(IOT_EVENT_SERVICE, on_service_event, NULL);
```

On `sdkcontract/` you do not register this callback yourself: the event reaches your
`onCoreEvent` unchanged (see `sdkcontract/README.md`).

---

### Group & EID Constants

Declared in `IOT_CoreEventTypes.h`.

```c
#define IOT_GROUP_EID_BASE      0xC000  // the LOCATION address — NOT a group. See below.
#define IOT_GROUP_EID_SECONDARY 0xC001  // Secondary group (auto-added during mesh provisioning)
#define IOT_INVALID_U16         0xFFFF  // Invalid/unset sentinel for 16-bit IDs
```

> ### ⚠️ `0xC000` is LOCATION, not a group — despite the constant's name
>
> | EID | meaning | what to do |
> |---|---|---|
> | **`== 0xC000`** | **LOCATION** — every device you own | apply to **all** your devices. **No group lookup.** |
> | **`> 0xC000`** (e.g. `0xC15E`) | a real group | expand its members from your group DAO |
>
> Core decides this for you and reports it as `targetType`
> (`IOT_SET_TARGET_LOCATION` vs `IOT_SET_TARGET_GROUP`). **Read `targetType`; do not
> compare the EID yourself.**
>
> An earlier version of this document said *"EIDs >= this are group addresses"*, which
> reads as including `0xC000`. It was wrong. Code written to it —
> `if (eid >= 0xC000) treat_as_group()` — sends every whole-room command down the group
> path, where it fails: there is no group record with id `0xC000`. Single-device control
> keeps working, so it looks like rooms are specifically broken.

---

## 4. IOT_CoreReporter.h — outbound calls

### Overview

Functions for sending messages to cloud. The SDK calls these in response to
events (or proactively from hardware/timer callbacks). Core handles all message
encoding, encryption, and MQTT delivery.

`IOT_CoreReporter.h` is now an **umbrella that declares nothing**. The declarations live in
headers named for the domain you work in; including the umbrella still gives you the first
four, exactly as before:

| Header | Holds | In the umbrella? |
|---|---|---|
| `IOT_CoreState.h` | GET_STATE answers, state-change reports, device status, DEVICE command response | yes |
| `IOT_CoreSmart.h` | trigger reports, sync-on-off, condition groups, SMART command and CHECK_SYNC responses | yes |
| `IOT_CoreSensorLog.h` | legacy (V1) sensor-log response | yes |
| `IOT_CoreIr.h` | IR keyset request, IR learn results | yes |
| `IOT_CoreRecord.h` | Record (V2) response and push | **no — include it yourself** |
| `IOT_CoreTimeRequest.h` | `IOT_CoreTimeRequested()` | no |

---

### GET_STATE Response — `IOT_CoreRequestReportState()`

Declared in `IOT_CoreState.h`.

```c
typedef struct {
    uint16_t eid;
    devStateElemsInfo_t *fullState; // Full device state — SDK retains ownership
    IOT_RequestContext_t ctx;       // Pass through from IOT_EventStateGetRequest_t.ctx
    uint8_t *unixTime;              // Echo from getData->unixTime (NULL if not requested)

    // Each recorded attribute's CURRENT history block (optional, NULL/0 when none)
    const IOT_CoreRecordStateBlock_t *recordBlocks;
    uint8_t numRecordBlocks;        // capped at IOT_CORE_RECORD_STATE_BLOCKS_MAX (4)
} IOT_CoreStateResponse_t;

iot_err_t IOT_CoreRequestReportState(const IOT_CoreStateResponse_t *response);
```

**Usage:**
```c
case IOT_EVENT_TYPE_STATE_GET: {
    IOT_EventStateGetRequest_t *getData = &stateEvent->data.getRequest;
    // ... build fullState from DAO ...
    IOT_CoreStateResponse_t response = {
        .eid       = getData->eid,
        .fullState = &fullState,
        .ctx       = getData->ctx,   // pass-through
        .unixTime  = getData->unixTime,
    };
    IOT_CoreRequestReportState(&response);
}
```

Pass `unixTime` through unchanged; core attaches the time only when
`IOT_CoreTimeRequested(unixTime)` is true, and answers with the state alone if the
device's clock is not yet synchronised.

**Always build the response with a designated initialiser (or zero it).** The two
`recordBlocks` fields were appended in v2026.10.24; an initialiser like the one above
leaves them zero, which means "no record blocks". A response declared on the stack and
filled field by field hands core an uninitialised pointer.

**Attaching record blocks (optional).** If your SDK keeps Record V2 history, you can put
each recorded attribute's *current* block on the GET_STATE reply, so the app learns where
that history starts without sending a separate history query. `IOT_CoreRecordStateBlock_t`
(in `IOT_CoreRecord.h`) is one block plus the identity needed to encode it:

```c
typedef struct {
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    uint8_t valueCount;              // 1, or 2 for the power pair: IOT_AttrRecordValueCount(attrId)
    IOT_CoreRecordBlock_t block;     // the same block type a Record V2 reply uses
} IOT_CoreRecordStateBlock_t;
```

```c
#include "IOT_CoreRecord.h"

IOT_CoreRecordStateBlock_t blocks[IOT_CORE_RECORD_STATE_BLOCKS_MAX];
uint8_t count = 0;
// TODO: fill blocks[] from your record store -- the open block of each attribute
//       recorded for getData->eid, but ONLY if its checkpoint is a real UTC time.

IOT_CoreStateResponse_t response = {
    .eid             = getData->eid,
    .fullState       = &fullState,
    .ctx             = getData->ctx,
    .unixTime        = getData->unixTime,
    .recordBlocks    = count ? blocks : NULL,
    .numRecordBlocks = count,
};
IOT_CoreRequestReportState(&response);
```

- Core encodes the blocks only when `IOT_CoreTimeRequested(unixTime)` is true, the same
  rule as the time block. A checkpoint means nothing to an app that did not ask the time.
- At most `IOT_CORE_RECORD_STATE_BLOCKS_MAX` (4) are sent; extras are ignored. Every
  block enlarges the reply, and that memory comes from the same heap a TLS handshake
  needs. The app reaches older blocks, and any attributes beyond the fourth, with the
  usual Record V2 history query (`IOT_EVENT_TYPE_STATE_RECORD`).
- **Leave out a block whose checkpoint is not yet a real time**, for example values
  recorded before the clock first synced. An app anchors its history to that checkpoint.
- A hub attaches only its **own** blocks. Each sub-device is queried by its own EID.
- `core` copies the blocks while it builds the reply, so the array can live on your
  stack.

#### `IOT_CoreTimeRequested()` — `IOT_CoreTimeRequest.h`

```c
static inline bool IOT_CoreTimeRequested(const uint8_t *unixTime);
```

True when `unixTime` (8-byte big-endian epoch milliseconds, or NULL) is a real request:
non-NULL **and** not eight zero bytes. It is defined inline in the header, so it costs
no link dependency. Use it anywhere you would have written `unixTime != NULL` — for
example when deciding whether a time-sync failure matters to your reply.

---

### State Change Report — `IOT_CoreRequestReportStateChanged()`

Declared in `IOT_CoreState.h`. Called after applying a SET command, or proactively when local hardware state changes.

```c
typedef struct {
    uint16_t eid;
    uint8_t *mac;
    uint8_t  macLen;
    uint8_t *devId;
    uint8_t  devIdLen;
    devStateElemsInfo_t *fullState;   // Complete device state — SDK retains ownership
    uint16_t numChangedElems;
    uint16_t *changedElemIds;
    uint16_t changedAttrId;
    bool pushNotify;                  // true = cloud sends push notification (alerts only)
} IOT_CoreStateChangeDeviceData_t;

typedef struct {
    IOT_CoreStateChangeDeviceData_t *devices;
    uint8_t numDevices;
    IOT_RequestContext_t ctx; // Pass through from SET event, or {0} for local changes
} IOT_CoreStateChangeReport_t;

iot_err_t IOT_CoreRequestReportStateChanged(const IOT_CoreStateChangeReport_t *report);
```

**Responding to a cloud SET:**
```c
IOT_CoreStateChangeReport_t report = {
    .devices    = &devData,
    .numDevices = 1,
    .ctx        = setData->ctx,  // route reply back to sender
};
IOT_CoreRequestReportStateChanged(&report);
```

**Proactive report (button press, sensor, timer):**
```c
IOT_CoreStateChangeReport_t report = {
    .devices    = &devData,
    .numDevices = 1,
    .ctx        = (IOT_RequestContext_t){0}, // no reply routing
};
IOT_CoreRequestReportStateChanged(&report);
```

> **Batch reports:** Set `numDevices > 1` to send state changes for multiple devices
> in a single MQTT message (e.g. after a group command).

---

### SENSOR_LOG Response — `IOT_CoreRequestSensorLogResponse()` (legacy V1)

Declared in `IOT_CoreSensorLog.h`.

> ⚠️ **Not available in this library.** Sensor log is Record **V1**, and the shipped
> `libiotcore.a` is built for Record **V2** only. Here `IOT_CoreRequestSensorLogResponse()`
> sends **nothing** — not even an empty reply — logs an error, and returns
> `IOT_ERR_NOT_SUPPORTED`; the app's sensor-log query receives no answer and times out.
> Use Record V2 (`IOT_EVENT_TYPE_STATE_RECORD` + `IOT_CoreRequestRecordResponse()`, below)
> for history. The declaration below is documented so existing code keeps compiling.

```c
typedef struct {
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    uint16_t year;
    uint16_t dayOfYear;

    const uint16_t *minutes;  // minutes since LOCAL midnight, one per entry, any order
    const uint8_t  *values;   // count * valueStride bytes; entry i at values[i * valueStride]
    uint16_t count;           // number of entries; 0 is valid (an empty log)
    uint8_t  valueStride;     // bytes per value AS SUPPLIED
    uint8_t  logValueBytes;   // bytes of each value to LOG; 0 = derive from attrId

    IOT_RequestContext_t ctx;
} IOT_CoreSensorLogResponse_t;

iot_err_t IOT_CoreRequestSensorLogResponse(const IOT_CoreSensorLogResponse_t *response);
```

> **Breaking: `logData` / `logDataLen` are gone.** The struct now takes entries — facts —
> rather than a pre-packed byte blob. Core owns the wire layout, the value width, the
> ordering (newest first) and the truncation policy; delete any packing code. All arrays
> are borrowed for the duration of the call. `logValueBytes = 0` derives the width from
> `attrId`, which is correct for every standard attribute; set it for a partner custom
> attribute in the `0xF700`–`0xFEFF` range (see
> [Custom_Attributes.md](Custom_Attributes.md)), or to keep a width core does not know.
> On a build that supports V1 the call returns `IOT_ERR_INVALID_ARG` for a NULL entry array
> or a zero `valueStride` with a non-zero `count`.

---

### Record Response — `IOT_CoreRequestRecordResponse()`

Declared in `IOT_CoreRecord.h` — **include it directly**; the `IOT_CoreReporter.h`
umbrella does not. This answers `IOT_EVENT_TYPE_STATE_RECORD` (§3) and replaces the
day-keyed sensor log.

You hand core facts; core owns every wire byte. Do not pre-pack entries, byte-swap, or pad
to a block size. `attrId` is opaque to core — which is why `valueCount` is yours to supply.

```c
typedef struct {
    uint64_t checkpoint;      // epoch MILLISECONDS this block opened — origin of its time axis
    uint64_t prevCheckpoint;  // checkpoint of the next OLDER block; 0 = oldest, ends the chain
    const uint16_t *fields;   // host-order entries, oldest to newest (see below)
    uint16_t fieldCount;      // total uint16s in `fields` — NOT the entry count
} IOT_CoreRecordBlock_t;

typedef struct {
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    uint8_t  valueCount;                 // 1, or 2 for a paired reading (the power pair)
    const IOT_CoreRecordBlock_t *blocks; // NEWEST first; at least one, always
    uint16_t blockCount;
    IOT_RequestContext_t ctx;
} IOT_CoreRecordResponse_t;

iot_err_t IOT_CoreRequestRecordResponse(const IOT_CoreRecordResponse_t *response);
```

**Entry layout in `fields`**, in natural order (core does any reordering the cloud
expects):

| `valueCount` | each entry |
|---|---|
| 1 | `[value][diffSeconds]` |
| 2 | `[value1][value2][diffSeconds]` |

`diffSeconds` is measured from the block's `checkpoint`. Entries within a block run oldest
to newest — the opposite of the block order. `fieldCount` must divide by
`valueCount + 1`; core checks it. Get `valueCount` from `IOT_AttrRecordValueCount(attrId)`
in `reference/defines/IOT_AttrDefines.h` rather than deriving it from the attribute size —
some attributes record fewer fields than their wire value carries, and a wrong count makes
the app parse the history on the wrong stride with no error at either end.

**Rules:**

- **`blockCount` is never 0.** For an unknown or evicted checkpoint, reply with **one**
  block whose `checkpoint` is the value that was requested, `prevCheckpoint = 0` and no
  entries. Core never invents a block; a reply carrying checkpoint 0 would collide with
  both the end-of-chain sentinel and the "current block" request.
- **Call it synchronously, inside the event callback.** `ctx.senderId` is freed when your
  handler returns.
- Arrays are borrowed for the call only; one scratch buffer may be refilled between
  blocks.
- Core frames the blocks in the order given and stops when the reply reaches its size
  budget instead of failing; the app fetches the rest by asking for the last
  `prevCheckpoint` it received.
- A Record query arriving over **BLE** is refused with `IOT_ERR_NOT_SUPPORTED` — a reply
  runs to about a kilobyte and the BLE path does not fragment. Query over MQTT or LAN.

**Returns:** `IOT_OK`, or `IOT_ERR_INVALID_ARG` for a NULL response, `blockCount == 0`, or a
`valueCount` outside 1..2.

---

### Record Push — `IOT_CoreReportRecordBlocks()`

Also in `IOT_CoreRecord.h`. Sends **closed** Record blocks to the cloud unsolicited, so
history reaches the cloud without the app having to ask for it. Optional: an SDK that never
calls it behaves exactly as before.

```c
#define IOT_CORE_RECORD_PUSH_BLOCKS_MAX 4

typedef struct {
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    uint8_t  valueCount;                 // 1, or 2 for the power pair: IOT_AttrRecordValueCount(attrId)
    const IOT_CoreRecordBlock_t *blocks; // OLDEST first -- the opposite of IOT_CoreRecordResponse_t
    uint16_t blockCount;                 // 1..IOT_CORE_RECORD_PUSH_BLOCKS_MAX
} IOT_CoreRecordPush_t;

iot_err_t IOT_CoreReportRecordBlocks(const IOT_CoreRecordPush_t *push);
```

Blocks use the same `IOT_CoreRecordBlock_t` and entry layout as the response above.

**Rules:**

- **One series per call, oldest first, 1 to 4 blocks.** A call is all or nothing; core never
  trims a push.
- **Push only blocks that have closed and are already in your flash store** — never the
  block you are still appending to.
- **Keep a "last pushed" bookmark per series and move it only on `IOT_OK`.** Any other
  result means the block was not sent: leave the bookmark and retry later. Keep the bookmark
  by your own sequence number, not by checkpoint — a backward clock step reorders
  checkpoints.
- **Delivery is at-least-once.** The cloud keeps blocks by checkpoint and a later copy
  replaces an earlier one, so a resend after a reboot is harmless.
- **It blocks** until the message is handed to the cloud connection. Call it from a task —
  never from a timer callback, and never while holding your own Record lock.
- **Pace a backlog.** After a reconnect, send it a call at a time with a gap between calls;
  a burst fills the outgoing queue and starves the TLS session of heap.
- Check with Rogo that your tenant's cloud accepts pushed history before you turn pushing on.

**Returns:**

| Code | Means | Do |
|---|---|---|
| `IOT_OK` | Handed to the cloud connection | Move your bookmark |
| `IOT_ERR_INVALID_ARG` | NULL, `blockCount` 0 or > 4, `valueCount` outside 1..2 | Fix the caller |
| an encoder error | A malformed block, e.g. a `fieldCount` that does not divide by `valueCount + 1` | Fix the caller |
| `IOT_ERR_INVALID_STATE` | The cloud session is not fully up yet (this is later than the MQTT connect event) | Retry soon |
| any other non-OK | Not sent | Keep the bookmark, retry later |

The Record rules for both ways of building an SDK — what to record, when a block closes, how
the app walks the chain — are in **[../sdk/7_Record_Contract.md](../sdk/7_Record_Contract.md)**.
On `sdkcontract/` its Record sends every reply and push, so you never call these yourself; for a
Record of your own, the lifecycle is in **[../sdk/6_Record_History.md](../sdk/6_Record_History.md)**.

---

### Device Status Report — `IOT_CoreRequestReportDeviceStatus()`

Declared in `IOT_CoreState.h`.

```c
#define IOT_DEVICE_MODE_MASTERSLAVE  0x01
#define IOT_DEVICE_MODE_MASTER       0x02
#define IOT_DEVICE_MODE_SLAVE        0x03
#define IOT_DEVICE_MODE_INDEPENDENT  0x04  // stands alone; the usual value

typedef struct {
    const char *firmwareVersion;  // e.g. "1.2.3"
    int64_t uptimeSeconds;
    int64_t lastActivationTime;   // Unix timestamp of last state change (0 if unknown)
    uint8_t protocolFlags[6];     // [mode, mesh, zigbee, matter, reserved, reserved] — see below
    uint8_t ramUsagePercent;      // 0-100
    uint8_t nvsUsagePercent;      // 0-100
    uint32_t totalRamKB;
    uint32_t totalNvsKB;
} IOT_CoreDeviceStatusData_t;

iot_err_t IOT_CoreRequestReportDeviceStatus(const IOT_CoreDeviceStatusData_t *status);
```

Called on cloud GET request (`DEV_EVENT_DEVICE_STATUS_REQUEST`) and on MQTT reconnect.

`protocolFlags` is what the cloud reads to decide what this device is:

- **`[0]` mode** — one of `IOT_DEVICE_MODE_*`. There is deliberately no zero value: `0` is
  "unset", and a device reporting it cannot be classified (it will not be shown as a hub).
  Most devices report `IOT_DEVICE_MODE_INDEPENDENT`.
- **`[1]` mesh, `[2]` zigbee, `[3]` matter** — `1` only if this device can actually serve
  that transport, else `0`. Do not set a flag for a transport the build cannot serve: the
  cloud will offer capabilities that then fail.

---

### Smart Trigger Report — `IOT_CoreRequestSmartTriggerReport()`

Declared in `IOT_CoreSmart.h`, as are the sync-on-off and cond-group sections that follow
and the SMART command / CHECK_SYNC responses under *DAO-Completed Response APIs*.

```c
typedef struct {
    uint16_t smid;
    uint16_t attrId;
    uint16_t attrSize;
    const uint8_t *attrValue;
    uint8_t triggerKey[8]; // ownerEid(2) + ownerCount(2) + extEid(2) + extCount(2)
    uint8_t timeCfg[4];    // type(2) + value(2)
} IOT_CoreSmartTriggerReport_t;

iot_err_t IOT_CoreRequestSmartTriggerReport(const IOT_CoreSmartTriggerReport_t *report);
```

Called when a trigger condition is met locally. Cloud receives this and dispatches
bind commands to target devices for cross-device automations.

### Sync On/Off (subSmartType 71) — ✅ ready

> **Replaces attribute-sync (subSmartType 1024)**, which has been removed. See the
> migration note in `CHANGELOG.md`.

Sync-on-off lets a set of devices that share one SMID keep their ON/OFF state in
lock-step **directly, without a cloud round-trip**. When the state changes on one
device, it broadcasts to every device in the location, and every peer bound to the
same SMID mirrors the value (one-to-many). It has two halves — a **receive** side
(an event you handle) and a **broadcast** side (a reporter function you call).

**Binding is yours to store.** The cloud binds a sync-on-off group by sending a
trigger with `subSmartType == 71`, delivered as `SMART_EVENT_BIND_TRIGGER`. Unlike an
ordinary trigger, its **primary attribute value is not a match value** — it is the
sync-on-off elements attribute (id **315**), whose value format is the list of
elements this group drives on *this* device. Each element ID is a 2-byte big-endian
value; see the attribute documentation for the definitive format.

Decode that list and persist "this SMID ↔ these local elements" in your own DAO. Core
does not remember the binding for you. The synced attribute is always ON/OFF, so there
is no per-binding attribute to store.

```c
case SMART_EVENT_BIND_TRIGGER:
{
    IOT_CoreSmartEventBindTriggerData_t *data = &smartEvent->data.bindTrigger;

    if (data->bindTriggerElmInfo.subSmartType == MY_SUB_TYPE_SYNC_ON_OFF &&
        data->primaryAttrValue.attrId == MY_ATTR_SYNC_ONOFF_ELEMENTS)
    {
        uint16_t elems[8];
        uint8_t  n = 0;
        for (uint16_t off = 0; off + 2 <= data->primaryAttrValue.attrSize && n < 8; off += 2)
        {
            elems[n++] = (uint16_t)((data->primaryAttrValue.attrValue[off] << 8) |
                                     data->primaryAttrValue.attrValue[off + 1]);
        }
        // TODO: dao_save_sync_binding(data->bindTriggerElmInfo.smid, elems, n);
    }
    // ... normal bind handling + IOT_CoreRespondSmartCmd() as usual
    break;
}
```

> **Constants are yours to define.** Core headers do not declare sub-type or attribute
> IDs — take them from `docs/core/Attribute_Reference.md` and define them in your own SDK
> (`reference/defines/IOT_AttrDefines.h` ships the attribute ids as a reference you can copy).
> For sync-on-off that is sub-type **71**, the elements attribute **315**, and your own
> ON/OFF attribute id. Sub-type numbers are not in the reference header — they are not
> attributes.

#### Receive side — `SMART_EVENT_ACTIVE_BY_SYNC_ON_OFF`

A peer broadcast arrives as this SMART event. Core hands you:

```c
typedef struct {
    uint16_t attrId;
    uint16_t attrSize;
    uint8_t *attrValue;   // borrowed — valid only during the synchronous dispatch
} IOT_CoreSmartBlockAttrValue_t;

typedef struct {
    uint16_t                    smid;       // which sync-on-off group
    IOT_CoreSmartBlockAttrValue_t attrValue; // { attrId, attrSize, attrValue[] } — the new value
    uint16_t                    senderEid;  // broadcaster's root EID; 0 = legacy sender
} IOT_CoreSmartEventActiveBySyncOnOffData_t;
```

What to do in your handler:

1. Read `data->smid` and look up the element LIST your DAO bound to it (the
   `subSmartType == 71` binding). If you have no binding for this SMID, ignore.
2. **Echo-suppress:** the broadcast reaches every device in the location, *including
   the sender*. Fetch your own root EID with `IOT_CoreGetRootEid()`; if
   `data->senderEid != 0 && data->senderEid == myRootEid`, drop the event.
3. Apply `attrValue.attrValue[0..attrSize)` to **every element in the list** — this
   is the difference from an ordinary trigger, which drives a single element — then
   report the new element state as you would for any local change. The pointer is
   borrowed; copy the bytes if you need them after the handler returns.

```c
case SMART_EVENT_ACTIVE_BY_SYNC_ON_OFF:
{
    IOT_CoreSmartEventActiveBySyncOnOffData_t *data = &smartEvent->data.activeBySyncOnOff;

    uint16_t myRootEid = 0;
    if (IOT_CoreGetRootEid(&myRootEid) == IOT_OK &&
        data->senderEid != 0 && data->senderEid == myRootEid)
    {
        break;  // our own broadcast reflected back — ignore
    }

    // TODO: elems = dao_lookup_sync_binding(data->smid);   // your DAO
    //       for each element in elems:
    //           apply(element, data->attrValue.attrValue, data->attrValue.attrSize);
    break;
}
```

#### Broadcast side — `IOT_CoreRequestSmartSyncOnOffReport()`

```c
typedef struct {
    uint16_t smid;
    uint16_t attrId;
    uint16_t attrSize;
    const uint8_t *attrValue;   // caller retains ownership
} IOT_CoreSmartSyncOnOffReport_t;

iot_err_t IOT_CoreRequestSmartSyncOnOffReport(const IOT_CoreSmartSyncOnOffReport_t *report);
```

Call this **when the ON/OFF state changes locally on a device that holds a
sync-on-off binding** — e.g. the user physically toggles the element, or a cloud SET
changes it. Core builds the broadcast (including your own root EID for echo
suppression) and broadcasts it to every device in the location; peers receive it as the
event above. Returns `IOT_ERR_INVALID_ARG` if `report` or its attribute value is
missing/empty.

```c
uint8_t value = 1;  // new ON/OFF state
IOT_CoreSmartSyncOnOffReport_t report = {
    .smid      = boundSmid,   // the SMID your DAO stored for this element
    .attrId    = attrId,
    .attrSize  = sizeof(value),
    .attrValue = &value,
};
IOT_CoreRequestSmartSyncOnOffReport(&report);
```

> Do **not** re-broadcast a value you just applied from an incoming
> `SMART_EVENT_ACTIVE_BY_SYNC_ON_OFF` — only broadcast changes that originate
> locally. Combined with the `senderEid` echo-suppression above, this keeps a
> group of peers from ping-ponging the same update.

#### `IOT_CoreSmartStairSwitchMirror()` — legacy stub

```c
iot_err_t IOT_CoreSmartStairSwitchMirror(uint16_t pairRootEid, uint16_t pairEid,
                                         uint16_t pairElemId, bool on);
```

A legacy compatibility function. It is always declared and always links, but in this
library it is a stub that returns `IOT_ERR_NOT_SUPPORTED` and sends nothing. Do not build
on it: **sync-on-off (subSmartType 71), above, is the supported mechanism** for keeping
devices' ON/OFF state in step.

---

### Cond-Group / AND-OR automations (subSmartType 2048) — ✅ ready

An ordinary automation has one condition on one device. A **cond-group** spreads the
conditions of a single automation across several devices and fires only when **K of N**
of them hold at once — "turn the porch light on when the door opens **and** it is dark",
where the door sensor and the lux sensor are different devices.

No device sees the whole picture on its own, and there is no cloud round-trip at fire
time. Each member owns exactly **one seat** in the group and keeps a small scoreboard:

| Field | Meaning |
|---|---|
| `condCount` (**N**) | how many conditions the group has |
| `fireCount` (**K**) | how many must hold before the automation fires |
| `condIndex` | **this** device's seat, `0..7` (max 8 members) |
| `mask` | the latched bitmask — bit *i* is seat *i*'s condition |
| `version` | the cloud-assigned config generation for this SMID |

Whenever its own condition flips, a member broadcasts its seat bit; peers merge it and
whoever sees `popcount(mask) >= K` runs the automation's commands (delivered to it as the
ordinary `SMART_EVENT_ACTIVE_BY_TRIGGER`). Core carries the messages; **your SDK owns the
scoreboard, persists it, and decides when to fire.**

#### Two rules that are not optional

**1. A device is authoritative only for its own bit.** An incoming report carries the
sender's full `mask`, but you merge only *their* bit at *their* `condIndex` and keep your
own. Consuming the rest lets a stale peer overwrite fresher bits.

**2. Merge by `version`, never by arrival order.** Cloud bumps `version` whenever the
group is reconfigured. Higher version → adopt the sender's `version` **and** its `N`/`K`,
then take its bit. Equal version → take its bit only. Lower version → ignore the shape,
it is stale.

#### Receiving — three events

```c
// A peer announced its seat bit (also used for each group inside a sync response).
typedef struct {
    uint16_t smid;
    uint16_t eid;           // element/device EID the condition belongs to
    uint16_t senderRootEid; // sender's root EID — echo suppression; 0 = absent
    uint16_t version;       // sender's config generation for this smid
    uint8_t condCount;      // N (<= 8)
    uint8_t fireCount;      // K
    uint8_t condIndex;      // sender's seat
    uint8_t mask;           // sender's latched mask; its own bit = (mask >> condIndex) & 1
    uint8_t askForFire;     // see "ask before fire" below
    uint8_t departing;      // see "a member is removed" below
} IOT_CoreSmartCondStatus_t;

typedef IOT_CoreSmartCondStatus_t IOT_CoreSmartEventReportCondStateData_t;   // SMART_EVENT_REPORT_COND_STATE

typedef struct {                                                             // SMART_EVENT_SYNC_COND_REQUEST
    IOT_CoreSmartCondStatus_t *groups; // handler-owned; freed after dispatch returns
    uint16_t groupCount;
    uint16_t requesterEid;             // unicast the answer here; 0 = absent
} IOT_CoreSmartEventSyncCondRequestData_t;

typedef struct {                                                             // SMART_EVENT_COND_NOT_MEMBER
    uint16_t smid;         // the group the responder is NOT in
    uint16_t responderEid; // who answered (diagnostics)
} IOT_CoreSmartEventCondNotMemberData_t;
```

```c
case SMART_EVENT_REPORT_COND_STATE:
{
    IOT_CoreSmartCondStatus_t *d = &smartEvent->data.reportCondState;
    // Look up your stored group for d->smid; ignore it if you hold no trigger
    // (you are not a member). Then: drop self-originated echoes, apply the version
    // rule, merge ONLY (d->mask >> d->condIndex) & 1 into bit d->condIndex of your
    // own mask, persist, and fire if popcount(mask) >= your effective K.
    app_cond_merge_peer(d);
    break;
}
```

> **On `sdkcontract/` none of this is yours:** the contract implements condition groups
> itself (see [../sdk/8_Smart_Contract.md](../sdk/8_Smart_Contract.md)). **For an SDK of your
> own**, a complete implementation shipped in `examples/esp32-basic/main/main.c` up to the
> `v2026.10.27` tag — search for `app_cond_`. It is written to be read and lifted: the merge,
> the effective-K calculation, the report funnel, the local-change broadcast and the reboot
> re-sync, with `TODO: persist` at each point a real device needs storage. Start there rather
> than from this page.

#### Sending — the reporter calls

| Call | When |
|---|---|
| `IOT_CoreRequestSmartReportCondState()` | your own condition flipped — broadcast your seat bit |
| `IOT_CoreRequestSmartSyncCondRequest()` | after a reboot — ask peers *and* announce your state in one message |
| `IOT_CoreRequestSmartSyncCondResponse()` | you differ from an asker — unicast back to it |
| `IOT_CoreRequestSmartSyncCondPush()` | a bind changed your `N`/`K`/`version` — announce so peers catch up |
| `IOT_CoreRequestSmartCondNotMember()` | you were asked about a SMID you hold no trigger for |

```c
typedef struct {
    uint16_t smid;
    uint16_t eid;
    uint16_t version;
    uint8_t condCount;
    uint8_t fireCount;
    uint8_t condIndex;
    uint8_t mask;
    uint8_t askForFire;
    uint8_t departing;
} IOT_CoreSmartReportCondState_t;

iot_err_t IOT_CoreRequestSmartReportCondState(const IOT_CoreSmartReportCondState_t *report);
iot_err_t IOT_CoreRequestSmartSyncCondRequest(const IOT_CoreSmartSyncCondRequest_t *request);
iot_err_t IOT_CoreRequestSmartSyncCondPush(const IOT_CoreSmartReportCondState_t *groups,
                                           uint16_t groupCount);
iot_err_t IOT_CoreRequestSmartSyncCondResponse(const IOT_CoreSmartReportCondState_t *reports,
                                               uint16_t reportCount, uint16_t targetEid);
iot_err_t IOT_CoreRequestSmartCondNotMember(uint16_t smid, uint16_t targetEid);
```

> **Fill every field of the report struct explicitly.** These structs are commonly
> declared as stack locals; a stray `departing` byte makes every peer retire your seat.

#### Ask before fire

A device that reaches K may still be acting on a scoreboard that went stale while it was
offline. Set `askForFire = 1` on a sync request to make the pending fire *conditional* on
the answers, and leave it `0` on an ordinary reboot re-sync — with `0`, peers that hold no
trigger for the SMID stay silent instead of each emitting a reply per unknown SMID.

#### A member is removed

Cloud sends the unbind **only to the removed device** — the survivors are never told the
group shrank, and would otherwise wait forever on a seat that no longer exists. Two
consequences you must handle:

**The unbind carries the new shape, and it is the only copy in existence.**
`SMART_EVENT_UNBOUND_TRIGGER` now delivers it:

```c
typedef struct {
    uint16_t eid;
    uint16_t smid;
    IOT_RequestContext_t ctx;
    uint8_t hasCondGroup;  // 0 = ordinary automation, or an older sender: ignore the rest
    uint8_t condCount;     // N after the removal
    uint8_t fireCount;     // K after the removal
    uint16_t condVersion;  // cloud-assigned config generation
} IOT_CoreSmartEventUnboundTriggerData_t;
```

Relay it to the peers with `departing = 1` **before** you delete your trigger — that is the
farewell that tells them which seat is gone.

**Retire the named seat; do not adopt the announced `N`.** Cloud does **not** renumber
seats. A surviving 2-of-2 member keeps `condIndex = 1` while being told `N = 1`, and after
a middle seat leaves the survivors sit at seats 0 and 2 inside an announced `N = 2`.
Adopting `N` strands the higher survivor and makes the lower one clip a live member's bit.
So on a report with `departing = 1`: mark seat `condIndex` retired, lower your effective
threshold to `min(K, live seats)`, **persist that** — a member that only retires the seat in
RAM fires correctly until the next reboot and then stalls, because the departed device never
comes back to re-announce — and leave your own `N` alone so your seat index still fits.

`departing` cannot be inferred from a smaller `N`: a *surviving* member re-bound by cloud
with a smaller group announces a smaller `N` too, and retiring **its** seat would be wrong.

> **Known gap.** If the removed device is offline when it is removed, nobody relays the
> farewell and the survivors only recover on their next periodic re-sync. Retiring a seat
> on silence instead was considered and rejected: silence also means "temporarily offline",
> and that would fire an automation on fewer conditions than the user configured. A late
> fire is recoverable; a false one is not.

> **Constants are yours to define.** As with sync-on-off, core declares no sub-type or
> attribute IDs — cond-group is sub-type **2048**; take the rest from
> `docs/core/Attribute_Reference.md`, or start from `reference/defines/IOT_AttrDefines.h`.

---

### IR — `IOT_CoreIr.h`

The outbound half of IR support; the inbound half is *IR Events* in §3. As there, core
owns the protocol only — the IR hardware driver is yours.

#### Keyset request

```c
iot_err_t IOT_CoreRequestIrData(uint16_t eid, uint16_t deviceType,
                                const uint8_t *devId, uint16_t devIdLen);
```

Asks the cloud to push an IR sub-device's keyset (the appliance's code book). Call it when
an IR sub-device joins; the keyset then arrives as `IOT_IR_EVENT_STORE_KEYSET`. `eid` is the
appliance sub-device, `deviceType` its device type, and `devId` its cloud-assigned device
ID (may be NULL with `devIdLen = 0`).

#### Learn results

```c
#define IOT_IR_RAW_REPORT_MAX 2048   // bytes; a larger raw capture is REFUSED, not truncated

typedef struct {
    bool raw;               // true → report learned timings; false → report a detected protocol
    uint16_t protocol;      // decoded protocol, or protocol + model for an AC
    uint8_t rogoIrProtocol; // IR protocol family (decoded protocol vs AC), supplied by you
    uint16_t deviceType;    // appliance device type
    uint16_t manufacturer;  // appliance manufacturer id
    const uint8_t *attrs;   // AC only: the AC's attribute list; NULL otherwise
    uint16_t attrsLen;
    const uint8_t *code;    // decoded value, big-endian, ceil(bits/8) bytes; empty for an AC
    uint8_t codeLen;
    const uint8_t *rawData; // big-endian uint16 timing pairs (raw == true)
    uint16_t rawLen;        // bytes in rawData; must be <= IOT_IR_RAW_REPORT_MAX
} IOT_CoreIrDetectResult_t;

iot_err_t IOT_CoreReportIrDetected(const IOT_CoreIrDetectResult_t *result,
                                   const IOT_RequestContext_t *ctx);
iot_err_t IOT_CoreReportIrLearnFailed(uint16_t errorCode, const IOT_RequestContext_t *ctx);
```

Report the outcome of a learn window opened by `IOT_IR_EVENT_LEARN_START`, passing the
`ctx` you copied from that event; core answers on the transport the original request came
in on. **You** decide what was found — the protocol-to-device-type mapping is SDK-side
knowledge — and core owns only the wire format:

- **Decoded, not an AC** — `raw = false`, fill `protocol`, `deviceType`, `manufacturer` and
  the value in `code`/`codeLen`. Without `code` the reply says which remote was seen but
  not which key, and the cloud has nothing to store.
- **Decoded AC** — `raw = false`, fill `attrs` with the AC state and leave `code` empty. An
  AC frame is an AC regardless of its bit count; the 64-bit limit applies only to a non-AC
  `code`.
- **Raw capture** — `raw = true`, timings in `rawData`/`rawLen`.

`IOT_CoreReportIrDetected()` returns `IOT_ERR_INVALID_ARG` for a NULL argument or raw mode
with no data, and `IOT_ERR_INVALID_SIZE` if `rawLen` exceeds `IOT_IR_RAW_REPORT_MAX`.

**Answer every window, including failures.** The learn command is acknowledged the moment
the window opens, so a window you never report looks to the app exactly like a device
that died mid-request. On timeout or a failed capture, call
`IOT_CoreReportIrLearnFailed()` with your error code; core replies with a failure result
carrying it. It returns `IOT_ERR_INVALID_ARG` when `ctx` is NULL.

---

## 5. IOT_CoreServiceMsg.h — partner business messages

Publish your **own** data, in your **own** format, to your **own** backend, over the MQTT
session the device already has — a periodic report, an alert list, a usage counter, a
reading with no attribute ID. The payload is opaque to core: it is never framed, stamped,
encrypted or inspected. Messages your backend sends **to** the device arrive as the
`IOT_EVENT_SERVICE` event (§3, *SERVICE Events*).

```c
#define IOT_SERVICE_MSG_PAYLOAD_MAX     1024  // bytes per publish
#define IOT_SERVICE_MSG_CHANNEL_MAX_LEN 32    // characters, excluding the NUL

iot_err_t IOT_CoreServiceMsgPublish(const char *channel, const void *payload, size_t len);
```

```c
#include "IOT_CoreServiceMsg.h"

/* ... once connected ... */
iot_err_t err = IOT_CoreServiceMsgPublish("report", myJson, myJsonLen);
err = IOT_CoreServiceMsgPublish("alerts", myAlerts, myAlertsLen);
```

**Each publish names a channel** — the kind of message, such as `"report"` or `"alerts"` —
so one device can feed several streams of your backend. Ask us which channel names your
backend listens on. Core supplies the rest of the routing from its own configuration once
the device is provisioned, so routing is not yours to get wrong.

> **Changed in v2026.10.30.** `IOT_CoreConfigureServiceMsg()` and
> `IOT_SERVICE_MSG_PROJECT_ID_MAX_LEN` are removed, and the publish call takes the channel
> as its first argument. Delete the configure call and pass a channel on every publish.
> Code that was not migrated fails to compile, on purpose: a project id passed where a
> channel is expected would publish to a destination nobody reads.

**The channel** is used verbatim — no case change, no truncation — and is validated on every
call, even before `IOT_CoreInit()`: 1..`IOT_SERVICE_MSG_CHANNEL_MAX_LEN` printable ASCII
characters, excluding `/`, `+` and `#`. A bad channel returns `IOT_ERR_INVALID_ARG` or
`IOT_ERR_INVALID_SIZE` and sends nothing.

**`IOT_CoreServiceMsgPublish()`** sends `len` bytes (1..`IOT_SERVICE_MSG_PAYLOAD_MAX`)
byte for byte — binary content and embedded NULs are fine — at QoS 0, no retain.

- **`IOT_OK` does not mean delivered.** It means the MQTT client wrote the message to the
  socket. At QoS 0 there is no acknowledgement and nothing tells you later that a message
  was lost; the return value is the only signal you get.
- **Offline messages are dropped, not queued.** Core will not decide for you whether a
  stale reading is still worth sending. Retry from your own code, using
  `IOT_CoreIsCloudConnected()` or the `DEV_EVENT_CLOUD_CONNECTED` /
  `DEV_EVENT_CLOUD_DISCONNECTED` events.
- **Put an idempotency key in your payload.** The transport does not redeliver, but *your*
  retries after a failed publish create duplicates, and something downstream has to tell a
  retry from a new event.
- **It blocks for up to about ten seconds** (client lock, then the TLS write). Never call it
  from a FreeRTOS timer callback.
- **Pace yourself.** It shares the TLS session the cloud and OTA depend on, and core does not
  rate-limit or silently drop to protect the link.

| Return | Meaning |
|---|---|
| `IOT_OK` | handed to the MQTT client (see above for what this does not promise) |
| `IOT_ERR_INVALID_ARG` | `channel` is NULL, empty or has a rejected character; `payload` is NULL; or `len` is 0 |
| `IOT_ERR_INVALID_SIZE` | `channel` is longer than `IOT_SERVICE_MSG_CHANNEL_MAX_LEN`, `len` exceeds `IOT_SERVICE_MSG_PAYLOAD_MAX`, or the device's routing configuration does not fit |
| `IOT_ERR_NOT_INITIALIZED` | `IOT_CoreInit()` never ran |
| `IOT_ERR_NOT_PROVISIONED` | the device has never been onboarded — possibly a long wait |
| `IOT_ERR_DATA_NOT_FOUND` | provisioned, but the partner identity could not be read |
| `IOT_ERR_NO_NETWORK_CONNECT` | no usable session right now — retry on reconnect |
| `IOT_ERR_NO_MEM` | transient allocation failure — retry |
| `IOT_ERR_TIMEOUT` | another task held the lock too long — retry |

> `IOT_ERR_NOT_PROVISIONED` (never onboarded) and `IOT_ERR_DATA_NOT_FOUND` (onboarded, value
> missing) are distinct here. `IOT_CoreGetRootEid()` predates the former and still reports
> "not provisioned" as `IOT_ERR_DATA_NOT_FOUND`.

---

## 6. IOT_CoreHeapProbe.h — heap diagnostics (dev only)

```c
typedef struct {
    uint32_t freeBytes;        // total free bytes
    uint32_t largestFreeBlock; // biggest single allocatable block
    uint32_t freeBlocks;       // number of free holes
} IOT_HeapProbeSample_t;

void IOT_HeapProbeSample(IOT_HeapProbeSample_t *out);
void IOT_HeapProbeFormatDelta(const IOT_HeapProbeSample_t *before,
                              const IOT_HeapProbeSample_t *after,
                              char *out, size_t outSize);
void IOT_HeapProbeLogDelta(const char *label, const IOT_HeapProbeSample_t *before);
```

Answers *did this scope fragment the heap?* — which free heap alone cannot: under
fragmentation total free barely moves while the largest free block (the one a TLS session
needs) collapses and the heap gains holes, so a sample carries all three numbers.

> **No-ops in the shipped library.** The measuring bodies are compiled only when core is
> built with `CONFIG_IOT_HEAP_PROBE_ENABLED`, and `libiotcore.a` in this distribution is built
> without it — setting the option in your own `sdkconfig` does not change a prebuilt library.
> The calls link and are safe to leave in: `IOT_HeapProbeSample()` returns zeros,
> `IOT_HeapProbeFormatDelta()` writes an empty string, and `IOT_HeapProbeLogDelta()` does
> nothing. For a real measurement, sample your platform's heap counters directly (on
> ESP-IDF: free size, largest free block and minimum free size), taken *during* the suspect
> operation rather than before it.

---

## Domain Types

Shared types used across events and reporter APIs (declared in `IOT_CoreEventTypes.h`,
included by `IOT_CoreEventHandler.h`):

```c
typedef struct {
    uint16_t attrId;
    uint16_t attrSize;
    uint8_t *attrValue;
} elemStateAttr_t;

typedef struct {
    uint16_t elemId;
    uint16_t elemType;
    uint8_t numOfAttr;
    elemStateAttr_t *attrList;
} devStateElemState_t;

typedef struct {
    uint16_t numOfElem;
    devStateElemState_t *elemList;
} devStateElemsInfo_t;
```

---

## DAO-Completed Response APIs

After completing DAO operations for SMART, DEVICE, or CHECK_SYNC commands, call the
corresponding Reporter API so core sends the cloud notification with the correct result.
These APIs are no-ops when `ctx.requiresResponse` is false — i.e. when the sender expects no response.

### SMART Command Response

```c
typedef enum {
    IOT_SMART_CMD_BIND_TRIGGER,
    IOT_SMART_CMD_UNBOUND_TRIGGER,
    IOT_SMART_CMD_TRIGGER_MODE,
    IOT_SMART_CMD_BIND_CMD,
    IOT_SMART_CMD_UNBOUND_CMD,
    IOT_SMART_CMD_ENABLE_DISABLE,
} IOT_SmartCmdType_t;

typedef struct {
    IOT_SmartCmdType_t   cmdType;  // Which SMART command this response is for
    uint16_t             smid;     // ENABLE_DISABLE only — ignored otherwise
    uint16_t             cfm;      // Confirmation value (0 if not applicable, e.g. BIND_CMD/UNBOUND_CMD)
    iot_err_t            result;   // IOT_OK = success, else failure
    IOT_RequestContext_t ctx;      // Pass through from the original event unchanged
} IOT_CoreSmartCmdResponse_t;

iot_err_t IOT_CoreRespondSmartCmd(const IOT_CoreSmartCmdResponse_t *response);
```

For BIND_TRIGGER, UNBOUND_TRIGGER, TRIGGER_MODE, BIND_CMD and UNBOUND_CMD, core answers
with `cfm` on success or the error on failure; the outcome is the whole reply.

**ENABLE_DISABLE** goes through the same call but is answered in a different shape that
core builds for you: the reply carries this device's identity alongside the automation ID,
so the cloud can attribute it. Set **`smid`** for this `cmdType`; `cfm` is
unused. ENABLE_DISABLE additionally stays silent on `result == IOT_ERR_NOT_FOUND` — a device
that does not hold the SMID must not answer at all, so one command yields one reply rather
than one per device. Pass `IOT_ERR_NOT_FOUND` through for "not mine".

> Earlier versions of this document omitted `smid` from the struct. It sits between
> `cmdType` and `cfm`, so use designated initialisers (`.cmdType = …, .cfm = …`) rather
> than positional ones.

### DEVICE Command Response

Declared in `IOT_CoreState.h` (the SMART and CHECK_SYNC responses are in `IOT_CoreSmart.h`).

```c
typedef enum {
    IOT_DEVICE_CMD_BIND_GRP,
    IOT_DEVICE_CMD_BIND_GRP_ELMS,
    IOT_DEVICE_CMD_UNBOUND_GRP,
    IOT_DEVICE_CMD_SYNC_DEV_REMOVED,
    IOT_DEVICE_CMD_SYNC_DEV_JOINED,
    IOT_DEVICE_CMD_DEVICE_ATTR,
} IOT_DeviceCmdType_t;

typedef struct {
    IOT_DeviceCmdType_t  cmdType;  // Which DEVICE command this response is for
    uint8_t              cmdData2; // Raw echo value — meaning varies by command
    iot_err_t            result;   // IOT_OK = success, else failure
    IOT_RequestContext_t ctx;      // Pass through from the original event unchanged
} IOT_CoreDeviceCmdResponse_t;

iot_err_t IOT_CoreRespondDeviceCmd(const IOT_CoreDeviceCmdResponse_t *response);
```

### CHECK_SYNC Response

```c
typedef struct {
    uint16_t             smid;
    bool                 exists;     // false = trigger not in DAO — core sends nothing
    uint16_t             triggerEid; // only valid if exists=true
    bool                 enabled;    // only valid if exists=true
    uint16_t             cfm;        // only valid if exists=true
    IOT_RequestContext_t ctx;        // Pass through from the original event unchanged
} IOT_CoreSmartCheckSyncResponse_t;

iot_err_t IOT_CoreRespondSmartCheckSync(const IOT_CoreSmartCheckSyncResponse_t *response);
```

**Usage pattern:** Pass `ctx` through from the incoming event unchanged. Set `result = IOT_OK`
on DAO success or the appropriate error code on failure.

```c
// Example: SMART BIND_TRIGGER completed
IOT_CoreSmartCmdResponse_t resp = {
    .cmdType = IOT_SMART_CMD_BIND_TRIGGER,
    .cfm     = bindData->cfm,
    .result  = IOT_OK,
    .ctx     = bindData->ctx,  // pass through unchanged
};
IOT_CoreRespondSmartCmd(&resp);
```
