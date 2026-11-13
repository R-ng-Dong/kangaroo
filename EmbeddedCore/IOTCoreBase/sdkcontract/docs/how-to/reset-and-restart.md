# Factory reset, contract reset and restarts

## Product factory reset (long press, console command)

Call `IOT_CoreFactoryReset()`. Core wipes its configuration and reports the reset
(`DEV_EVENT_FACTORY_RESET`, not a removal); the contract wipes its stores and calls
`onDeviceRemoved(eid, true)`, where you reset your own data and restart. All of it runs on your
task before `IOT_CoreFactoryReset()` returns.

To have the contract restart instead, call `IOT_ContractRemovalConfigure(true)` before
`IOT_ContractStart()`: it restarts once `onDeviceRemoved` returns (or at once with no hook), so you
only reset your data there. The same applies when the cloud removes this device. Either way, the
device must restart: a device that keeps running after a removal runs with its data wiped.

## Contract reset: `IOT_ContractFactoryReset()`

Wipes only the contract's stores — for an SDK's own recovery, for example a store found unreadable
at boot. It is **not** a product reset: core's configuration, the cloud's view of the device and
your callbacks are untouched.

It reads the EID list first, because the Device store is wiped last and the per-device stores (IR,
Record) are keyed by it. Order: Smart triggers and their bind commands, condition-group state,
schedules, IR, Record per device, then Device and Group. Anything of yours keyed by the same EIDs
must read them before calling it. Each store is reset on a best-effort basis; it returns `IOT_OK`.
Blocks (storage erases); any task, not a timer callback.

## A restart of your own

Call `IOT_ContractPrepareRestart()` first. It closes Record's held windows, then writes Record's open
blocks and the Device store's element state to storage before it returns; without it, up to `IOT_RECORD_FLUSH_INTERVAL_SEC` of
history and the latest element state are lost. It returns `IOT_OK` or the first error from the two
flushes (both are always attempted); the restart may still go ahead. Not needed on a factory reset,
which wipes the stores anyway.

A reboot or update **requested by the cloud** is flushed for you before `onCoreEvent` sees it
([core-events.md](core-events.md#reboot-and-update)).

## When the power is about to fail

Some products learn that the mains has gone a moment before the supply does, for example from a
supply monitor or a brownout input with a capacitor holding the rail up. Use that moment to call
`IOT_ContractPrepareRestart()`. It is the same call as before a restart: it closes Record's held
windows early, writes Record's open blocks and the Device store's element state, and returns when
they are in storage.

- **Call it from a task.** An interrupt should only wake that task (a notification or a semaphore).
  The call blocks on storage, so it cannot run in an interrupt or a timer callback.
- **Time it on your board.** It waits up to about a second for Record's lock. Then it writes at most
  `IOT_RECORD_MAX_OPEN_BLOCKS` history blocks plus the element state. Measure the call on your
  hardware with history active, and make sure it fits inside your hold-up time. For scale: on an
  ESP32-S3 with five history series dirty or held, it took about 50 ms.
- **A write cut off by the power keeps the previous copy on ESP32.** NVS replaces an entry only once
  the new one is complete, so a block that did not finish writing is still there as it was at the
  previous flush. On a platform with its own storage, that guarantee is your storage
  implementation's.
- **Nothing stops.** If the power comes back, the device keeps recording, and you may call the
  function again on the next warning.

Without this call, a power cut loses up to `IOT_RECORD_FLUSH_INTERVAL_SEC` of history, any value a
window is still holding, and about the last second of element state.

## When `IOT_ContractStart()` fails

It returns `IOT_ERR_INVALID_ARG` (a NULL config, model id, callbacks or `apply`; nothing done), a
store or registration error (core is not started), or core's own start-up error. Treat any of them
as fatal for this boot. If a firmware update is pending at that point, discard it and restart: a
pending update makes every boot an update boot, so the device would retry the same failing start
forever and could not be set up again.

```c
if (err != IOT_OK && IOT_CoreOtaIsPending() && IOT_CoreOtaDiscardPending() == IOT_OK)
{
    IOT_ApplicationRestartChip();
}
```
