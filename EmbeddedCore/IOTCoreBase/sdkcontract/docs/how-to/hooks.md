# Use the device hooks

Three optional callbacks in `IOT_ContractCallbacks_t` tell you when a device joins or leaves. The
contract has already stored the change (or is about to wipe it); the hooks are for what you hold
yourself.

| Hook | When | Do here |
|---|---|---|
| `onDeviceJoined(eid, phase)` | A sub-device reached a join phase (below), at most once per phase, in order | Index the device, refresh your radio's view, ask it for data. The contract answers core after you return |
| `onDeviceRemoving(eid, root)` | Before the contract deletes anything; the device's record is still readable | Release what you hold for it; reset it on its own network (its key is still in its record) |
| `onDeviceRemoved(eid, root)` | The device is gone from every store and, for a removal the cloud asked for, the cloud has its answer. Also after a failed removal | `root` true: this device was removed: reset your own data, then restart (or let the contract restart, below). `root` false: a sub-device; do not restart |

## Join phases

| Phase | Called |
|---|---|
| `IOT_DEV_JOIN_PHASE_DEVICE_INFO` | As soon as the device's common information (type, protocol, network address) is in the Device store, so you can index it and attribute its traffic before the join finishes. Its identity and group entry are written after the hook returns; if either then fails, the phase is not answered. Not called when the common information could not be stored |
| `IOT_DEV_JOIN_PHASE_ELM_INFO` | After its elements are stored; not called when they could not be |
| `IOT_DEV_JOIN_PHASE_SYNC_COMPLETE` | The end of the join, whatever became of storing its network key (a failed key save is still reported to core as a failure) |

A phase the contract does not know is not passed on. Read the device from the Device store. When
the join completes, subscribers see `IOT_CONTRACT_EVT_DEVICE_ADDED`.

## Removals

`root` is true when this device itself was removed — by the cloud, or by a factory reset
(`IOT_CoreFactoryReset()`). The contract's stores are then wiped (as `IOT_ContractFactoryReset()`)
and the device **must** restart: a device that keeps running after a removal runs with its data
wiped. Either restart from `onDeviceRemoved`, or call `IOT_ContractRemovalConfigure(true)` before
`IOT_ContractStart()` and the contract restarts once the hook returns (or at once, with no hook).
A hook that restarts the chip itself never returns, so restarting yourself stays possible either
way. A sub-device's removal never restarts.

A sub-device that joins again under a new EID loses its old entry through the same two removal
hooks. Subscribers see `IOT_CONTRACT_EVT_DEVICE_REMOVED` before `onDeviceRemoved`.

## Rules

- All hooks run on the task that delivers core events — for a factory reset you call, on your task
  inside `IOT_CoreFactoryReset()`. They may block briefly; the join or removal waits for them.
- NULL hooks are fine: nothing to do.
