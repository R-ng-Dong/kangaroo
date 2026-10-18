# IoTCoreEmbedded API Reference

**Updated**: 2026-07-14

This document covers the public iotcore headers:
- `IOT_Core.h` — Lifecycle, provisioning, boot utilities, change-WiFi BLE
- `IOT_CoreHubRole.h` — Hub-role capability startup (mesh / Zigbee)
- `IOT_CoreEventHandler.h` — Event types, data structures, callback registration
- `IOT_CoreReporter.h` — Sending state reports and responses to cloud

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

**Returns:** `IOT_OK` on success

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

Lets a provisioned device re-advertise over BLE so a paired app can update its
WiFi credentials (via `SETTING_WIFI_SCAN` / `SETTING_WIFI_SSID_PWD`) without a
factory reset.

```c
// Start "change WiFi" BLE advertisement (provisioned device only).
// Advertises the WILEDIRECT PROV UUID (00007269) with the device's own
// obfuscated deviceId in manufacturer data.
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
    uint32_t bleFallbackDelayMs;  // ms to wait after WiFi drop before BLE. Default: 30000
} IOT_CoreChangeWifiConfig_t;

// Optional. Must be called BEFORE IOT_CoreInit(). Defaults apply if not called.
iot_err_t IOT_CoreConfigureChangeWifi(const IOT_CoreChangeWifiConfig_t *config);
```

`IOT_CoreConfigureChangeWifi()` returns `IOT_ERR_INVALID_ARG` if `config` is NULL
and `IOT_ERR_INVALID_STATE` if called after `IOT_CoreInit()`. When
`bleFallbackEnabled` is true, the core automatically starts change-WiFi BLE
`bleFallbackDelayMs` milliseconds after WiFi drops and stops it on reconnect.

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

`IOT_CoreInit()` calls `IOT_CoreStartProvision()` automatically when unprovisioned.
Call it again manually only to re-enter provisioning after the device is already provisioned.

`IOT_CoreSetTimeZone()` stores a POSIX TZ string such as `"ICT-7"` in core config and applies it to the platform timer layer.
`IOT_CoreGetTimeZone()` reads the persisted value into a caller-provided buffer. UTC timestamp APIs remain UTC.

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
iot_err_t IOT_CoreConnectStoredWifi(void);
```

`IOT_CoreIsCloudConnected()` answers whether the cloud channel is *genuinely
usable right now* — the MQTT session is up **and** every standard topic
subscription has been confirmed. That is a stricter bar than "the TCP session
opened", and the distinction matters: anything published before this point (a
status NOTIFY, say) has no subscriber yet and is **silently lost**. It is
non-blocking and just reads a flag the MQTT callbacks maintain, so it is cheap to
poll from a boot-time wait loop.

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

### OTA

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
believing BLE is up. Suspend the change-WiFi BLE fallback **first**: otherwise a
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

**Call order** (in `IOT_SDKInit()` or equivalent):

```c
IOT_CoreInit(modelId);
IOT_CoreSetHubRole(IOT_HUB_ROLE_MESH, true);   /* if your SDK runs a mesh hub  */
IOT_CoreHubZigbeeStart();  // returns IOT_ERR_NOT_SUPPORTED if no Zigbee driver
```

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
void      IOT_CoreHubZigbeeStop(void);
```

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

**Event categories:**

```c
typedef enum {
    IOT_EVENT_STATE,   // GET_STATE, SET_STATE, SENSOR_LOG
    IOT_EVENT_DEVICE,  // Provisioning, join, remove, WiFi/MQTT lifecycle, group bind, Zigbee sub-devices
    IOT_EVENT_MESH,    // Mesh: keys and topology handed to your SDK
    IOT_EVENT_SETTING, // WiFi config change
    IOT_EVENT_SMART,   // Automation: triggers, schedules, bind/unbind
    IOT_EVENT_OS,      // Reboot
} IOT_CoreEventCategory_t;
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
    }
}
```

---

### Request Context

```c
typedef enum {
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
| `IOT_SENDER_AUTOMATION` | 0x14 | Automation (smid-based) |
| `IOT_SENDER_SCHEDULE` | 0x16 | Scheduled automation |
| `IOT_SENDER_COUNTDOWN` | 0x18 | Countdown timer |
| `IOT_SENDER_GOOGLE_VOICE` | 0x20 | Google Assistant |
| `IOT_SENDER_ALEXA` | 0x21 | Amazon Alexa |
| `IOT_SENDER_OLLI` | 0x22 | Olli voice |

---

### STATE Events

```c
typedef enum {
    IOT_EVENT_TYPE_STATE_SET,        // Cloud/app SET command
    IOT_EVENT_TYPE_STATE_GET,        // Cloud/app GET state request
    IOT_EVENT_TYPE_STATE_SENSOR_LOG, // Cloud sensor log query
} IOT_EventTypeState_t;
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
        struct { uint16_t groupEid; } group;      // >= IOT_GROUP_EID_BASE (0xC000)
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

#### GET_STATE — `IOT_EventStateGetRequest_t`

```c
typedef struct {
    uint16_t eid;
    IOT_RequestContext_t ctx;
    uint8_t *unixTime; // Non-NULL if time sync was requested
} IOT_EventStateGetRequest_t;
```

SDK reads current state from DAO and calls `IOT_CoreRequestReportState()`.

#### SENSOR_LOG — `IOT_EventStateSensorLogRequest_t`

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

SDK reads log data from DAO and calls `IOT_CoreRequestSensorLogResponse()`.

---

### DEVICE Events

```c
typedef enum {
    DEV_EVENT_IDENTIFICATION,        // Provisioning started (BLE connected)
    DEV_EVENT_PROVISION_CANCELLED,   // BLE disconnected before provisioning completed
    DEV_EVENT_PROVISION_COMPLETED,   // Device successfully provisioned
    DEV_EVENT_NEW_DEVICE_JOINED,     // New sub-device sync (3 phases)
    DEV_EVENT_DEVICE_REMOVED,        // Device removed from network
    DEV_EVENT_BOOTED,                // Device booted (provisioned or not)
    DEV_EVENT_WIFI_CONNECTED,        // WiFi got IP address
    DEV_EVENT_WIFI_DISCONNECTED,     // WiFi disconnected
    DEV_EVENT_CLOUD_CONNECTED,       // MQTT broker connected
    DEV_EVENT_CLOUD_DISCONNECTED,    // MQTT broker disconnected
    DEV_EVENT_WIFI_CONNECTING,       // WiFi connecting (pre-IP)
    DEV_EVENT_CLOUD_CONNECTING,      // MQTT connecting
    DEV_EVENT_ERROR,                 // Internal error
    DEV_EVENT_DEVICE_STATUS_REQUEST, // Cloud requested device status
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

#### DEV_EVENT_NEW_DEVICE_JOINED — 3 phases

Core dispatches this event three times per device join:

```c
typedef enum {
    IOT_DEV_JOIN_PHASE_DEVICE_INFO,  // Identity + common info — save to DAO
    IOT_DEV_JOIN_PHASE_ELM_INFO,     // Element list — initialize state in DAO
    IOT_DEV_JOIN_PHASE_SYNC_COMPLETE // Sync done — persist mesh key if present
} IOT_DevJoinPhase_t;
```

- **DEVICE_INFO:** Contains `devId[12]`, `mac[6]`, `eid`, `rootEid`, `protocol`, `deviceType`, `eleNum`, etc.
- **ELM_INFO:** Contains `eid`, array of `IOT_DevJoinElmInfo_t` (elemId, elemType, attrIdList)
- **SYNC_COMPLETE:** Contains `eid`, optional `meshKey` (encrypted, NULL if no mesh)

#### DEV_EVENT_DEVICE_REMOVED

```c
typedef struct {
    uint16_t eid;
    bool isRootDevice; // true = factory reset path (SDK should also reset itself and reboot)
} IOT_CoreDeviceEventDeviceRemovedData_t;
```

#### DEV_EVENT_GROUP_BIND

```c
typedef struct {
    uint16_t eid;
    uint16_t newGroupId;
    uint16_t oldGroupId;    // 0 if first bind
    uint8_t *elemData;      // Element bindings [elemId(2B)]... NULL = all elements
    uint16_t elemDataLen;   // 0 if no element bindings
    uint8_t cmdData2;       // Protocol metadata echoed by core
} IOT_CoreDeviceEventGroupBindData_t;
```

Save the group mapping in your DAO, then call `IOT_CoreRespondDeviceCmd()` with `cmdType = IOT_DEVICE_CMD_BIND_GRP` or `IOT_DEVICE_CMD_BIND_GRP_ELMS`. Pass `cmdData2` and `ctx` through from the event unchanged.

#### DEV_EVENT_GROUP_UNBIND

```c
typedef struct {
    uint16_t eid;
    uint16_t groupId;
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

Dispatched under `IOT_EVENT_MESH` on devices whose SDK has declared the mesh hub role
with `IOT_CoreSetHubRole(IOT_HUB_ROLE_MESH, true)`.

```c
typedef enum {
    MESH_EVENT_NONE = 0,
    MESH_EVENT_NODE_JOINED, // A mesh node joined the network
    MESH_EVENT_NODE_LEFT,   // A mesh node left the network
} IOT_EventTypeMesh_t;

typedef struct {
    uint16_t nodeAddr; // Mesh unicast address of the node
} IOT_CoreMeshEventNodeData_t;
```

Access via `event->evt.mesh.data.node` (valid for JOINED / LEFT).

---

### SETTING Events

```c
// SETTING_EVENT_WIFI_CHANGED
typedef struct {
    const char *ssid; // New WiFi SSID
} IOT_CoreSettingWifiChangedData_t;
```

---

### SMART Events

```c
typedef enum {
    SMART_EVENT_REMOVE_ANNOUNCE,            // Automation deleted
    SMART_EVENT_ACTIVE_BY_USER,             // Manually triggered
    SMART_EVENT_ACTIVE_BY_SCHEDULE,         // Schedule fired
    SMART_EVENT_ACTIVE_BY_TRIGGER,          // Trigger condition met
    SMART_EVENT_TRIGGER_UPDATE_ANNOUNCE,    // Trigger definition updated
    SMART_EVENT_TRIGGER_MODE,               // Trigger mode changed
    SMART_EVENT_BIND_TRIGGER,               // Bind trigger to device
    SMART_EVENT_UNBOUND_TRIGGER,            // Unbind trigger
    SMART_EVENT_SCHEDULE_UPDATE_ANNOUNCE,   // Schedule updated
    SMART_EVENT_SCHEDULE_REMOVE_ANNOUNCE,   // Schedule deleted
    SMART_EVENT_BIND_CMD,                   // Bind command to automation
    SMART_EVENT_UNBOUND_CMD,                // Unbind command
    SMART_EVENT_CHECK_SYNC,                 // Cloud requesting sync check
    SMART_EVENT_ENABLE_DISABLE,             // Enable/disable a smart entity by SMID  ✅ ready
    SMART_EVENT_ACTIVE_BY_SYNC_ON_OFF,      // Sync-on-off broadcast (subSmartType 71)  ✅ ready
    SMART_EVENT_REPORT_COND_STATE,          // Cond-group peer status report (subSmartType 2048)  ✅ ready
    SMART_EVENT_SYNC_COND_REQUEST,          // Cond-group reboot re-sync query (subSmartType 2048)  ✅ ready
    SMART_EVENT_COND_NOT_MEMBER,            // Cond-group "not a member" reply (subSmartType 2048)  ✅ ready
} IOT_EventTypeSmart_t;
```

> Enum order above is illustrative — always use the names, not numeric values.
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

See `IOT_Core.h` (§1, *OTA*) for the pending-request API and the deferred
download pattern, and `example/esp32App_rd/main/main.c` for a worked handler.

---

### Group & EID Constants

```c
#define IOT_GROUP_EID_BASE      0xC000  // EIDs >= this are group addresses
#define IOT_GROUP_EID_SECONDARY 0xC001  // Secondary group (auto-added during mesh provisioning)
#define IOT_INVALID_U16         0xFFFF  // Invalid/unset sentinel for 16-bit IDs
```

---

## 4. IOT_CoreReporter.h

### Overview

Functions for sending messages to cloud. The SDK calls these in response to
events (or proactively from hardware/timer callbacks). Core handles all message
encoding, encryption, and MQTT delivery.

---

### GET_STATE Response — `IOT_CoreRequestReportState()`

```c
typedef struct {
    uint16_t eid;
    devStateElemsInfo_t *fullState; // Full device state — SDK retains ownership
    IOT_RequestContext_t ctx;       // Pass through from IOT_EventStateGetRequest_t.ctx
    uint8_t *unixTime;              // Echo from getData->unixTime (NULL if not requested)
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

---

### State Change Report — `IOT_CoreRequestReportStateChanged()`

Called after applying a SET command, or proactively when local hardware state changes.

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

### SENSOR_LOG Response — `IOT_CoreRequestSensorLogResponse()`

```c
typedef struct {
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    uint16_t year;
    uint16_t dayOfYear;
    uint8_t *logData;    // Raw log bytes — SDK retains ownership
    uint16_t logDataLen; // 0 if no data found (response still sent)
    IOT_RequestContext_t ctx;
} IOT_CoreSensorLogResponse_t;

iot_err_t IOT_CoreRequestSensorLogResponse(const IOT_CoreSensorLogResponse_t *response);
```

---

### Device Status Report — `IOT_CoreRequestReportDeviceStatus()`

```c
typedef struct {
    const char *firmwareVersion;  // e.g. "1.2.3"
    int64_t uptimeSeconds;
    int64_t lastActivationTime;   // Unix timestamp of last state change (0 if unknown)
    uint8_t protocolFlags[6];     // [mode, mesh, zigbee, matter, reserved, reserved]
    uint8_t ramUsagePercent;      // 0-100
    uint8_t nvsUsagePercent;      // 0-100
    uint32_t totalRamKB;
    uint32_t totalNvsKB;
} IOT_CoreDeviceStatusData_t;

iot_err_t IOT_CoreRequestReportDeviceStatus(const IOT_CoreDeviceStatusData_t *status);
```

Called on cloud GET request (`DEV_EVENT_DEVICE_STATUS_REQUEST`) and on MQTT reconnect.

---

### Smart Trigger Report — `IOT_CoreRequestSmartTriggerReport()`

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
device, it broadcasts to the location command topic and every peer bound to the
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
> (`sdk/defines/IOT_AttrDefines.h` ships the attribute ids as a reference you can copy).
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
suppression) and sends it to the location command topic; peers receive it as the
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

> **A complete implementation of all of this ships in
> `example/esp32App_rd/main/main.c`** — search for `app_cond_`. It is written to be read and
> lifted: the merge, the effective-K calculation, the report funnel, the local-change
> broadcast and the reboot re-sync, with `TODO: persist` at each point a real device needs
> storage. Start there rather than from this page.

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
> `docs/core/Attribute_Reference.md`, or start from `sdk/defines/IOT_AttrDefines.h`.

---

## Domain Types

Shared types used across events and reporter APIs (defined in `IOT_CoreEventHandler.h`):

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
    uint16_t             cfm;      // Confirmation value (0 if not applicable)
    iot_err_t            result;   // IOT_OK = success, else failure
    IOT_RequestContext_t ctx;      // Pass through from the original event unchanged
} IOT_CoreSmartCmdResponse_t;

iot_err_t IOT_CoreRespondSmartCmd(const IOT_CoreSmartCmdResponse_t *response);
```

### DEVICE Command Response

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
