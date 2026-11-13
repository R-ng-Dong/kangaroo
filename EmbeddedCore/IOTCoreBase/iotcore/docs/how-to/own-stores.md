# Keep data of your own: storage, cache, memory

**Path A:** the Device, Group, IR, Smart and Record stores are `sdkcontract`'s, and every write to
them is the contract's. Read the Device and IR stores through `sdkcontract/include/dao/`
([sdkcontract's read-state how-to](../../../sdkcontract/docs/how-to/read-state.md)); keep only data of
your own (a calibration, a product setting) in a store of your own, as below.

**Path B:** you keep every store yourself — devices, groups, automations, history, IR — on the same
interfaces. Core's own configuration (provisioning, WiFi, cloud credentials) is core's: you never
read its storage, and you reach what you need through `IOT_Core.h` (`IOT_CoreIsProvisioned()`,
`IOT_CoreGetRootEid()`, `IOT_CoreGetRootDeviceId()`, `IOT_CoreFactoryReset()`).

## What a path-B SDK stores

| Data | Keyed by |
|---|---|
| Device identity and common info | EID |
| Element state (attribute values) | EID, element, attribute |
| Group membership | group id ([hub-and-sub-devices.md](hub-and-sub-devices.md#groups)) |
| Automation commands, triggers, schedules | SMID ([smart-engine.md](smart-engine.md)) |
| History | series and block ([Record rulebook](../../../sdkcontract/docs/record.md)) |
| IR keysets | the appliance's EID |

## The storage interface (`dao/IOT_DaoStorage.h`)

All persistence goes through the platform's key-value interface, never the chip's storage API
directly: it keeps your stores portable and serialises writes through the platform's queue.

| Call | Blocks |
|---|---|
| `IOT_DaoStorageGet(ns, key, type, &data, &len)` — the caller frees `data` | Yes |
| `IOT_DaoStorageSet(ns, key, type, data, len)` | Yes |
| `IOT_DaoStorageSetBatch(ns, entries, count)` — many keys, one commit, all or nothing | Yes |
| `IOT_DaoStorageSetAsync(...)`, `IOT_DaoStorageRemoveByKeyAsync(ns, key)` | No: queued |
| `IOT_DaoStorageRemoveByKey(ns, key)`, `IOT_DaoStorageCheckExists(ns, key, &exists)` | Yes |
| `IOT_DaoStorageRemoveNamespace(ns)`, `IOT_DaoStorageGetKeyList(...)`, `IOT_DaoStorageGetNamespaceData(...)` | Yes |

On ESP32 a namespace name and a key are each at most **15 characters**; plan names up front and
encode keys compactly (`"%04x:%04x"` rather than a sentence). `IOT_DaoInit()` is the platform's and
runs inside `IOT_InitPlatform()`; bring your own stores up after it, before `IOT_CoreInit()`.

```c
char key[16];
snprintf(key, sizeof(key), "%04x:%04x:%04x", eid, elemId, attrId);
IOT_DaoStorageSet("mydev_st", key, IOT_DAO_BLOB, value, len);
```

## The cache (`dao/IOT_Cached.h`)

Evaluate the cache before reaching for raw storage. It keeps recently used values in RAM in front of
storage, for hot data read on every query or every trigger evaluation (element state, identity
fields). Do not cache large, rarely read blobs (IR codes, history).

```c
const IOT_CacheConfig_t cfg = {
    .nsName = "mydev_st",
    .maxEntries = 32,
    .flushIntervalMs = 0,                 // manual flush only
    .policy = IOT_CACHE_WRITE_THROUGH,    // or IOT_CACHE_WRITE_BACK: written on flush or eviction
};
IOT_CacheRegisterNamespace(&cfg);         // after IOT_InitPlatform()

IOT_CacheSet("mydev_st", key, IOT_DAO_BLOB, value, len);
IOT_CacheGet("mydev_st", key, IOT_DAO_BLOB, &out, &outLen);   // a miss falls back to storage
```

`IOT_CACHE_WRITE_THROUGH` is the safer default for state: nothing is lost when the device restarts
between flushes. With `IOT_CACHE_WRITE_BACK`, call `IOT_CacheFlush()` before a restart of your own.

## Task context

Every store call except the `Free*` functions and the `*Async` writes blocks on storage or on the
cache, so none of them may run in a timer callback; all of them are fine from your core event
callback or your own tasks. A timer that needs a store hands the work to core's task with
`IOT_CoreDeferWork()`.

## Memory

Use `iotcommon` rather than the C library directly: `Mem_SafeMalloc(size, TAG, "what")` and
`SAFE_FREE(ptr)` (frees and sets the pointer to NULL), through `IOT_Common.h`. Free every output
structure a store call allocates, and release on every path with one cleanup label:

```c
iot_err_t handle_event(void)
{
    iot_err_t err = IOT_OK;
    devStateElemsInfo_t fullState = {0};
    elemStateAttr_t *attrs = NULL;

    err = my_device_get_full_state(eid, &fullState);
    if (err != IOT_OK)
        goto cleanup;
    /* ... */

cleanup:
    SAFE_FREE(attrs);
    my_device_free_full_state(&fullState);
    return err;
}
```

On path A the Device store's outputs have their own free functions
(`IOT_DaoDeviceFreeElemsInfo()`, `IOT_DaoDeviceFreeIdentityInfo()`); see
[sdkcontract's reference](../../../sdkcontract/docs/reference.md).
