# Smart Contract

How a device stores and runs the automations the cloud and the app create, and the rules that keep
them correct.

This guide is the Smart rulebook for both ways of building an SDK:

- **Path A — you build on `sdkcontract/`** ([../../sdkcontract/README.md](../../sdkcontract/README.md)).
  `IOT_ContractStart()` takes the `IOT_EVENT_SMART` events and implements Smart (the stores,
  trigger evaluation, schedules, condition groups); it keeps every rule below for you, except the
  ones marked **yours on both paths**.
- **Path B — you write your own Smart** against the public `iotcore` API: the `IOT_EVENT_SMART`
  events in `IOT_CoreEventSmart.h`, the helpers in `IOT_CoreSmart.h`, and state control and
  reporting. Every rule below is then yours to keep.

## How Smart works

The cloud configures automations by sending `IOT_EVENT_SMART` events: bind and unbind triggers and
bind-commands, schedule updates, enable/disable, removal announcements, and run requests. The
device stores them, evaluates its triggers against its own state changes, runs its schedules, and
exchanges condition-group state with its peers.

When an automation fires, its actions are not applied inside Smart: they go back through core as
ordinary STATE_SET commands, and the device turns the hardware on or off exactly as for a command
from the app (with sdkcontract: through your `apply`). Smart never touches the hardware. A fired trigger is
reported to the cloud with `IOT_CoreRequestSmartTriggerReport()`.

## Rules

1. **Store every SMART event the cloud sends, and only through one owner.** Triggers, schedules and
   bind-commands are persisted in a layout the next firmware reads; changing that layout is an
   upgrade-path change.
   With sdkcontract: done for you; the Smart stores are internal to the contract.
2. **Actuate dispatched commands like any other STATE_SET. Yours on both paths.** An automation
   that fires reaches the device as a STATE_SET; if it is not actuated, the automation fires and
   nothing happens.
   With sdkcontract: it reaches your `apply` like any command, with `source` set to
   `IOT_CONTRACT_SOURCE_AUTOMATION` or `IOT_CONTRACT_SOURCE_SCHEDULE`.
   On path B: dispatch each action as an `IOT_EVENT_STATE` event of type
   `IOT_EVENT_TYPE_STATE_SET` with `IOT_CoreDispatchEvent()` (`IOT_Core.h`), with the request's
   sender set to the automation sender, so it reaches your own STATE_SET handler.
3. **Feed every state change to trigger evaluation, after storing it. Yours on both paths.** Every
   element-state change -- from a STATE_SET, your own hardware or a sub-device -- must reach
   trigger evaluation, with the cloud's element ids. Condition groups re-read element state from
   the device's stored state when they evaluate, so store the new state first; the other order
   evaluates the old state.
   With sdkcontract: done for you on every commit; a change your hardware made by itself goes
   through `IOT_ContractLocalChange()`, a sub-device's through `IOT_ContractSubDeviceReport()`.
4. **Keep the loop guard. Yours on both paths.** A change caused by a scenario, automation,
   schedule, countdown or voice sender (`senderFrom >= IOT_SENDER_SCENARIO_UUID_V4`) is not fed to
   trigger evaluation. It is Smart's own output, and feeding it back re-fires triggers and chains
   automations. So pass the sender of the request that caused the change, unchanged; a change the
   device made itself passes `IOT_SENDER_DEVICE`.
   With sdkcontract: done for you; the contract carries the request's sender through the commit,
   and a sub-device confirming a change the contract applied keeps that change's sender.
5. **Honour a sync-on-off mirror only from a pairing you hold.** With a sync-on-off or stair-switch
   automation, a paired device mirrors its on/off to its partner as a STATE_SET whose attribute is a
   sync-on-off carrier (`IOT_AttrResolveSyncOnOffCarrier()` in your attribute table recognises it).
   Act on it only when every named element of every addressed device belongs to such a pairing
   this device holds; then apply it as the ordinary on/off attribute with the sender set to
   `IOT_SENDER_AUTOMATION`, so rule 4 keeps the mirrored change from firing the pairing back
   (which would mirror back again, forever). Otherwise ignore it and send no state report; core
   answers the sender.
   On path B: the sending side mirrors with `IOT_CoreRequestSmartSyncOnOffReport()` (sync-on-off)
   and `IOT_CoreSmartStairSwitchMirror()` (stair switch), both in `IOT_CoreSmart.h`; the receiving
   side recognises the carrier with `IOT_AttrResolveSyncOnOffCarrier()`.
   With sdkcontract: done for you before `apply` is called; a refused mirror never reaches
   `apply` (subscribers see `IOT_CONTRACT_EVT_MIRROR_REFUSED`).
6. **Trigger condition `ANY` (code 1) fires on every change.** Do not treat it as "on change from a
   different value". Actions behind such a trigger have to be idempotent, which is why the
   auto-reverse and stair-switch actions are synthesised from the trigger value rather than stored.
7. **Time frames.** Schedule times are minutes of the week in UTC0, compared against UTC. A
   trigger's time window (`timeStart`, `timeStop`, weekday mask) is compared against the device's
   local wall-clock time. A trigger with a window never fires before the clock is synced; one
   without a window always fires. The frame of the trigger window is under review and may change
   in a later release.
   With sdkcontract: enforced inside the component; there is nothing to configure.
8. **Run a scheduled slot once.** The cloud may request a schedule run while the device's own
   offline tick sees the same minute. Both must go through one claim keyed on (SMID, minute of
   week), so a non-idempotent action does not run twice.
9. **Do not renumber condition-group seats.** A device owns one or more seats of a K-of-N group.
   When a member is removed the other seats keep their index, and the survivors shrink the pool
   rather than the requirement (2 of 3 with one seat gone still needs 2). The first member's bind
   may arrive alone and is not repeated when the group grows; a device joins the group when a
   peer's report proves it.
   On path B: peers' reports arrive as `SMART_EVENT_REPORT_COND_STATE` and
   `SMART_EVENT_SYNC_COND_REQUEST`; send this device's own seat with
   `IOT_CoreRequestSmartReportCondState()`, announce after a bind with
   `IOT_CoreRequestSmartSyncCondPush()`, answer a sync request with
   `IOT_CoreRequestSmartSyncCondResponse()` (or `IOT_CoreRequestSmartCondNotMember()` for a group
   this device is not in) -- all in `IOT_CoreSmart.h`.
10. **Keep the three synthesised action sub-types.** Stair switch (64), auto-reverse (70) and
    sync-on-off (71) synthesise their action from the trigger, with no stored bind-command.
    Sync-on-off has its own dispatch and never occupies a condition-group seat.
    On path B: the synthesised action is dispatched with `IOT_CoreDispatchEvent()` like any other
    (rule 2); sync-on-off and stair switch also mirror to their partner with the calls in rule 5.
11. **Resume after every cloud connect. Yours on both paths.** Stored schedules must run again
    after a reboot, and condition groups must re-sync with their peers, whose changes may have been
    missed while offline.
    On path B: on `DEV_EVENT_CLOUD_CONNECTED`, start your schedule tick and ask the peers for their
    seats with `IOT_CoreRequestSmartSyncCondRequest()` (`IOT_CoreSmart.h`).
    With sdkcontract: done for you on every cloud connect, before your `onCoreEvent` sees it; the
    scheduler waits for the time to be known by itself.
12. **Forget a device's automations when the device goes.** A removed device's triggers and
    bind-commands are deleted, and a factory reset deletes all of them.
    With sdkcontract: done for you when the cloud removes a device, and on a factory reset
    (`IOT_CoreFactoryReset()`).

## sdkcontract: your inputs at a glance

| Input | Call | If you skip it |
|---|---|---|
| Start-up | `IOT_ContractStart()` once at boot, with your `apply` | Nothing is stored or run |
| Actuation | `apply`, for commands Smart dispatches too | Automations fire and nothing happens |
| Your own changes | `IOT_ContractLocalChange()` / `IOT_ContractSubDeviceReport()` | Triggers never see the change |
| Attribute table | `IOT_ATTR_DEFINES_DIR` (see `sdkcontract/README.md`, *Building*), with `IOT_AttrResolveSyncOnOffCarrier()` | The build stops |

## See also

- [3_Smart_Automation.md](3_Smart_Automation.md): path B only, a walk-through of every
  `IOT_EVENT_SMART` event and one way to store and run automations yourself. Where it and this
  guide differ, this guide is right.
- [7_Record_Contract.md](7_Record_Contract.md): the Record rulebook.
- [../core/API_Document.md](../core/API_Document.md): SMART events in §3, the Smart reporter
  calls in §4.
