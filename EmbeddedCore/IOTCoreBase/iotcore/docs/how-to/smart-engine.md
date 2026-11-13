# Run automations in your own SDK (path B)

**Path B only.** On path A `IOT_ContractStart()` takes `IOT_EVENT_SMART` and does everything on this
page; read it only to understand what the contract does for you. The rules an implementation of
your own must keep (which sender feeds triggers, sync-on-off pairing, condition groups, schedule
slots run once) are the [Smart rulebook](../../../sdkcontract/docs/smart.md); where this page and the
rulebook differ, the rulebook is right. Every Smart event's task, data and answer is in
[events.md](../events.md#smart-events).

The smart automation system splits responsibility between the **cloud** and your **SDK on device**:

- **Cloud/App**: defines automations — user creates rules in the app, cloud assigns an SMID, and sends the configuration (commands, trigger rules, schedule times) to your device.
- **Your SDK**: **is the automation engine** — stores the rules, evaluates trigger conditions locally, checks time windows, runs the schedule ticker, executes bind-commands (with delay/reverse timers), and reports trigger activations back to cloud for cross-device routing.

The cloud does **not** evaluate triggers or run schedules — your device does all of that locally. The cloud only intervenes for **cross-device** automations: when a trigger fires on device A and the target is device B on a different hub, your SDK reports to cloud, and cloud forwards the activation to device B.

## How It Works (Big Picture)

```
┌──────────────────────────────────────────────────────────────────────┐
│  CLOUD / MOBILE APP  (defines rules, routes cross-device)            │
│                                                                      │
│  User creates automation in app:                                     │
│  "When motion detected (EID 5) between 6pm-6am → turn on light"     │
│                                                                      │
│  Cloud assigns SMID=0x0042 and sends configuration to device:        │
│    1. bind command  → what to do (turn on EID 1, attr ON)            │
│    2. bind trigger  → when to fire (watch EID 5, condition=EQUAL,    │
│                       automationSubType=DETECTED_MOTION, time 18:00-06:00)   │
│    3. (optional) schedule        → also run at 7:00 AM daily         │
└──────────────────┬───────────────────────────────────────────────────┘
                   │ protocol messages
                   ▼
┌──────────────────────────────────────────────────────────────────────┐
│  IOTCORE (Protocol Engine)                                           │
│  Parses messages → dispatches SMART events to your SDK               │
│  (SMART_EVENT_BIND_CMD, SMART_EVENT_BIND_TRIGGER, ...)               │
└──────────────────┬───────────────────────────────────────────────────┘
                   │ events with SMID, automationSubType, condition, etc.
                   ▼
┌──────────────────────────────────────────────────────────────────────┐
│  YOUR SDK — the automation engine (you implement all of this)        │
│                                                                      │
│  STORAGE (receive from cloud, persist in your DAOs):                 │
│    SMART_EVENT_BIND_CMD     → serialize commands → store in Smart DAO│
│    SMART_EVENT_BIND_TRIGGER → store trigger rule (condition, time, automationSubType)    │
│    SCHEDULE      → store minute-of-week times                        │
│                                                                      │
│  ACTIVATION (4 paths, all end at bind-command execution):            │
│                                                                      │
│    Path 1: Cloud/app says "run now"                                  │
│      ACTIVE_BY_USER → executeSmartCommands(smid)                     │
│                                                                      │
│    Path 2: Cloud/app sends schedule, or your local timer ticks       │
│      ACTIVE_BY_SCHEDULE → executeSmartCommands(smid)                 │
│                                                                      │
│    Path 3: Cloud forwards trigger from another device                │
│      ACTIVE_BY_TRIGGER → executeSmartCommands(smid, triggerValue)    │
│                                                                      │
│    Path 4: Local trigger evaluation (your SDK runs this locally):    │
│      Device state changes → load triggers for this EID →             │
│        check attr/element match → check condition → check time →     │
│        dispatch by automationSubType → executeSmartCommands(smid)            │
│        + report to cloud (for cross-device routing)                  │
│                                                                      │
│  EXECUTION (same for all 4 paths):                                   │
│    Load commands for SMID → for each command:                        │
│      - Resolve sync attrs (311→copy value, 312→invert)               │
│      - Apply delay timer if delay > 0                                │
│      - Dispatch STATE_SET (senderFrom=AUTOMATION for loop prevention)│
│      - Schedule reverse timer if reverse > 0                         │
└──────────────────────────────────────────────────────────────────────┘
```

## Key Concept: SMID

SMID (Smart ID) is a 16-bit ID assigned by the cloud that **links together** all parts of one automation:

```
SMID = 0x0042
    ├── Bind-Commands (stored per target EID)
    │     EID=0x0001: [attr=1, value=ON, delay=0, reverse=5s, elm=1]
    │     EID=0x0003: [attr=1, value=ON, delay=2s, reverse=0, elm=1]
    │
    ├── Trigger (optional — one per SMID)
    │     Watch: EID=0x0005, elmId=1, attrId=1
    │     Condition: EQUAL, value=ON
    │     Time window: 18:00-06:00, weekDay=all
    │     automationSubType: DETECTED_MOTION (66)
    │
    └── Schedule (optional — one per SMID)
          Times: [Mon 07:00, Tue 07:00, Wed 07:00, ...]
```

Core delivers these as separate events (`SMART_EVENT_BIND_CMD`, `SMART_EVENT_BIND_TRIGGER`, `SMART_EVENT_SCHEDULE_UPDATE_ANNOUNCE`), but they all share the same SMID.

## All Paths to Bind-Command Execution

There are **4 entry points**, all ending at `executeSmartCommands(smid)`:

```
1. Cloud manual activation
   SMART_EVENT_ACTIVE_BY_USER → executeSmartCommands(smid)

2. Schedule tick matches (cloud or local timer)
   SMART_EVENT_ACTIVE_BY_SCHEDULE → executeSmartCommands(smid)

3. Cloud forwards trigger from another device (cross-device)
   SMART_EVENT_ACTIVE_BY_TRIGGER → executeSmartCommandsEx(smid, attrValue)

4. Local trigger evaluation (device state changed)
   your STATE_SET handler → TriggerEvaluate() → condition matches
     → executeSmartCommandsEx(smid, triggerValue)   ← local execution
     → IOT_CoreRequestSmartTriggerReport()           ← tells cloud
```

## Smart Event Types

> With `sdkcontract` you do not implement the storage and evaluation columns below yourself: `IOT_ContractStart()` takes `IOT_EVENT_SMART` and does them.

| Event | When Dispatched | Your SDK Must |
|-------|----------------|---------------|
| `SMART_EVENT_BIND_CMD` | Cloud sends automation action binding | Serialize + store command list in your Smart DAO |
| `SMART_EVENT_UNBOUND_CMD` | Cloud removes one device from an automation | Remove that EID's commands from the SMID |
| `SMART_EVENT_REMOVE_ANNOUNCE` | Cloud deletes entire automation | Remove all data for the SMID (commands + triggers + schedules) |
| `SMART_EVENT_ACTIVE_BY_USER` | Cloud manually activates automation | Load bind-commands by SMID, execute them |
| `SMART_EVENT_ACTIVE_BY_SCHEDULE` | Cloud activates on schedule (or your local scheduler fires) | Store schedule time + execute bind-commands |
| `SMART_EVENT_ACTIVE_BY_TRIGGER` | Cloud activates trigger remotely | Execute bind-commands with trigger value |
| `SMART_EVENT_BIND_TRIGGER` | Cloud creates/updates a trigger rule | Store trigger in your Trigger DAO |
| `SMART_EVENT_UNBOUND_TRIGGER` | Cloud removes a trigger | Remove trigger from your Trigger DAO |
| `SMART_EVENT_TRIGGER_MODE` | Cloud enables/disables a trigger or schedule | Update enabled state in your DAO |
| `SMART_EVENT_TRIGGER_UPDATE_ANNOUNCE` | Cloud announces trigger key for sync check | Compare with stored trigger, log match/mismatch |
| `SMART_EVENT_SCHEDULE_UPDATE_ANNOUNCE` | Cloud sends schedule times | Store minute-of-week array in your Schedule DAO |
| `SMART_EVENT_SCHEDULE_REMOVE_ANNOUNCE` | Cloud removes a schedule | Remove schedule from your Schedule DAO |
| `SMART_EVENT_CHECK_SYNC` | Cloud requests sync status | Reply with stored trigger state for the SMID |


## Handling Bind-Commands (Store + Execute)

When the cloud binds commands to an smart (SMID), serialize and store them:

```c
case SMART_EVENT_BIND_CMD:
{
    IOT_CoreSmartEventBindCmdData_t *data = &smartEvent->data.bindCmd;
    // data->smid       — smart ID
    // data->eid        — target device
    // data->rootEid    — root device of target
    // data->cmdSize    — number of commands
    // data->attrValueList — array of commands:
    //   each has: .attrValue (attrId, attrSize, attrValue), .reverse, .delay, .elmId
    //   .delay   — seconds to wait before executing this command (0 = immediate)
    //   .reverse — seconds after execution to undo the command (0 = no undo)
    //              e.g., reverse=300 on ONOFF=ON means: turn ON now, turn OFF after 5 minutes

    // Serialize command list into flat bytes
    uint16_t blobLen = 0;
    uint8_t *blob = serializeBindCmds(data->attrValueList, data->cmdSize, &blobLen);

    // Store metadata (rootEid, protocol, filter) + command blob
    YourSmartDaoSetBindCmd(data->smid, data->eid, blob, blobLen,
                           data->rootEid, data->prtc, data->filter);
    SAFE_FREE(blob);
    break;
}
```

When activated (by user, schedule, or trigger), load and execute:

```c
case SMART_EVENT_ACTIVE_BY_USER:
{
    IOT_CoreSmartEventActiveByUserData_t *data = &smartEvent->data.activeByUser;
    executeSmartCommands(data->smid);  // Your function — loads bind-cmds, dispatches STATE_SET
    break;
}
```

### Bind-Command Execution Detail

`executeSmartCommands(smid)` loads the stored command blob and dispatches STATE_SET per target device:

```c
void executeSmartCommandsEx(uint16_t smid, uint8_t *triggerValue, uint16_t triggerValueSize)
{
    // 1. Load bind-commands from DAO
    //    YourSmartDaoGetBindCmds(smid) → per-EID command blobs

    // 2. For each target EID's command list:
    for (each command) {
        // a. Resolve sync attributes:
        //    attrId=311 (SYNC_NORMAL)    → replace value with triggerValue, attrId becomes 1
        //    attrId=312 (SYNC_REVERSING) → replace value with inverted triggerValue, attrId becomes 1
        //    else                         → use stored value as-is

        // b. Apply delay (if delay > 0, schedule via IOT_TimerInsertCb in ms; its callback
        //    only hands the command to core's task with IOT_CoreDeferWork())

        // c. Dispatch STATE_SET with senderFrom = IOT_SENDER_AUTOMATION
        //    → reaches your STATE_SET handler, but senderFrom >= IOT_SENDER_SCENARIO_UUID_V4
        //      blocks trigger re-evaluation

        // d. Apply reverse (if reverse > 0, schedule undo after N seconds)
    }
}
```

## Handling Triggers (Store + Evaluate)

Store trigger rules when the cloud binds them:

```c
case SMART_EVENT_BIND_TRIGGER:
{
    IOT_CoreSmartEventBindTriggerData_t *data = &smartEvent->data.bindTrigger;
    // Key fields:
    // data->bindTriggerElmInfo.smid       — SMID to fire
    // data->bindTriggerElmInfo.eid        — device EID to watch
    // data->bindTriggerElmInfo.elmId      — element to watch
    // data->bindTriggerElmInfo.condition  — EQUAL, BETWEEN, ANY, EXIST
    // data->bindTriggerElmInfo.smartType  — automationSubType (motion, sync, stair, etc.)
    // data->primaryAttrValue              — threshold value
    // data->timeStart / data->timeStop    — active time window (minutes from midnight)
    // data->weekDay                       — day bitmask (0x00 = all days)
    // data->bindTriggerKey                — owner/ext EID for sync checks

    // Build your trigger struct and store
    YourTrigger_t trigger = { /* populate from data */ };
    YourTriggerDaoSet(data->bindTriggerElmInfo.smid, &trigger);
    break;
}
```

### Trigger Evaluation Flow

Trigger evaluation runs locally in your state SET handler, **after** the device state is updated:

```
Device state changes (e.g., motion sensor attr=1 goes ON)
    │
    ▼
Your STATE_SET handler
    │ 1. Update DAO, actuate hardware, send state report
    │ 2. Check: senderFrom >= IOT_SENDER_SCENARIO_UUID_V4? → STOP (loop prevention)
    ▼
YourTriggerEvaluate(eid, devSetInfo)
    │
    │ 3. Load SMID list for this EID from trigger index
    │
    ▼ For each SMID:
    │ 4. Load trigger struct
    │ 5. Skip if disabled
    │ 6. Check attr match  (trigger.attrId == changed attrId?)
    │ 7. Check element match (any changed elem == trigger.elmId?)
    │ 8. Check condition:
    │      COND_ANY(1)=always | COND_EQUAL(2) | COND_EXIST(3) | COND_BETWEEN(4)
    │ 9. Check time window (minute-of-day + weekday bitmask)
    │
    ▼ ALL checks passed → dispatch by automationSubType (see table below)
```

## Trigger Action Types (automationSubType / AUTOMATION_SUB_TYPE)

| Type | Value | Execution Path | Action |
|------|-------|----------------|--------|
| `STAIR_SWITCH` | 64 | **Inline** | Direct device control on eidMix (linked stair light) — bypasses bind-commands. Legacy: the mirror to another device, `IOT_CoreSmartStairSwitchMirror()`, is a stub in the shipped library (`IOT_ERR_NOT_SUPPORTED`); use sync-on-off (71) |
| `NOTIFICATION` | 65 | **Inline** | Sends notification to cloud (state report with pushNotify=true) — no device control |
| `DETECTED_MOTION` | 66 | Bind-commands | Loads SMID bind-commands → device control |
| `NO_MOTION_BY_EVENT` | 67 | Bind-commands | Loads SMID bind-commands → device control |
| `NO_MOTION_BY_TIME` | 68 | Bind-commands | Loads SMID bind-commands → device control |
| `REVERSE_ON_OFF` | 70 | **Inline** | Direct device control on the SAME triggering device — flips ON/OFF |
| `SYNC_ON_OFF_MULTI_DEVICE` | 71 | Bind-commands | Loads SMID bind-commands → device control (uses sync attrs 311/312) |
| `TRIGGER_ACTION_SWITCH_SCENE` | 96 | Bind-commands | Loads SMID bind-commands → device control |
| `ZONE_CONTROL` | 128 | Bind-commands | Loads SMID bind-commands → device control |

**Inline** types handle the action directly without loading bind-commands. **Bind-commands** types call `executeSmartCommands(smid)` + `IOT_CoreRequestSmartTriggerReport()` (local execution + cloud notification for cross-device).

Low-numbered automationSubType values (e.g., automationSubType=2) are generic — they fall through to the bind-commands path. The actual behavior is determined by the command's attribute ID (e.g., attr 311 = sync normal, attr 312 = sync reversing).

### Local vs Cross-Device

Both local exec AND cloud report fire simultaneously for default automationSubTypes:

- **Local**: `executeSmartCommands()` dispatches STATE_SET for target devices this hub controls directly. May have no bind-cmds for remote devices — that's normal.
- **Cross-device**: `IOT_CoreRequestSmartTriggerReport()` tells the cloud the trigger fired. Cloud then sends `SMART_EVENT_ACTIVE_BY_TRIGGER` to the remote device's hub, which executes its own locally-stored bind-commands.

```
Device A (motion sensor)                     Device B (light, different hub)
    │                                              │
    ├─ executeSmartCommands(smid)                   │
    │   (controls local devices)                    │
    │                                              │
    └─ SmartTriggerReport → Cloud ─────────────────►│
                                   ACTIVE_BY_TRIGGER│
                                                    ▼
                                   executeSmartCommandsEx(smid, attrValue)
                                      (controls device B)
```

## Handling Schedules (Store + Tick)

Store schedule times and run a periodic checker:

```c
case SMART_EVENT_SCHEDULE_UPDATE_ANNOUNCE:
{
    IOT_CoreSmartEventScheduleUpdateAnnounceData_t *data = &smartEvent->data.scheduleUpdateAnnounce;
    // data->smid              — smart ID
    // data->scheduleTimes     — array of minute-of-week values (UTC)
    // data->scheduleTimeCount — array length

    // Minute-of-week: day * 1440 + hour * 60 + minute (range 0-10079)
    // day: 0=Sunday, 1=Monday, ..., 6=Saturday
    YourScheduleDaoSet(data->smid, data->scheduleTimes, data->scheduleTimeCount);
    break;
}

case SMART_EVENT_SCHEDULE_REMOVE_ANNOUNCE:
{
    IOT_CoreSmartEventScheduleRemoveAnnounceData_t *data = &smartEvent->data.scheduleRemoveAnnounce;
    YourScheduleDaoRemove(data->smid);
    break;
}
```

Run a periodic timer (e.g., every 60 seconds) that checks all schedules against the current UTC
minute-of-week and fires matches. `IOT_TimerInsertCb()` takes **milliseconds** and fires once, so
re-arm it each tick. The timer callback runs on a small timer stack where store reads and reports
must not run: it only hands the work to core's task with `IOT_CoreDeferWork()`.

```c
static void schedulerRun(void *arg)            // core's task: store reads and reports are fine
{
    uint16_t currentMoW = getCurrentMinuteOfWeek();  // day*1440 + hour*60 + minute, UTC
    // Load all schedules; for each enabled one holding currentMoW, run the slot once
    // (keyed on SMID + minute of week, rulebook rule 8) → executeSmartCommands(smid)
}

static void schedulerTickCb(void *arg)         // timer callback: re-arm and hand over, nothing else
{
    IOT_CoreDeferWork(schedulerRun, NULL);           // NULL: nothing for core to free
    IOT_TimerInsertCb(schedulerTickCb, NULL, 60 * 1000);
}
```

## Handling Trigger Mode (Enable/Disable)

```c
case SMART_EVENT_TRIGGER_MODE:
{
    IOT_CoreSmartEventTriggerModeData_t *data = &smartEvent->data.triggerMode;
    switch (data->type)
    {
    case IOT_CORE_SMART_TRIGGER_MODE_SET_TRIGGER:
        // Enable/disable a specific trigger by SMID
        YourTriggerDaoSetEnabled(data->data.setTrigger.smid,
                                  data->mode, data->data.setTrigger.minuteDisable);
        break;
    case IOT_CORE_SMART_TRIGGER_MODE_SCHEDULE:
        // Enable/disable a schedule by SMID
        YourScheduleDaoSetEnabled(data->data.schedule.smid, data->mode);
        break;
    case IOT_CORE_SMART_TRIGGER_MODE_ENABLE_DISABLE_AUTOMATION:
        // Enable/disable all triggers for a device EID
        YourTriggerDaoSetEnabledByEid(data->data.enableDisableAutomation.eid, data->mode);
        break;
    case IOT_CORE_SMART_TRIGGER_MODE_ENABLE_DISABLE_ALL_AUTOMATION:
        // Enable/disable every automation this device holds
        YourTriggerDaoSetEnabledAll(data->mode);
        break;
    default:
        break;
    }
    break;
}
```

## Loop Prevention

When your smart execution dispatches STATE_SET, use `senderFrom = IOT_SENDER_AUTOMATION`. In your state handler, skip trigger evaluation when `senderFrom >= IOT_SENDER_SCENARIO_UUID_V4`:

```
senderFrom <  IOT_SENDER_SCENARIO_UUID_V4  (control, app, cloud, phones, device)  → trigger eval runs
senderFrom >= IOT_SENDER_SCENARIO_UUID_V4  (scenario, automation, schedule,
                                            countdown, voice assistants, third party) → BLOCKED
```

This prevents: trigger → bind-cmd → STATE_SET → trigger → infinite loop. The threshold is numeric,
so every sender from `IOT_SENDER_SCENARIO_UUID_V4` (0x13) up, voice assistants included, is held
back ([concepts.md](../concepts.md#loop-prevention)).


## Smart Trigger Report API

When a trigger fires locally (device detects state change → condition matches), your SDK must notify the cloud so it can dispatch bind-commands to **other** devices (cross-device automation):

```c
iot_err_t IOT_CoreRequestSmartTriggerReport(const IOT_CoreSmartTriggerReport_t *report);
```

```c
typedef struct {
    uint16_t smid;            // Smart automation ID
    uint16_t attrId;          // Triggered attribute ID
    uint16_t attrSize;        // Size of triggered attribute value
    const uint8_t *attrValue; // Triggered attribute value
    uint8_t triggerKey[8];    // Trigger key: opaque; copy from the bind
    uint8_t timeCfg[4];      // Time config: opaque; copy from the bind
} IOT_CoreSmartTriggerReport_t;
```

**When to call**: In your trigger evaluation, after a trigger condition matches and you've executed local bind-commands, send this report so the cloud knows the trigger fired:

```c
// After executeSmartCommands(smid) succeeds:
IOT_CoreSmartTriggerReport_t report = {
    .smid = trigger.smid,
    .attrId = trigger.attrId,
    .attrSize = devSetInfo->attrSize,
    .attrValue = devSetInfo->attrValue,
    // triggerKey and timeCfg: copied unchanged from the bind you stored
};
IOT_CoreRequestSmartTriggerReport(&report);
```

**Notification triggers** (automationSubType = 65): These don't execute bind-commands. Instead, send a state change report with `pushNotify = true` so the cloud sends a push notification to the user's phone:

```c
IOT_CoreStateChangeDeviceData_t deviceData = {
    // ... normal fields (eid, mac, devId, fullState, changedElemIds) ...
    .pushNotify = true,  // triggers push notification on user's phone
};
IOT_CoreRequestReportStateChanged(&report);
```

## The other Smart calls

All in `IOT_CoreSmart.h`; [reference.md](../reference.md#iot_coresmarth) has one line each, and the
rulebook says when each is owed.

| Call | When |
|---|---|
| `IOT_CoreRespondSmartCmd()` | Answer a bind or unbind of a trigger or commands, a trigger mode or an enable/disable, after the store work (it sends nothing when `ctx.requiresResponse` is false) |
| `IOT_CoreRespondSmartCheckSync()` | Answer `SMART_EVENT_CHECK_SYNC`; with `exists` false core sends nothing |
| `IOT_CoreRequestSmartSyncOnOffReport()` | A sync-on-off (sub-type 71) element changed locally: broadcast the new value to the location |
| `IOT_CoreRequestSmartReportCondState()` | Your seat of a condition group (sub-type 2048) changed: broadcast it |
| `IOT_CoreRequestSmartSyncCondPush()` | Announce your condition-group seats after a bind |
| `IOT_CoreRequestSmartSyncCondRequest()` | After a reboot, ask the location for the state of your condition groups |
| `IOT_CoreRequestSmartSyncCondResponse()` | Answer a peer's sync request, only for groups whose state differs |
| `IOT_CoreRequestSmartCondNotMember()` | Answer an ask-before-fire sync request for a group you are not in |
| `IOT_CoreSmartStairSwitchMirror()` | Legacy stair-switch mirror; a stub returning `IOT_ERR_NOT_SUPPORTED` in the shipped library |
