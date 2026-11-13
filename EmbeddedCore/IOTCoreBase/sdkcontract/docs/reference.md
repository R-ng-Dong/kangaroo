# Reference

Every public name in `sdkcontract/include/`, one table per header, one line each. The header
comment is the full contract for each name (arguments, return codes, blocking, task); follow the
link. Every name a public header declares is listed here, and a check in our build enforces it.

## IOT_Contract.h

[`include/IOT_Contract.h`](../include/IOT_Contract.h) — start, apply, reports, hooks, contract events.

### Functions

| Function | What it does |
|---|---|
| `IOT_ContractStart(config, cb)` | The one call that brings the device up: opens the stores, takes every core event category, starts core. Boot task, once |
| `IOT_ContractIrConfigure(tx, rx, detect)` | Declares the device's IR capability, used to answer the cloud. Before start; not called = no IR |
| `IOT_ContractRemovalConfigure(restart)` | `true`: the contract restarts the device after this device is removed (after `onDeviceRemoved` returns). Before start |
| `IOT_ContractRestoreConfigure(policy)` | Chooses what a provisioned device does at boot with its last state (`IOT_ContractRestorePolicy_t`). Before start |
| `IOT_ContractDeviceStatusConfigure(adjust)` | Installs a hook that may change the status the contract answers a device status request with. Before start; NULL = as filled |
| `IOT_ContractFactoryReset()` | Wipes every contract store and nothing else; for an SDK's own recovery, not a product reset |
| `IOT_ContractPrepareRestart()` | Closes Record's held windows and writes out Record's open blocks and the Device store's element state, before a restart of your own or when the power is about to fail |
| `IOT_ContractLocalChange(eid, elemId, attrId, value, len)` | Your hardware (or a sub-device) changed an element: store, Record, report, Smart with the sender "device". Raw value bytes |
| `IOT_ContractReport(elemId, attrId, value)` | Report one single-field value of this device's own element; the usual call for a button or sensor |
| `IOT_ContractReportFields(elemId, attrId, fields, count)` | Report a value given as 16-bit fields (colour, a paired reading); held until provisioning |
| `IOT_ContractApplyField(req, index, out)` | Read field `index` of the value inside `apply` |
| `IOT_ContractElementStoreId(eid, elemId)` | The id the Device store keeps one of your elements under |
| `IOT_ContractSubDeviceReport(eid, elemId, attrId, value, len, pushNotify)` | A hub passes a sub-device's reported state; an echo of an applied change is stored only |
| `IOT_ContractSubscribe(fn, arg)` | Subscribe to contract events. Before start; up to `IOT_CONTRACT_EVENT_SUBSCRIBERS_MAX` |
| `IOT_ContractGetStatus()` | The current device status (`IOT_ContractStatus_t`). Any task |

### Types and callbacks

| Name | What it is |
|---|---|
| `IOT_ContractConfig_t` | What every device tells the contract at start: `modelId`, `elementIds`, `elementCount` |
| `IOT_ContractCallbacks_t` | Your callbacks: `apply` (required), `onCoreEvent`, `onDeviceJoined`, `onDeviceRemoving`, `onDeviceRemoved` |
| `IOT_ContractApply_t` | One value for one element, passed to `apply`: `eid`, `elemId`, `attrId`, `value`, `valueLen`, `source` |
| `IOT_ContractSource_t` | Who caused a change: `IOT_CONTRACT_SOURCE_CLOUD`, `_APP_LOCAL`, `_AUTOMATION`, `_SCHEDULE`, `_VOICE`, `_DEVICE`, `_OTHER`, `_RESTORE` |
| `IOT_ContractRestorePolicy_t` | Boot restore: `IOT_CONTRACT_RESTORE_LAST` (default), `_OFF`, `_NONE` |
| `IOT_ContractEventType_t` | Which contract event (the `IOT_CONTRACT_EVT_*` values below) |
| `IOT_ContractEvent_t` | One contract event: `type` and a `data` union with one member per type |
| `IOT_ContractCause_t` | Which path committed a change: `IOT_CONTRACT_CAUSE_STATE_COMMAND`, `_LOCAL_CHANGE`, `_SUBDEVICE_REPORT`, `_ATTRIBUTE_SET` |
| `IOT_ContractStatus_t` | What a status LED shows: `IOT_CONTRACT_STATUS_UNPROVISIONED`, `_PAIRING`, `_NO_WIFI`, `_NO_CLOUD`, `_ONLINE`, `_UPDATING` |
| `IOT_ContractEventFn` | A subscriber: `void fn(const IOT_ContractEvent_t *event, void *arg)` |
| `IOT_ContractDeviceStatusFn` | The status hook: `void fn(IOT_CoreDeviceStatusData_t *status)`, called with the filled status before it is sent |

### Contract events

| Event | `data` member | Raised when |
|---|---|---|
| `IOT_CONTRACT_EVT_STATE_COMMITTED` | `stateCommitted` (`eid`, `elemIds`, `numElems`, `attrId`, `value`, `valueLen`, `source`, `cause`) | A device's new state was stored, recorded, reported and given to Smart; once per device per change |
| `IOT_CONTRACT_EVT_AUTOMATION_FIRED` | `automationFired` (`smid`) | This device ran an automation's actions (delayed ones are scheduled by then) |
| `IOT_CONTRACT_EVT_RECORD_BLOCK_CLOSED` | `recordBlockClosed` (`eid`, `elemId`, `attrId`) | A history block closed and a new one is open |
| `IOT_CONTRACT_EVT_RECORD_PUSHED` | `recordPushed` (`eid`, `elemId`, `attrId`, `blockCount`) | Closed history blocks were sent to the cloud |
| `IOT_CONTRACT_EVT_DEVICE_ADDED` | `deviceAdded` (`eid`) | A sub-device finished joining |
| `IOT_CONTRACT_EVT_DEVICE_REMOVED` | `deviceRemoved` (`eid`, `root`) | A device was removed from every store; before `onDeviceRemoved` |
| `IOT_CONTRACT_EVT_MIRROR_REFUSED` | `mirrorRefused` (`eid`, `elemId`) | A mirrored on/off command named an element with no pairing here; nothing applied |
| `IOT_CONTRACT_EVT_STATUS_CHANGED` | `statusChanged` (`status`) | The device status changed; also once when the start has started core |

### Constants

| Name | Meaning |
|---|---|
| `IOT_CONTRACT_VERSION_MAJOR`, `_MINOR`, `_PATCH`, `IOT_CONTRACT_VERSION_STRING` | Contract API version (`2.2.0`) |
| `IOT_CONTRACT_MAX_ELEMENTS` | Most elements a device can declare (16) |
| `IOT_CONTRACT_REPORT_FIELDS_MAX` | Most fields in one `IOT_ContractReportFields()` value (8) |
| `IOT_CONTRACT_EVENT_SUBSCRIBERS_MAX` | Most contract event subscribers (4) |

## IOT_ContractAttrs.h

[`include/IOT_ContractAttrs.h`](../include/IOT_ContractAttrs.h) — the attribute table: `IOT_ATTR_*`
ids, `IOT_DEVTYPE_*` element types, value constants.

| Name | What it is |
|---|---|
| `IOT_ContractRegisterAttrs(defs, count)` | Register your product's own attribute ids (`0xF700`-`0xFEFF`). Before start; all or nothing |
| `IOT_ContractAttrSize(attrId)` | Size in bytes of an attribute's value (2 per 16-bit field); unknown ids are one field |
| `IOT_ContractAttrIsKnown(attrId)` | Whether an id is in the standard table or registered |
| `IOT_ContractAttrDef_t` | One attribute of your own: `id`, `fieldCount`, `defaultValue`, `restore` |
| `IOT_CONTRACT_ATTR_ID_MIN`, `IOT_CONTRACT_ATTR_ID_MAX`, `IOT_CONTRACT_ATTR_FIELDS_MAX` | The id range you may register, and the widest value (8 fields) |
| `iot_attr_onoff_t` | `IOT_OFF`, `IOT_ON` |
| `iot_attr_openclose_action_t` | `IOT_CLOSE`, `IOT_OPEN`, `IOT_STOP`, `IOT_MOVING` |
| `iot_attr_lock_t` | `IOT_LOCK`, `IOT_UNLOCK` |
| `iot_attr_startstop_t` | `IOT_STOPPED`, `IOT_STARTED`, `IOT_RUNNING` |
| `iot_attr_mode_t` | Air conditioner mode: `IOT_AC_AUTO`, `_COOLING`, `_DRY`, `_HEATING`, `_FAN` |
| `iot_attr_fan_swing_t` | `IOT_SWING_AUTO`, `IOT_SWING_OFF` |
| `iot_attr_fan_speed_t` | `IOT_FAN_AUTO`, `_LOW`, `_NORMAL`, `_HIGH`, `_MAX`, `_CUSTOM` |
| `iot_attr_button_press_t` | `IOT_PRESS_SINGLE`, `IOT_PRESS_LONG`, `IOT_PRESS_DOUBLE` |
| `IOT_ATTR_*`, `IOT_DEVTYPE_*` | Standard attribute ids and element/device types (see the header) |

## IOT_ContractRecord.h

[`include/IOT_ContractRecord.h`](../include/IOT_ContractRecord.h) — what history is kept. Rules
and behaviour: [record.md](record.md).

| Name | What it is |
|---|---|
| `IOT_ContractRecordRegisterRule(rule)` | Register or replace a rule (device type, element type, attribute) |
| `IOT_ContractRecordClearRules()` | Clear every rule and element override |
| `IOT_ContractRecordSetElementEnabled(eid, elemId, attrId, enabled)` | Force or suppress history for one exact element attribute |
| `IOT_ContractRecordUseCustomStore(fn)` | Hand accepted entries to your own store instead of Record's. Before start |
| `IOT_ContractRecordLogValueWidth(deviceType, elemType, attrId)` | Bytes a custom store keeps per entry for this attribute |
| `IOT_ContractRecordEvent_t` | One change as Record evaluates it (passed to a filter and to a custom store) |
| `IOT_ContractRecordRule_t` | A rule: `deviceType`, `elemType`, `attrId`, `valueSize`, `logValueBytes`, `minIntervalSec`, `heartbeatSec`, `filter`, `userCtx` |
| `IOT_ContractRecordFilterFn` | Optional value filter on a rule: return true to record |
| `IOT_ContractRecordAppendOverrideFn` | A custom store's append routine |
| `IOT_CONTRACT_RECORD_USE_DEFAULT` | Either timing field: take the attribute's default (0) |
| `IOT_CONTRACT_RECORD_EVERY_CHANGE` | `minIntervalSec`: never hold a change (events) |
| `IOT_CONTRACT_RECORD_NO_HEARTBEAT` | `heartbeatSec`: never re-record an unchanged value |
| `IOT_CONTRACT_RECORD_DEFAULT_LOG_VALUE_BYTES`, `IOT_CONTRACT_RECORD_MAX_LOG_VALUE_BYTES` | Default (2) and widest (8) log value width |

## dao/IOT_DaoDeviceMgmt.h

[`include/dao/IOT_DaoDeviceMgmt.h`](../include/dao/IOT_DaoDeviceMgmt.h) — the Device store,
read-only. See [how-to/read-state.md](how-to/read-state.md).

| Name | What it is |
|---|---|
| `IOT_DaoDeviceGetIdentityInfo(eid, out)` | MAC and device id; free with `IOT_DaoDeviceFreeIdentityInfo()` |
| `IOT_DaoDeviceFreeIdentityInfo(info)` | Frees what `IOT_DaoDeviceGetIdentityInfo()` allocated |
| `IOT_DaoDeviceGetCommonInfo(eid, out)` | Root EID, protocol, network address, device type |
| `IOT_DaoDeviceGetDeviceElemsState(eid, out)` | Every element with its attributes and values (cloud's element ids); free with `IOT_DaoDeviceFreeElemsInfo()` |
| `IOT_DaoDeviceGetElementType(eid, elemId, out)` | One element's type (`IOT_DEVTYPE_*`) |
| `IOT_DaoDeviceFreeElemsInfo(info)` | Frees what `IOT_DaoDeviceGetDeviceElemsState()` allocated |
| `IOT_DaoDeviceGetMeshKey(eid, out, cap, outLen)` | The stored mesh device key blob, opaque |
| `IOT_DaoDeviceGetAllEids(outEids, outCount)` | Every device's EID; free the array |
| `IOT_DevIdentityInfo_t` | `macType`, `mac`, `macSize`, `devId` |
| `IOT_DevCommonInfo_t` | `rootEid`, `protocol`, `nwkAddr`, `deviceType` |
| `IOT_DevElemsStateInfo_t` | A raw element state buffer (`state`, `stateLen`) |
| `IOT_DaoDeviceElemsAttrsInfo_t` | `numOfElem`, `elemList` |
| `IOT_DaoDeviceStateElemState_t` | One element: `elemId`, `elemType`, `numOfAttr`, `attrList` |
| `IOT_DaoDeviceStateAttr_t` | One attribute: `attrId`, `attrSize`, `attrValue` |
| `IOT_DAO_DEVICE_ID_LEN`, `IOT_DAO_DEVICE_MESH_KEY_MAX_LEN` | Device id length (12); mesh key buffer size (18) |

## dao/IOT_DaoIRMgmt.h

[`include/dao/IOT_DaoIRMgmt.h`](../include/dao/IOT_DaoIRMgmt.h) — the IR store, read-only. See
[how-to/ir.md](how-to/ir.md).

| Name | What it is |
|---|---|
| `IOT_DaoIRGetProtocolCode(eid, dataName, outCode)` | A protocol-based IR code for one button |
| `IOT_DaoIRGetRawZipHeader(eid, outHeader, outLen)` | The shared rawzip header of an IR appliance; caller frees |
| `IOT_DaoIRGetRawZipData(eid, dataName, outData, outLen)` | Compressed raw IR data for one button; caller frees |
| `IOT_IR_HEADER_DATANAME` | The reserved `dataName` of the rawzip header |
