# Run a hub's sub-devices

A hub drives other devices over its own radio. The contract stores them, resolves commands to them
and calls your `apply` with the sub-device's EID; your SDK sends the value on and passes back what
the sub-devices report.

## Commands to a sub-device

`apply` receives the sub-device's `eid` and the element id its hub assigned (used as given, never
translated). Send the value over your radio and return `IOT_OK` once it was sent. A device
attribute set for a sub-device is stored without calling `apply`.

## Every report from a sub-device

```c
/* from the task that receives your radio's messages */
iot_err_t err = IOT_ContractSubDeviceReport(eid, elemId, attrId, value, len, pushNotify);
```

Pass **every** state a sub-device reports, including the ones that confirm a change you sent from
`apply`:

- **A confirmation** — the same element, attribute and value bytes as a change the contract applied
  to that sub-device within the last 3 seconds. Only the Device store is updated: the change was
  already recorded, reported and given to Smart, with the sender of the request that caused it.
  Each applied change is confirmed at most once, and raises no contract event.
- **Anything else** is the sub-device's own change and is committed like a
  [local change](local-change.md): Device store, Record, the state report (with
  `pushNotify`, whether the report may raise a push notification), then Smart with the sender
  "device". Smart is skipped only when the report could not read the device
  (`IOT_ERR_DATA_NOT_FOUND`).

The value must have the attribute's declared size (`IOT_ContractAttrSize()`); any other size is
refused with `IOT_ERR_INVALID_SIZE`, so one malformed report cannot shrink the stored value. The
element must already be known to the Device store; an attribute it does not hold yet is added.
Return codes are as for `IOT_ContractLocalChange()`. Call it from any task except a timer callback.

## Joins and removals

The contract stores a joining sub-device phase by phase and removes it from every store when the
cloud removes it, calling your [hooks](hooks.md) on the way. A sub-device that joins again under a
new EID loses its old entry through the same two removal hooks.

## Hub notes

- There is no public group read: the contract resolves groups itself.
- To find a sub-device by MAC or device id, loop over `IOT_DaoDeviceGetAllEids()` and read each with
  `IOT_DaoDeviceGetIdentityInfo()` ([read-state.md](read-state.md)).
- `IOT_ContractRecordSetElementEnabled()` takes the hub-assigned element id for a sub-device.
- A sub-device's mesh device key, if it has one, is readable with `IOT_DaoDeviceGetMeshKey()`.
