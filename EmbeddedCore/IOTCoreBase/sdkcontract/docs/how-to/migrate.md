# Move an existing SDK onto the contract

1. **Delete what the contract now does.** Your own Device, Group and IR stores, any Smart engine or
   Record you wrote, and your own handling of state commands, joins, group binds and removals.
   Delete your copy of the attribute table too, and register your own ids with
   `IOT_ContractRegisterAttrs()` ([custom-attributes.md](custom-attributes.md)).
2. **Replace your start.** Your core start and event registration become one
   `IOT_ContractStart()`; move your actuation into [`apply`](apply.md) and your hardware input onto
   [`IOT_ContractReport()` / `IOT_ContractReportFields()`](local-change.md) (or
   `IOT_ContractLocalChange()` for an irregular layout) and, on a hub,
   [`IOT_ContractSubDeviceReport()`](sub-devices.md). Keep only what is still yours in
   `onCoreEvent` ([core-events.md](core-events.md)).
3. **Re-provision.** Devices that move from your own stores keep no Device, Group or Smart data:
   the contract does not read what your stores wrote. Re-provision every such device after the
   update (test devices: erase the flash first).
