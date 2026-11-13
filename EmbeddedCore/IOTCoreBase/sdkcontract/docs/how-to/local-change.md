# Tell the contract about a local change

When your own hardware changes an element by itself — a button, a wall switch, a sensor edge —
tell the contract. It runs the same steps as for a command: the Device store, Record, the state
report, then Smart with the sender "device". Never report the change any other way.

## Pick the call

| Call | Use it for |
|---|---|
| `IOT_ContractReport(elemId, attrId, value)` | A single-field value of this device's own element: on/off, a level, a temperature |
| `IOT_ContractReportFields(elemId, attrId, fields, count)` | A value of several 16-bit fields (a colour, a paired temperature + humidity); `count` is `IOT_ContractAttrSize(attrId) / 2`, at most `IOT_CONTRACT_REPORT_FIELDS_MAX` |
| `IOT_ContractLocalChange(eid, elemId, attrId, value, len)` | A value whose layout is not a plain list of 16-bit fields, as raw bytes of the attribute's declared size; also takes a sub-device's EID |

```c
/* from your button task */
bool on = my_relay_toggle(elemId);
iot_err_t err = IOT_ContractReport(elemId, IOT_ATTR_ONOFF, on ? IOT_ON : IOT_OFF);

/* a colour: three fields, first field first */
const uint16_t hsv[3] = {h, s, v};
err = IOT_ContractReportFields(elemId, IOT_ATTR_COLOR_HSV, hsv, 3);
```

`elemId` is your own element number, as declared in `IOT_ContractConfig_t`; the contract
translates it. For a sub-device's own change use [`IOT_ContractSubDeviceReport()`](sub-devices.md).

## Before the device is set up

`IOT_ContractReport()` and `IOT_ContractReportFields()` hold the value when there is nothing to
report to yet — before provisioning, during it (the device is known but its elements are not
stored yet), and after a removal until the device is set up again. The latest value per element
and attribute is kept and sent with the first full state report; the call returns `IOT_OK`. A
table full of different held values refuses a new one with `IOT_ERR_NO_MEM` (a held one is still
replaced). `IOT_ContractLocalChange()` does not hold.

## Return codes

| Return | Meaning |
|---|---|
| `IOT_OK` | Committed (or held, see above) |
| `IOT_ERR_INVALID_ARG` | NULL value or fields, zero length or count; nothing done |
| `IOT_ERR_INVALID_SIZE` | The length or count is not the attribute's declared size; nothing done |
| the Device store's error | Not stored; nothing recorded or reported |
| `IOT_ERR_DATA_NOT_FOUND`, `IOT_ERR_NO_MEM` | Stored and recorded, but the report could not be assembled: nothing sent, Smart skipped |
| any other error | Stored and recorded; sending the report failed. Smart still ran, so local automations keep working offline |

## Rules

- **Task.** Any task that is not a timer callback, never an ISR: these calls block (storage, the
  network hand-off). Typically the task that handles your hardware input.
- **Size.** The value must have the attribute's declared size (`IOT_ContractAttrSize()`), or it is
  refused.
- **Drive first, then report.** Your hardware already changed; the contract does not call `apply`
  for a change you report.
