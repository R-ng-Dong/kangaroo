# Smart Automation

**SDK Writer Guide concepts:** Core Event Contract, SDK-Owned Responsibilities.

Smart automation lets the cloud (and your SDK) coordinate conditional control across devices. A **smid** (Smart ID,
`uint16_t`) is the primary key that ties together a trigger or schedule, a set of bind commands, and an optional time
window. This document covers the data model, every smart event your SDK must handle, how to execute bind commands, local
trigger evaluation, and schedule ticking.

---

## 1. Data Model

```
smid (uint16_t)
│
├── Trigger (IOT_SmartTrigger_t)           [IOT_DaoSmartTriggerMgmt.h]
│   keyed by smid
│   Watches: eid + elmId + attrId for a condition match in a time window
│   Fields: condition, attrId/attrSize/attrValue, secondaryAttrValue (COND_BETWEEN),
│            timCfgType/timCfgValue, timeStart/timeStop, weekDay, enabled, ...
│
├── Schedule times (uint16_t[])            [IOT_DaoSmartScheduleMgmt.h]
│   keyed by smid
│   Array of minute-of-week UTC0 values
│   Formula: day * 1440 + hour * 60 + minute   (range 0–10079)
│
└── Bind commands (per target EID)         [IOT_DaoSmartMgmt.h]
    keyed by smid + eid
    One entry per target device EID.
    Each entry: serialized list of [attrId][attrSize][attrValue][reverse][delay][elmId] commands
```

**Key rules:**

- A smid has **either** a trigger **or** schedule times — not both. The cloud never sends both for the same smid.
- A smid has **N bind command entries**, one per target device EID. A smid that targets 3 devices has 3 entries.
- On `SMART_EVENT_REMOVE_ANNOUNCE` you must clean up **all three** namespaces for the smid: trigger DAO, schedule DAO, and all bind command entries.

---

## 2. Event Reference

### Broadcast Events

Broadcast events are sent to all devices in the location. If the smid is not in your DAO, silently return `IOT_OK` — it belongs to another device.

| Event | When it fires | SDK action |
|-------|--------------|------------|
| `SMART_EVENT_ACTIVE_BY_USER` | User manually triggered automation from the app | Execute bind commands for smid (see Section 3) |
| `SMART_EVENT_ACTIVE_BY_TRIGGER` | Cloud echo of a trigger fire (another device triggered this) | Execute bind commands — skip if already executed locally for this smid (dedup, see Section 5) |
| `SMART_EVENT_ACTIVE_BY_SCHEDULE` | A schedule minute-of-week matched current UTC time | Execute bind commands for smid |
| `SMART_EVENT_REMOVE_ANNOUNCE` | Automation deleted by cloud | Remove trigger + schedule times + all bind cmds for smid |
| `SMART_EVENT_SCHEDULE_REMOVE_ANNOUNCE` | Schedule removed for this smid | Remove schedule times for smid only (keep trigger and bind cmds) |
| `SMART_EVENT_TRIGGER_UPDATE_ANNOUNCE` | Trigger conditions changed (e.g. new time window) | Update the EID, elemId, and triggerKey fields in the trigger DAO for this smid (partial update — condition and timing are not included in this announce) |
| `SMART_EVENT_SCHEDULE_UPDATE_ANNOUNCE` | Schedule times updated by cloud | Replace schedule times in DAO from `scheduleTimes` array in event data |

Data structs for broadcast events:

```c
// ACTIVE_BY_USER
IOT_CoreSmartEventActiveByUserData_t   .data.activeByUser    // .smid
// ACTIVE_BY_TRIGGER
IOT_CoreSmartEventActiveByTriggerData_t .data.activeByTrigger // .smid, .attrValue, .triggerKey, .timeConfig
// ACTIVE_BY_SCHEDULE
IOT_CoreSmartEventActiveByScheduleData_t .data.activeBySchedule // .smid, .minuteOfWeekScheduleUTC0
// REMOVE_ANNOUNCE / SCHEDULE_REMOVE_ANNOUNCE
IOT_CoreSmartEventRemoveAnnounceData_t        .data.removeAnnounce        // .smid
IOT_CoreSmartEventScheduleRemoveAnnounceData_t .data.scheduleRemoveAnnounce // .smid
// SCHEDULE_UPDATE_ANNOUNCE
IOT_CoreSmartEventScheduleUpdateAnnounceData_t .data.scheduleUpdateAnnounce
    // .smid, .scheduleTimes (uint16_t[]), .scheduleTimeCount
// TRIGGER_UPDATE_ANNOUNCE
IOT_CoreSmartEventTriggerUpdateAnnounceData_t .data.triggerUpdateAnnounce
    // .eid, .rootEid, .elemId, .elemType, .triggerKey
```

### DAO-Completed Events

After your DAO work finishes, call `IOT_CoreRespondSmartCmd()` with an `IOT_CoreSmartCmdResponse_t`:

```c
typedef struct {
    IOT_SmartCmdType_t   cmdType;  // which command this is for
    uint16_t             cfm;      // confirmation value (from bindTriggerElmInfo.cfm, or 0 if N/A)
    iot_err_t            result;   // IOT_OK = success, else failure
    IOT_RequestContext_t ctx;      // pass through unchanged from event
} IOT_CoreSmartCmdResponse_t;

iot_err_t IOT_CoreRespondSmartCmd(const IOT_CoreSmartCmdResponse_t *response);
```

| Event | Key data fields | SDK action |
|-------|----------------|------------|
| `SMART_EVENT_BIND_TRIGGER` | `smid`, `eid`, `elmId`, `condition`, `attrId`/`attrSize`/`attrValue` (primary), `secondaryAttrValue` (COND_BETWEEN upper bound), `timCfgType`/`timCfgValue`, `timeStart`/`timeStop`, `weekDay`, `triggerKey.*`, `isDeviceUtc`, `timeZoneFraction`, `cfm` (from `bindTriggerElmInfo`) | Build `IOT_SmartTrigger_t` from event fields, call `IOT_DaoSmartTriggerSet()`. Respond with `cmdType=IOT_SMART_CMD_BIND_TRIGGER`, `cfm=bindTriggerElmInfo.cfm`. |
| `SMART_EVENT_UNBOUND_TRIGGER` | `smid`, `eid` | Call `IOT_DaoSmartTriggerRemove(smid)`. Respond with `cmdType=IOT_SMART_CMD_UNBOUND_TRIGGER`, `cfm=0`. |
| `SMART_EVENT_BIND_CMD` | `smid`, `eid`, `rootEid`, `prtc`, `filter`, `cmdSize`, `attrValueList` (array of `IOT_CoreSmartBlockAttrValueReverseDelayElm_t`) | Serialize `attrValueList` into a blob; call `IOT_DaoSmartSetBindCmd()`. Respond with `cmdType=IOT_SMART_CMD_BIND_CMD`, `cfm=0`. |
| `SMART_EVENT_UNBOUND_CMD` | `smid`, `eid` | Call `IOT_DaoSmartRemoveBindCmd(smid, eid)`. Respond with `cmdType=IOT_SMART_CMD_UNBOUND_CMD`, `cfm=0`. |
| `SMART_EVENT_TRIGGER_MODE` | `type` (`IOT_CoreSmartTriggerModeType_t`), `mode`, `smid`/`eid` (varies by type) | Update enabled/disabled state via `IOT_DaoSmartTriggerSetEnabled()` or `IOT_DaoSmartTriggerSetEnabledByEid()` or `IOT_DaoSmartScheduleSetEnabled()` per type. Respond with `cmdType=IOT_SMART_CMD_TRIGGER_MODE`, `cfm=0`. |

#### BIND_TRIGGER field mapping

```c
// Event source struct: IOT_CoreSmartEventBindTriggerData_t
IOT_SmartTrigger_t trigger = {0};
trigger.smid             = event.smid;  // from bindTriggerElmInfo.smid
trigger.eid              = event.bindTriggerElmInfo.eid;
trigger.type             = event.bindTriggerElmInfo.type;
trigger.smartType        = event.bindTriggerElmInfo.smartType;
trigger.subSmartType     = event.bindTriggerElmInfo.subSmartType;
trigger.elmIdInfo        = event.bindTriggerElmInfo.elmId;
trigger.conditionInfo    = event.bindTriggerElmInfo.condition;
trigger.cfm              = event.bindTriggerElmInfo.cfm;
trigger.elmId            = event.elmId;       // from ext elm info
trigger.condition        = event.condition;   // from ext elm info
trigger.attrId           = event.primaryAttrValue.attrId;
trigger.attrSize         = event.primaryAttrValue.attrSize;
// copy up to 8 bytes of primaryAttrValue.attrValue into trigger.attrValue
trigger.secondaryAttrSize  = event.secondaryAttrValue.attrSize;
// copy up to 8 bytes of secondaryAttrValue.attrValue into trigger.secondaryAttrValue
trigger.timCfgType       = event.timeConfigMix.timCfgType;  // or timeConfig — check which block is populated
trigger.timCfgValue      = event.timeConfigMix.timCfgValue;
trigger.timeStart        = event.timeStart;
trigger.timeStop         = event.timeStop;
trigger.weekDay          = event.weekDay;
trigger.triggerKeyOwnerEid      = event.bindTriggerKey.ownerEid;
trigger.triggerKeyOwnerEidCount = event.bindTriggerKey.ownerEidCount;
trigger.triggerKeyExtEid        = event.bindTriggerKey.extEid;
trigger.triggerKeyExtEidCount   = event.bindTriggerKey.extEidCount;
trigger.isDeviceUtc      = event.isDeviceUtc ? 1 : 0;
// timeZoneQuarterHours is only meaningful when isDeviceUtc == false
// (device uses a fixed timezone offset instead of UTC).
if (!event.isDeviceUtc) {
    trigger.timeZoneQuarterHours = (int16_t)(event.timeZoneFraction * 4.0f);
} else {
    trigger.timeZoneQuarterHours = 0;
}
trigger.enabled          = 1;
trigger.minuteDisable    = 0;
IOT_DaoSmartTriggerSet(trigger.smid, &trigger);
```

#### BIND_CMD serialization

The `attrValueList` is an array of `cmdSize` entries. Serialize each entry into a blob before calling `IOT_DaoSmartSetBindCmd()`:

```
Blob layout:
  [cmdCount (uint16_t)]
  for each command:
    [attrId    (uint16_t)]
    [attrSize  (uint16_t)]
    [attrValue (attrSize bytes)]
    [reverse   (uint16_t)]
    [delay     (uint16_t)]
    [elmId     (uint16_t)]
```

#### TRIGGER_MODE type dispatch

```c
switch (event.type) {
    case IOT_CORE_SMART_TRIGGER_MODE_SET_TRIGGER:
        // event.data.setTrigger.smid + event.mode + event.data.setTrigger.minuteDisable
        IOT_DaoSmartTriggerSetEnabled(smid, mode != 0, minuteDisable);
        break;
    case IOT_CORE_SMART_TRIGGER_MODE_SCHEDULE:
        // event.data.schedule.smid + event.mode
        IOT_DaoSmartScheduleSetEnabled(smid, mode != 0);
        break;
    case IOT_CORE_SMART_TRIGGER_MODE_ENABLE_DISABLE_AUTOMATION:
        // event.data.enableDisableAutomation.eid + event.mode
        IOT_DaoSmartTriggerSetEnabledByEid(eid, mode != 0);
        break;
    case IOT_CORE_SMART_TRIGGER_MODE_ENABLE_DISABLE_ALL_AUTOMATION:
        // event.mode: disable all triggers on this device (0=disable, 1=enable)
        // Iterate all triggers and enable/disable
        break;
}
```

### Query Events

| Event | SDK action |
|-------|------------|
| `SMART_EVENT_CHECK_SYNC` | Look up `smid` in trigger DAO via `IOT_DaoSmartTriggerGet()`. Call `IOT_CoreRespondSmartCheckSync()` with `exists=true/false`, `triggerEid`, `enabled`, and `cfm` from stored trigger. |

```c
typedef struct {
    uint16_t             smid;
    bool                 exists;      // false = not stored locally
    uint16_t             triggerEid;  // valid only if exists=true
    bool                 enabled;     // valid only if exists=true
    uint16_t             cfm;         // valid only if exists=true
    IOT_RequestContext_t ctx;
} IOT_CoreSmartCheckSyncResponse_t;

iot_err_t IOT_CoreRespondSmartCheckSync(const IOT_CoreSmartCheckSyncResponse_t *response);
```

If `exists=false`, core sends nothing to cloud (triggers a re-bind from cloud, which is correct).

---

## 3. Executing Bind Commands

When any `ACTIVE_BY_*` event fires — or when a local trigger match occurs (Section 4) — execute the automation:

**Step 1 — Load bind commands:**

```c
char   **keyList   = NULL;
void   **dataList  = NULL;
uint16_t *dataSizes = NULL;
uint16_t keyCount, rootEid, prtc;

iot_err_t err = IOT_DaoSmartGetBindCmds(smid, &keyList, &keyCount,
                                         &dataList, &dataSizes, &rootEid, &prtc);
if (err != IOT_OK) {
    // smid not in our DAO — cross-device automation, normal. Return IOT_OK silently.
    return IOT_OK;
}
```

**Step 2 — Parse each blob and dispatch commands:**

For each key (`keyList[i]` encodes the target EID, `dataList[i]` is the serialized blob):

```
Blob offset 0:  cmdCount (uint16_t)
For each command starting at offset 2:
  attrId    (uint16_t)
  attrSize  (uint16_t)
  attrValue (attrSize bytes)
  reverse   (uint16_t)
  delay     (uint16_t)
  elmId     (uint16_t)
```

- **`reverse == 1`** → invert the `attrValue` (toggle the attribute — e.g. flip 0 to 1).
- **`delay > 0`** → schedule command execution via a timer, do **NOT** block the current task.
- **`attrId == 311 or 312`** (sync attrs) → replace the stored value with the trigger source's current attribute value (passed in from the trigger that fired — see `activeByTrigger.attrValue`).

**Step 3 — Dispatch each command:**

Take the same code path as a normal SET: update device DAO state → actuate hardware / dispatch protocol → build and send a state change report via `IOT_CoreRequestReportStateChanged()`.

Set `senderFrom = IOT_SENDER_AUTOMATION` (0x14) in the context when dispatching automation commands so trigger evaluation in Step 4 can detect and skip re-evaluation (loop prevention).

**Step 4 — Notify cloud (trigger-sourced only):**

If the execution was triggered locally (Section 4 match), send a cloud notification so other devices in the location can execute their bind commands for the same smid:

```c
IOT_CoreSmartTriggerReport_t report = {
    .smid      = smid,
    .attrId    = trigger.attrId,
    .attrSize  = trigger.attrSize,
    .attrValue = trigger.attrValue,
    // pack triggerKey and timeCfg from stored trigger
};
IOT_CoreRequestSmartTriggerReport(&report);
```

**Step 5 — Free:**

```c
for (uint16_t i = 0; i < keyCount; i++) { free(keyList[i]); free(dataList[i]); }
free(keyList); free(dataList); free(dataSizes);
```

---

## 4. Trigger Evaluation

After every successful SET command, evaluate whether any stored triggers should fire. Call the deferred version (scheduled on a timer thread with ~10 ms delay) — **never** call it synchronously from the MQTT/BLE receive task, as this blocks the receive queue.

**Evaluation steps:**

1. Look up all smids monitoring the changed EID: `IOT_DaoSmartTriggerGetByEid(eid, &smids, &count)`.
2. For each smid: load trigger via `IOT_DaoSmartTriggerGet()`. Check:
   - `trigger.enabled == 1` — skip if disabled.
   - `trigger.attrId` matches the attribute that was just set.
   - `trigger.elmId` matches the element ID that changed (or `elmId == 0` for any element).
   - Condition met (see below).
   - Time window valid: if `timeStart != 0 || timeStop != 0`, check current UTC time is within `[timeStart, timeStop]` and that `weekDay` bitmask includes today (`bit0=Sun..bit6=Sat`, `0x00 = all days`).

3. **Condition types:**

   > **Note:** These condition values are protocol-level constants not defined in any shipped header. Define them in your SDK before using them in trigger evaluation:
   > ```c
   > // Condition type values — not defined in shipped headers.
   > // Define these in your SDK header before using them in trigger evaluation:
   > #define SMART_COND_ANY     1  // Fires on any value change
   > #define SMART_COND_EQUAL   2  // Fires when newValue == trigger.attrValue
   > #define SMART_COND_EXIST   3  // Fires when value is non-zero
   > #define SMART_COND_BETWEEN 4  // Fires when trigger.attrValue <= newValue <= trigger.secondaryAttrValue
   > ```

   | Value | Name | Fires when |
   |-------|------|-----------|
   | 1 | `SMART_COND_ANY` | Any value change (always fires) |
   | 2 | `SMART_COND_EQUAL` | `newValue == attrValue` |
   | 3 | `SMART_COND_EXIST` | `newValue != 0` |
   | 4 | `SMART_COND_BETWEEN` | `attrValue ≤ newValue ≤ secondaryAttrValue` |

4. If condition matched, check `trigger.smartType`:
   - `SMART_TYPE_NOTIFICATION` — send a notification report to cloud only (no bind command execution).
   - All other types — execute bind commands locally (Section 3) **and** notify cloud via `IOT_CoreRequestSmartTriggerReport()`.

---

## 5. Loop Prevention

**Sender-based filter:** Before evaluating triggers after a SET, check `ctx.senderFrom`. Skip trigger evaluation entirely if sender is:

| Constant | Value | Meaning |
|----------|-------|---------|
| `IOT_SENDER_AUTOMATION` | 0x14 | Automation (plain) |
| `IOT_SENDER_AUTOMATION_UUID_V4` | 0x15 | Automation (UUID v4 tagged) |
| `IOT_SENDER_SCHEDULE` | 0x16 | Schedule (plain) |
| `IOT_SENDER_SCHEDULE_UUID_V4` | 0x17 | Schedule (UUID v4 tagged) |
| `IOT_SENDER_COUNTDOWN` | 0x18 | Countdown timer |

**Cloud echo dedup:** For `SMART_EVENT_ACTIVE_BY_TRIGGER`, the cloud broadcasts a trigger activation back to all devices in the location. If your device already executed the automation locally (in Section 4), skip local execution to avoid running commands twice. Track this with a small recently-executed table keyed by smid (a small ring buffer of `{smid, timestamp}` is sufficient — clear entries older than a few seconds).

---

## 6. Schedule Tick

Start a 60-second periodic timer during your SDK init. On each tick:

```c
uint16_t  *smids      = NULL;
uint16_t   smidCount  = 0;
uint16_t **timeLists  = NULL;
uint16_t  *timeCounts = NULL;

IOT_DaoSmartScheduleGetAll(&smids, &smidCount, &timeLists, &timeCounts);

uint16_t nowMinuteOfWeek = getCurrentUtcMinuteOfWeek();
// day * 1440 + hour * 60 + minute, where day=0(Sun)..6(Sat)

for (uint16_t i = 0; i < smidCount; i++) {
    if (!IOT_DaoSmartScheduleIsEnabled(smids[i])) continue;
    for (uint16_t j = 0; j < timeCounts[i]; j++) {
        if (timeLists[i][j] == nowMinuteOfWeek) {
            executeBindCommands(smids[i]);  // Section 3
        }
    }
}
// free all allocated memory
```

Compare with a one-minute tolerance if your tick interval is not perfectly aligned.

---

## 7. Failure Handling

| Scenario | Action |
|----------|--------|
| `BIND_TRIGGER` DAO write fails | Respond with `result = IOT_ERR_*` — iotcore reports the failure to the cloud |
| `BIND_CMD` DAO write fails | Respond with `result = IOT_ERR_*` |
| `ACTIVE_BY_*` smid not in DAO | Return `IOT_OK` silently — broadcast, may belong to another device |
| Execution: `IOT_DaoSmartGetBindCmds()` not found | Log warning, return — cross-device smid is normal |
| Trigger evaluation: no smid monitors this EID | Return `IOT_OK` silently |
| Trigger evaluation: condition not met | Return `IOT_OK` silently |
| `CHECK_SYNC` smid not found | Respond with `exists=false` — cloud re-binds the trigger |
| `REMOVE_ANNOUNCE` | Remove trigger (`IOT_DaoSmartTriggerRemove`) + schedule (`IOT_DaoSmartScheduleRemove`) + all bind cmds (`IOT_DaoSmartRemoveSmid`) for smid |
| `SCHEDULE_REMOVE_ANNOUNCE` | Remove schedule times only (`IOT_DaoSmartScheduleRemove(smid)`) |
| `TRIGGER_UPDATE_ANNOUNCE` | Reload trigger from event fields and overwrite in DAO |
| `SCHEDULE_UPDATE_ANNOUNCE` | Remove old schedule times, store new ones from `scheduleTimes` array |
| Delayed command timer alloc fails | Log error, skip that command, continue executing remaining commands |
| Loop: sender is AUTOMATION/SCHEDULE/COUNTDOWN | Skip trigger evaluation; return after dispatch |
