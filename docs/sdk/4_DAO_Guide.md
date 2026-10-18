# DAO Implementation Guide

**SDK Writer Guide concepts:** SDK-Owned Responsibilities.

Data Access Objects (DAOs) are your SDK's persistence layer. This guide covers what to store, which interfaces to use,
how to apply caching, ownership boundaries, and the correct factory reset sequence.

---

## 1. What to Store and Where

| Data | DAO | Key pattern |
|------|-----|-------------|
| Device identity + common info | Device DAO | keyed by EID |
| Element state (attrId → value) | Device DAO | keyed by EID+elemId+attrId |
| Group membership | Group DAO | keyed by groupId |
| Smart trigger | Trigger DAO | keyed by smid |
| Smart schedule times | Schedule DAO | keyed by smid |
| Smart bind commands | Smart DAO | keyed by smid+eid |
| IR remote data | IR DAO | keyed by device EID |
| Sensor logs | SensorLog DAO | keyed by EID+date |

---

## 2. The Storage Interface (`IOT_DaoStorage.h`)

All DAO persistence goes through the abstract `IOT_DaoStorage` interface. Never call NVS (or any platform storage API) directly — using the abstract interface keeps your DAO portable and ensures writes are serialized through the platform's internal queue.

### Core function signatures

```c
// Read a value from storage (BLOCKING — call from tasks only, not callbacks)
iot_err_t IOT_DaoStorageGet(const char *ns, const char *key, IOT_DaoType type,
                             void **outData, uint16_t *outLen);

// Write a value to storage (BLOCKING)
iot_err_t IOT_DaoStorageSet(const char *ns, const char *key, IOT_DaoType type,
                             void *data, uint16_t len);

// Write a value asynchronously (NON-BLOCKING — safe from callbacks)
iot_err_t IOT_DaoStorageSetAsync(const char *ns, const char *key, IOT_DaoType type,
                                  void *data, uint16_t len);

// Remove a single key (BLOCKING)
iot_err_t IOT_DaoStorageRemoveByKey(const char *ns, const char *key);

// Remove a single key asynchronously (NON-BLOCKING)
iot_err_t IOT_DaoStorageRemoveByKeyAsync(const char *ns, const char *key);

// Check if a key exists (BLOCKING)
iot_err_t IOT_DaoStorageCheckExists(const char *ns, const char *key, bool *exists);

// Remove an entire namespace (BLOCKING)
iot_err_t IOT_DaoStorageRemoveNamespace(const char *ns);

// Get all keys in a namespace (BLOCKING)
iot_err_t IOT_DaoStorageGetKeyList(const char *ns, char ***outKeyList, uint16_t *outKeyCount);

// Get all key-value pairs in a namespace (BLOCKING)
iot_err_t IOT_DaoStorageGetNamespaceData(const char *ns, char ***outKeyList, uint16_t *outKeyCount,
                                          void ***outDataList, uint16_t **outDataSizes);
```

### NVS namespace limit

ESP32 NVS namespaces are limited to **15 characters**. Plan your namespace names upfront — exceeding 15 characters causes silent truncation or storage errors. Adopt a consistent prefix convention per DAO domain:

| Domain | Suggested namespace |
|--------|-------------------|
| Device identity | `"mydev"` |
| Element state | `"mydev_st"` |
| Group | `"mygrp"` |
| Smart trigger | `"smtrig"` |
| Smart schedule | `"smsched"` |
| Smart bind cmds | `"sm_%04x"` (per smid) |
| IR remote | `"ir"` |
| Sensor log | `"slog"` |

### Key naming examples

```c
char key[24];

// Device identity record, keyed by EID
snprintf(key, sizeof(key), "dev:%04x", eid);          // e.g. "dev:0001"
IOT_DaoStorageSet("mydev", key, IOT_DAO_TYPE_BLOB, &info, sizeof(info));

// Element state, keyed by EID + elemId + attrId
snprintf(key, sizeof(key), "%04x:%04x:%04x", eid, elemId, attrId);
IOT_DaoStorageSet("mydev_st", key, IOT_DAO_TYPE_BLOB, &val, valLen);
```

Keys within a namespace also have an NVS limit of 15 characters. If your formatted key could exceed that, use a hash or shorter encoding.

---

## 3. Group DAO Layout

Your SDK should store group membership in a way that can return all members for one group and the optional element
bindings for each member. One compact storage layout is one namespace and one key per group:

| Item | Value |
|---|---|
| Namespace | `"group-v1"` |
| Key | 4-character lowercase hex group ID, for example `"c000"` |
| Value type | Blob |
| Blob layout | Repeated `[eid(2B)][elemDataLen(2B)][elemData]` entries |

Each entry represents one member device:

```text
[eid high][eid low][elemDataLen high][elemDataLen low][elemData bytes...]
```

`elemData` is optional. When present, it is an array of 2-byte element IDs:

```text
[elemId high][elemId low][elemId high][elemId low]...
```

Use this layout, or an equivalent parsed model, to support the group-control behavior described in
`1_Device_Control.md`:

- `elemDataLen == 0`: group control applies to all elements registered for that EID in the device DAO.
- `elemDataLen > 0`: group control applies only to the listed element IDs.
- Removing a device should remove that EID from every group DAO entry.
- Binding a custom group should clear stale custom groups for that EID when the core event does not provide an
  `oldGroupId`.

---

## 4. Cache Layer (`IOT_Cached.h`)

The cache layer sits in front of NVS storage and holds recently-accessed data in heap memory. Use it for **hot data** — values read on every state GET request or evaluated on every smart trigger tick. Without caching, each attribute read translates to a blocking NVS read, which adds measurable latency under load.

**Use the cache for:**
- Device element state attributes (read frequently, updated on SET commands)
- Device identity fields accessed on every message dispatch

**Do NOT cache:**
- Large blobs: IR raw data, sensor log entries — these are read infrequently and can easily exhaust heap on embedded targets.

### Write policy

The cache supports `IOT_CACHE_WRITE_THROUGH` (write to NVS immediately on `IOT_CacheSet`) and `IOT_CACHE_WRITE_BACK` (write to NVS only on flush or eviction). For device state, `WRITE_THROUGH` is the safer default — it avoids data loss if the device reboots unexpectedly between flushes.

### Registering a namespace

Register your cache namespace inside your DAO init function, after `IOT_InitPlatform()` has run:

```c
iot_err_t MyDeviceDao_Init(void)
{
    IOT_CacheConfig_t cfg = {
        .namespace       = "mydev_st",
        .maxEntries      = 32,
        .flushIntervalMs = 0,                    // manual flush only
        .policy          = IOT_CACHE_WRITE_THROUGH,
    };
    return IOT_CacheRegisterNamespace(&cfg);
}
```

### Read/write pattern

```c
// Write: update cache (and NVS, with WRITE_THROUGH policy)
iot_err_t MyDeviceDao_SetElemState(uint16_t eid, uint16_t elemId,
                                    uint16_t attrId, void *value, uint16_t len)
{
    char key[16];
    snprintf(key, sizeof(key), "%04x:%04x:%04x", eid, elemId, attrId);
    return IOT_CacheSet("mydev_st", key, IOT_DAO_TYPE_BLOB, value, len);
}

// Read: cache returns hit immediately; on miss, falls back to NVS automatically
iot_err_t MyDeviceDao_GetElemState(uint16_t eid, uint16_t elemId,
                                    uint16_t attrId, void **outValue, uint16_t *outLen)
{
    char key[16];
    snprintf(key, sizeof(key), "%04x:%04x:%04x", eid, elemId, attrId);
    return IOT_CacheGet("mydev_st", key, IOT_DAO_TYPE_BLOB, outValue, outLen);
}
```

`IOT_CacheGet` checks the in-memory cache first and falls back to NVS on a miss, so the caller always gets the correct value regardless of cache state. On a miss the value is loaded into cache for subsequent reads.

---

## 4. DAO Ownership Rules

**Config DAO** (provision state, WiFi credentials, MQTT broker, mesh keys) is owned by iotcore. It is internal — never access it directly or attempt to read its NVS namespace. iotcore exposes `IOT_CoreIsProvisioned()`, `IOT_CoreGetRootEid()`, and `IOT_CoreFactoryReset()` as the only config access points available to your SDK.

**Device, Group, Smart, IR, and SensorLog DAOs** are SDK-owned. You implement and own these. The reference implementations shipped in `sdk/` are fully working starting points — read them, understand the key layout, then modify them to fit your storage design. iotcore never calls into these DAOs directly; it dispatches events to your SDK handlers, which in turn call your DAOs.

**`IOT_DaoInit()`** is platform-only. It initializes NVS flash and the in-memory cache system. Your SDK must call its own DAO init functions (for example `IOT_DaoSmartTriggerInit()`, `IOT_DaoSmartScheduleInit()`, and any device/group DAO inits you implement) separately, inside your SDK init function, after `IOT_InitPlatform()` has returned successfully.

**`IOT_DaoFactoryResetAll()`** is not called by iotcore. Your SDK is responsible for implementing the complete factory reset sequence — collecting smid lists, wiping all SDK-owned namespaces, and then calling `IOT_CoreFactoryReset()` last. The order matters; see Section 5.

---

## 5. Factory Reset Sequence

The factory reset sequence must follow a specific order because the smart bind command cleanup requires the smid list from the trigger DAO. If you wipe the trigger DAO first without capturing its smid list, you lose the information needed to locate and delete the per-smid bind command namespaces, leaving orphaned data in NVS.

The correct sequence:

```c
// Step 1: Collect smid list and wipe all trigger data
uint16_t *smids = NULL;
uint16_t smidCount = 0;
IOT_DaoSmartTriggerFactoryReset(&smids, &smidCount);

// Step 2: Wipe bind command namespaces for each smid
if (smids != NULL) {
    IOT_DaoSmartFactoryReset(smids, smidCount);
    SAFE_FREE(smids); // Use SAFE_FREE from IOT_Memory.h (iotcommon)
}

// Step 3: Wipe schedule data
IOT_DaoSmartScheduleFactoryReset();

// Step 4: Wipe your device and group DAOs
your_device_dao_factory_reset();
your_group_dao_factory_reset();

// Step 5: Wipe core config.
// Dispatches DEV_EVENT_DEVICE_REMOVED (isRoot=true) to your SDK event handler.
// Your handler is responsible for restarting the chip (e.g. esp_restart()).
IOT_CoreFactoryReset();
```

> **Important:** Steps 1–4 must complete before calling `IOT_CoreFactoryReset()`. After `IOT_CoreFactoryReset()` fires `DEV_EVENT_DEVICE_REMOVED` and your handler restarts the chip, any unfinished DAO cleanup will be left in an inconsistent state — the device will boot without provision data but with stale SDK DAO entries.
