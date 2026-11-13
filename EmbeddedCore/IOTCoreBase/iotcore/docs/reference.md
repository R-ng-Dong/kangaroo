# Reference

Every public name in `iotcore/include/`, one table per header, one line each. The header
comment is the full contract for each name (arguments, return codes, blocking, task). Events
link to their section in [events.md](events.md). Every function and event a public header
declares is listed here, and a check in our build enforces it. `iotcore/include/README.md`
maps the headers by role.

## IOT_Core.h

`IOT_Core.h` — start and stop core, provisioning, WiFi and the cloud session, identity, boot counter, the work queue. Includes `IOT_CoreOta.h`.

| Function | What it does |
|---|---|
| `IOT_CoreBleDeinit()` | Tear BLE down and reclaim its heap, keeping core's BLE state consistent |
| `IOT_CoreChangeWifiResumeBle()` | Resume the WiFi-loss BLE fallback when a download ends without a restart |
| `IOT_CoreChangeWifiSuspendBle()` | Pause the WiFi-loss BLE fallback (before releasing BLE for a download) |
| `IOT_CoreCloudConnect()` | Start a cloud connect attempt (`DEV_EVENT_CLOUD_CONNECTING`, then CONNECTED or CONNECT_FAILED) |
| `IOT_CoreCloudDisconnect()` | End the cloud session; core does not reconnect by itself until asked or the network reconnects |
| `IOT_CoreConfigDumpSafe()` | Call back once per configuration key that is safe to display (no secrets) |
| `IOT_CoreConfigureChangeWifi()` | Configure the WiFi-loss BLE fallback (`IOT_CoreChangeWifiConfig_t`). Before `IOT_CoreInit()` |
| `IOT_CoreConnectStoredWifi()` | Join WiFi with stored credentials without the rest of the LAN bring-up |
| `IOT_CoreDeferWork()` | Run a handler on core's task; core frees `arg` after it runs (pass NULL or heap you hand over) |
| `IOT_CoreDeinit()` | Stop core |
| `IOT_CoreDispatchEvent()` | Raise an event to the registered handlers, on the calling task |
| `IOT_CoreFactoryReset()` | Wipe core's identity and raise `DEV_EVENT_FACTORY_RESET` on the calling task before returning |
| `IOT_CoreGetBootCounter()` | Read the boot counter your SDK keeps for its own recovery. Blocks |
| `IOT_CoreGetRootDeviceId()` | This device's 12-byte device id |
| `IOT_CoreGetRootEid()` | This device's EID; `IOT_ERR_DATA_NOT_FOUND` before provisioning |
| `IOT_CoreGetRootGroup()` | This device's group address from provisioning |
| `IOT_CoreGetRootNwkAddr()` | This device's mesh unicast address from provisioning |
| `IOT_CoreGetTimeZone()` | Read the persisted time zone |
| `IOT_CoreGetWorkStats()` | Counters of core's work queue (depth, drops, slowest handler, publish times). Any task |
| `IOT_CoreInit()` | Start core with the 16-character model id: normal boot (`DEV_EVENT_BOOTED` inside it) or update boot (`OS_EVENT_OTA_BOOT_STARTED`). Register callbacks first. Path B only |
| `IOT_CoreIsCloudConnected()` | Whether the cloud session is up and every subscription confirmed. Non-blocking |
| `IOT_CoreIsProvisioned()` | Whether the device is provisioned. Non-blocking |
| `IOT_CoreSetBootCounter()` | Write the boot counter. Blocks |
| `IOT_CoreSetBootCounterAsync()` | Write the boot counter without blocking; safe from a timer callback |
| `IOT_CoreSetProvisionMode()` | Choose how setup runs; only `IOT_PROVISION_BLE_LAN` is implemented |
| `IOT_CoreSetTimeZone()` | Set and persist the POSIX time zone used for local-time schedules |
| `IOT_CoreSetWifiCredentials()` | Set WiFi credentials and switch networks (for a console or tool); the result comes as network events |
| `IOT_CoreStartChangeWifiBle()` | Advertise for new WiFi credentials on a provisioned device |
| `IOT_CoreStartProvision()` | Enter setup (BLE advert and LAN); called by `IOT_CoreInit()` on an unprovisioned device |
| `IOT_CoreStopChangeWifiBle()` | Stop the change-WiFi advert |
| `IOT_CoreStopProvision()` | Leave setup |

| Type | What it is |
|---|---|
| `IOT_CoreChangeWifiConfig_t` | WiFi-loss BLE fallback settings (`IOT_CORE_CHANGE_WIFI_CONFIG_DEFAULT`) |
| `IOT_CoreConfigDumpCb_t` | Callback of `IOT_CoreConfigDumpSafe()` |
| `IOT_CoreWorkStats_t` | Work-queue counters from `IOT_CoreGetWorkStats()` |
| `IOT_ProvisionMode_t` | Setup mode: `IOT_PROVISION_BLE_LAN` (default), `_LAN_ONLY`, `_AP_MODE` (not implemented) |

## IOT_CoreEventHandler.h

`IOT_CoreEventHandler.h` — umbrella for inbound events: the category enum, `IOT_CoreEvent_t`, callback registration; includes `IOT_CoreEventTypes.h` and the eight category headers.

| Function | What it does |
|---|---|
| `IOT_DevCoreRegisterEventCb()` | Register your callback for one event category. Path B only; before `IOT_CoreInit()` |
| `IOT_DevCoreUnregisterEventCb()` | Remove the callback of one category |

| Type | What it is |
|---|---|
| `IOT_CoreEventCategory_t` | `IOT_EVENT_STATE` .. `IOT_EVENT_SERVICE`, no gaps; `IOT_EVENT_CATEGORY_LAST` is the last |
| `IOT_CoreEventCb_t` | Your callback: `void cb(void *arg, const IOT_CoreEvent_t *event)` |
| `IOT_CoreEvent_t` | One event: `category` and the `evt` union with one member per category |

## IOT_CoreEventTypes.h

`IOT_CoreEventTypes.h` — the vocabulary every event shares: sender types, group constants, the request context, element-state types.

| Type | What it is |
|---|---|
| `IOT_RequestContext_t` | Who asked and how to answer: pass it through unchanged; read only `senderFrom`. Its pointers are borrowed |
| `IOT_RequestProtocol_t` | Transport a request arrived on: BLE, TCP, MQTT |
| `devStateElemState_t` | One element: `elemId`, `elemType`, its attributes |
| `devStateElemsInfo_t` | A device's full state: every element |
| `elemStateAttr_t` | One attribute value: `attrId`, `attrSize`, `attrValue` |

## IOT_CoreEventState.h

`IOT_CoreEventState.h` — `IOT_EVENT_STATE`: state commands, state queries, history queries.

| Event | When |
|---|---|
| [`IOT_EVENT_TYPE_STATE_SET`](events.md#iot_event_type_state_set) | The cloud, the app, a voice assistant or a local automation sends a SET state command (e.g., turn on the light). |
| [`IOT_EVENT_TYPE_STATE_GET`](events.md#iot_event_type_state_get) | The cloud or the app asks for the state of one device. |
| [`IOT_EVENT_TYPE_STATE_SENSOR_LOG`](events.md#iot_event_type_state_sensor_log) | The app asks for one day of the legacy sensor history (a year and a day of the year for one attribute of one element). |
| [`IOT_EVENT_TYPE_STATE_RECORD`](events.md#iot_event_type_state_record) | The app asks for one block of Record history (the [Record rulebook](../../sdkcontract/docs/record.md) describes the checkpoint chain it walks). |

| Type | What it is |
|---|---|
| `IOT_CoreStateEvent_t` | This category's event: `type` and a `data` union |
| `IOT_EventStateGetRequest_t` | The data of one request event |
| `IOT_EventStateRecordRequest_t` | The data of one request event |
| `IOT_EventStateSensorLogRequest_t` | The data of one request event |
| `IOT_EventStateSetRequest_t` | The data of one request event |
| `IOT_EventTypeState_t` | The event types of this category |
| `IOT_SetTargetType_t` | What a SET addresses: `IOT_SET_TARGET_DEVICE`, `_LOCATION`, `_GROUP`, `_VOICE` |
| `dev_set_info_t` | A SET's elements, attribute id and value (`eid` unused) |
| `elem_set_t` | One element id in a SET |

## IOT_CoreEventDevice.h

`IOT_CoreEventDevice.h` — `IOT_EVENT_DEVICE`: boot, setup, network, cloud, joins, removals, groups, identify, status, Zigbee.

| Event | When |
|---|---|
| [`DEV_EVENT_IDENTIFICATION`](events.md#dev_event_identification) | The app or the cloud asked the device to show itself. |
| [`DEV_EVENT_PROVISION_CANCELLED`](events.md#dev_event_provision_started--_cancelled--_failed--_completed) | The four moments of a setup. A provisioned device never raises them. |
| [`DEV_EVENT_PROVISION_COMPLETED`](events.md#dev_event_provision_started--_cancelled--_failed--_completed) | The four moments of a setup. A provisioned device never raises them. |
| [`DEV_EVENT_BOOTED`](events.md#dev_event_booted) | First thing in every normal boot (not an update boot), before any setup or network event. |
| [`DEV_EVENT_NETWORK_CONNECTED`](events.md#dev_event_network_connecting--_connected--_disconnected--_connect_failed) | The four moments of the device's network link. |
| [`DEV_EVENT_NETWORK_DISCONNECTED`](events.md#dev_event_network_connecting--_connected--_disconnected--_connect_failed) | The four moments of the device's network link. |
| [`DEV_EVENT_CLOUD_CONNECTED`](events.md#dev_event_cloud_connecting--_connected--_disconnected--_connect_failed) | The four moments of the device's cloud session. |
| [`DEV_EVENT_CLOUD_DISCONNECTED`](events.md#dev_event_cloud_connecting--_connected--_disconnected--_connect_failed) | The four moments of the device's cloud session. |
| [`DEV_EVENT_NETWORK_CONNECTING`](events.md#dev_event_network_connecting--_connected--_disconnected--_connect_failed) | The four moments of the device's network link. |
| [`DEV_EVENT_CLOUD_CONNECTING`](events.md#dev_event_cloud_connecting--_connected--_disconnected--_connect_failed) | The four moments of the device's cloud session. |
| [`DEV_EVENT_DEVICE_STATUS_REQUEST`](events.md#dev_event_device_status_request) | The cloud or the app asks for this device's status, including a phone over BLE during setup. |
| [`DEV_EVENT_NEW_DEVICE_JOINED`](events.md#dev_event_new_device_joined) | A new device (a hub's sub-device) joins; the event is raised once per phase, in order. |
| [`DEV_EVENT_DEVICE_REMOVED`](events.md#dev_event_device_removed) | The cloud removed a device from this one: a sub-device, or this device itself (the root). |
| [`DEV_EVENT_GROUP_BIND`](events.md#dev_event_group_bind--dev_event_group_unbind) | The cloud binds a device (or some of its elements) to a group, or takes it out of one. |
| [`DEV_EVENT_GROUP_UNBIND`](events.md#dev_event_group_bind--dev_event_group_unbind) | The cloud binds a device (or some of its elements) to a group, or takes it out of one. |
| [`DEV_EVENT_DEVICE_ATTR_SET`](events.md#dev_event_device_attr_set) | The cloud sets an attribute directly on a device, outside a state command. |
| [`DEV_EVENT_ZIGBEE_DEVICE_JOINING`](events.md#dev_event_zigbee_device_joining--_interviewed--_unknown_model--_left) | A hub with a Zigbee coordinator (`CONFIG_IOT_ZIGBEE_ENABLED`) sees a Zigbee device join the network (JOINING, the interview still pending), finish its interview with a known model (INTERVIEWED) or an unknown one (UNKNOWN_MODEL), or leave the network (LEFT). |
| [`DEV_EVENT_ZIGBEE_DEVICE_INTERVIEWED`](events.md#dev_event_zigbee_device_joining--_interviewed--_unknown_model--_left) | A hub with a Zigbee coordinator (`CONFIG_IOT_ZIGBEE_ENABLED`) sees a Zigbee device join the network (JOINING, the interview still pending), finish its interview with a known model (INTERVIEWED) or an unknown one (UNKNOWN_MODEL), or leave the network (LEFT). |
| [`DEV_EVENT_ZIGBEE_DEVICE_UNKNOWN_MODEL`](events.md#dev_event_zigbee_device_joining--_interviewed--_unknown_model--_left) | A hub with a Zigbee coordinator (`CONFIG_IOT_ZIGBEE_ENABLED`) sees a Zigbee device join the network (JOINING, the interview still pending), finish its interview with a known model (INTERVIEWED) or an unknown one (UNKNOWN_MODEL), or leave the network (LEFT). |
| [`DEV_EVENT_ZIGBEE_DEVICE_LEFT`](events.md#dev_event_zigbee_device_joining--_interviewed--_unknown_model--_left) | A hub with a Zigbee coordinator (`CONFIG_IOT_ZIGBEE_ENABLED`) sees a Zigbee device join the network (JOINING, the interview still pending), finish its interview with a known model (INTERVIEWED) or an unknown one (UNKNOWN_MODEL), or leave the network (LEFT). |
| [`DEV_EVENT_FACTORY_RESET`](events.md#dev_event_factory_reset) | Your product called `IOT_CoreFactoryReset()`; never the cloud. |
| [`DEV_EVENT_WIFI_RECOVERY_STARTED`](events.md#dev_event_wifi_recovery_started--_ended) | The WiFi recovery advert went on the air (STARTED) and is off the air again (ENDED). |
| [`DEV_EVENT_WIFI_RECOVERY_ENDED`](events.md#dev_event_wifi_recovery_started--_ended) | The WiFi recovery advert went on the air (STARTED) and is off the air again (ENDED). |
| [`DEV_EVENT_NETWORK_CONNECT_FAILED`](events.md#dev_event_network_connecting--_connected--_disconnected--_connect_failed) | The four moments of the device's network link. |
| [`DEV_EVENT_CLOUD_CONNECT_FAILED`](events.md#dev_event_cloud_connecting--_connected--_disconnected--_connect_failed) | The four moments of the device's cloud session. |
| [`DEV_EVENT_PROVISION_STARTED`](events.md#dev_event_provision_started--_cancelled--_failed--_completed) | The four moments of a setup. A provisioned device never raises them. |
| [`DEV_EVENT_PROVISION_FAILED`](events.md#dev_event_provision_started--_cancelled--_failed--_completed) | The four moments of a setup. A provisioned device never raises them. |

| Type | What it is |
|---|---|
| `IOT_CoreDeviceEventAttrSetData_t` | The data of one event (see the event) |
| `IOT_CoreDeviceEventDeviceRemovedData_t` | The data of one event (see the event) |
| `IOT_CoreDeviceEventFactoryResetData_t` | The data of one event (see the event) |
| `IOT_CoreDeviceEventGroupBindData_t` | The data of one event (see the event) |
| `IOT_CoreDeviceEventGroupUnbindData_t` | The data of one event (see the event) |
| `IOT_CoreDeviceEventIdentificationData_t` | The data of one event (see the event) |
| `IOT_CoreDeviceEventNetworkConnectFailedData_t` | The data of one event (see the event) |
| `IOT_CoreDeviceEventNetworkConnectedData_t` | The data of one event (see the event) |
| `IOT_CoreDeviceEventNetworkDisconnectedData_t` | The data of one event (see the event) |
| `IOT_CoreDeviceEventNewDeviceJoinedData_t` | The data of one event (see the event) |
| `IOT_CoreDeviceEventStatusRequestData_t` | The data of one event (see the event) |
| `IOT_CoreDeviceEventZigbeeInterviewedData_t` | The data of one event (see the event) |
| `IOT_CoreDeviceEventZigbeeJoiningData_t` | The data of one event (see the event) |
| `IOT_CoreDeviceEventZigbeeLeftData_t` | The data of one event (see the event) |
| `IOT_CoreDeviceEvent_t` | This category's event: `type` and a `data` union |
| `IOT_CoreNetIface_t` | Network interface: `IOT_CORE_NET_IFACE_WIFI`, `_ETHERNET` |
| `IOT_DevJoinElmInfo_t` | One joining element: id, type, attribute ids (sizes come from the attribute table) |
| `IOT_DevJoinPhase_t` | Join phase: DEVICE_INFO, ELM_INFO, SYNC_COMPLETE |
| `IOT_EventTypeDevice_t` | The event types of this category |

## IOT_CoreEventMesh.h

`IOT_CoreEventMesh.h` — `IOT_EVENT_MESH`: mesh keys from the cloud.

| Event | When |
|---|---|
| [`MESH_EVENT_KEYS_RECEIVED`](events.md#mesh_event_keys_received) | The cloud delivers a mesh network or application key to this device. |

| Type | What it is |
|---|---|
| `IOT_CoreMeshEventKeysData_t` | The data of one event (see the event) |
| `IOT_CoreMeshEvent_t` | This category's event: `type` and a `data` union |
| `IOT_EventTypeMesh_t` | The event types of this category |
| `IOT_MeshKeyKind_t` | `IOT_MESH_KEY_NETWORK` or `IOT_MESH_KEY_APPLICATION` |

## IOT_CoreEventSetting.h

`IOT_CoreEventSetting.h` — `IOT_EVENT_SETTING`: new WiFi credentials.

| Event | When |
|---|---|
| [`SETTING_EVENT_WIFI_CHANGED`](events.md#setting_event_wifi_changed) | The cloud sent new WiFi credentials and the device stored them and joined the new network. |

| Type | What it is |
|---|---|
| `IOT_CoreSettingEvent_t` | This category's event: `type` and a `data` union |
| `IOT_CoreSettingWifiChangedData_t` | The data of one event (see the event) |
| `IOT_EventTypeSetting_t` | The event types of this category |

## IOT_CoreEventSmart.h

`IOT_CoreEventSmart.h` — `IOT_EVENT_SMART`: automations, triggers, schedules, sync-on-off, condition groups.

| Event | When |
|---|---|
| [`SMART_EVENT_ACTIVE_BY_USER`](events.md#smart_event_active_by_user) | The cloud or the app runs an automation now (a broadcast). |
| [`SMART_EVENT_ACTIVE_BY_TRIGGER`](events.md#smart_event_active_by_trigger) | The cloud forwards a trigger that fired on another device (a broadcast). |
| [`SMART_EVENT_ACTIVE_BY_SCHEDULE`](events.md#smart_event_active_by_schedule) | The cloud runs a schedule slot (a broadcast); your own scheduler's tick reaches the same code through your own call. |
| [`SMART_EVENT_REMOVE_ANNOUNCE`](events.md#smart_event_remove_announce) | The cloud deletes a whole automation (a broadcast). |
| [`SMART_EVENT_SCHEDULE_REMOVE_ANNOUNCE`](events.md#smart_event_schedule_update_announce--smart_event_schedule_remove_announce) | The cloud sends the schedule times of an automation, or removes its schedule (broadcasts). |
| [`SMART_EVENT_TRIGGER_UPDATE_ANNOUNCE`](events.md#smart_event_trigger_update_announce) | The cloud announces a trigger's key so devices can check their copy (a broadcast). |
| [`SMART_EVENT_SCHEDULE_UPDATE_ANNOUNCE`](events.md#smart_event_schedule_update_announce--smart_event_schedule_remove_announce) | The cloud sends the schedule times of an automation, or removes its schedule (broadcasts). |
| [`SMART_EVENT_TRIGGER_MODE`](events.md#smart_event_trigger_mode--smart_event_enable_disable) | The cloud enables or disables a trigger, a schedule, or every automation of a device (TRIGGER_MODE), or enables or disables one automation by SMID (ENABLE_DISABLE). |
| [`SMART_EVENT_ENABLE_DISABLE`](events.md#smart_event_trigger_mode--smart_event_enable_disable) | The cloud enables or disables a trigger, a schedule, or every automation of a device (TRIGGER_MODE), or enables or disables one automation by SMID (ENABLE_DISABLE). |
| [`SMART_EVENT_BIND_TRIGGER`](events.md#smart_event_bind_trigger--smart_event_unbound_trigger) | The cloud creates or updates a trigger rule on this device, or removes one. |
| [`SMART_EVENT_UNBOUND_TRIGGER`](events.md#smart_event_bind_trigger--smart_event_unbound_trigger) | The cloud creates or updates a trigger rule on this device, or removes one. |
| [`SMART_EVENT_BIND_CMD`](events.md#smart_event_bind_cmd--smart_event_unbound_cmd) | The cloud binds the commands an automation runs on this device, or removes this device's commands from it. |
| [`SMART_EVENT_UNBOUND_CMD`](events.md#smart_event_bind_cmd--smart_event_unbound_cmd) | The cloud binds the commands an automation runs on this device, or removes this device's commands from it. |
| [`SMART_EVENT_CHECK_SYNC`](events.md#smart_event_check_sync) | The cloud asks whether this device holds the trigger of an SMID. |
| [`SMART_EVENT_ACTIVE_BY_SYNC_ON_OFF`](events.md#smart_event_active_by_sync_on_off) | A device of a sync-on-off pairing (sub-type 71) broadcasts its new ON/OFF value to the location. |
| [`SMART_EVENT_REPORT_COND_STATE`](events.md#smart_event_report_cond_state) | A peer device reports its own seat of a condition group (AND/OR, sub-type 2048): its bit, its config version, N and K. |
| [`SMART_EVENT_SYNC_COND_REQUEST`](events.md#smart_event_sync_cond_request) | A peer that rebooted asks the devices of its condition groups for their state, announcing its own at the same time. |
| [`SMART_EVENT_COND_NOT_MEMBER`](events.md#smart_event_cond_not_member) | A peer answered your sync request with "I hold no trigger for this SMID". |

| Type | What it is |
|---|---|
| `IOT_CoreSmartBlockAttrValueReverseDelayElm_t` | One bound command: value, delay, reverse time, element |
| `IOT_CoreSmartBlockAttrValue_t` | An attribute id and value inside a Smart event |
| `IOT_CoreSmartBlockTimeConfig_t` | A trigger's time configuration (opaque: copy it into the trigger report) |
| `IOT_CoreSmartBlockTriggerKey_t` | A trigger's key (opaque: copy it into the trigger report) |
| `IOT_CoreSmartCondStatus_t` | One condition-group seat as a peer reported it |
| `IOT_CoreSmartDataBlockTriggerElmInfo_t` | What a trigger watches: SMID, device, element, condition, sub-type |
| `IOT_CoreSmartEventActiveByScheduleData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEventActiveBySyncOnOffData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEventActiveByTriggerData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEventActiveByUserData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEventBindCmdData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEventBindTriggerData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEventCheckSyncData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEventCondNotMemberData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEventEnableDisableData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEventRemoveAnnounceData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEventScheduleRemoveAnnounceData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEventScheduleUpdateAnnounceData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEventSyncCondRequestData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEventTriggerModeData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEventTriggerUpdateAnnounceData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEventUnboundCmdData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEventUnboundTriggerData_t` | The data of one event (see the event) |
| `IOT_CoreSmartEvent_t` | This category's event: `type` and a `data` union |
| `IOT_CoreSmartTriggerModeType_t` | What a trigger mode addresses: a trigger, a schedule, a device's automations, all automations |
| `IOT_EventTypeSmart_t` | The event types of this category |

## IOT_CoreEventOs.h

`IOT_CoreEventOs.h` — `IOT_EVENT_OS`: reboot and firmware-update requests, the update boot.

| Event | When |
|---|---|
| [`OS_EVENT_REBOOT_REQUESTED`](events.md#os_event_reboot_requested) | The cloud asks this device to restart. |
| [`OS_EVENT_OTA_REQUESTED`](events.md#os_event_ota_requested) | The cloud asks for a firmware update. |
| [`OS_EVENT_OTA_BOOT_STARTED`](events.md#os_event_ota_boot_started) | This boot exists only to run an update you claimed with `IOT_CORE_OTA_CLAIM_AFTER_REBOOT`. |

| Type | What it is |
|---|---|
| `IOT_CoreOsEventOtaBootStartedData_t` | The data of one event (see the event) |
| `IOT_CoreOsEventOtaRequestedData_t` | The data of one event (see the event) |
| `IOT_CoreOsEventRebootRequestedData_t` | The data of one event (see the event) |
| `IOT_CoreOsEventType_t` | The event types of this category |
| `IOT_CoreOsEvent_t` | This category's event: `type` and a `data` union |
| `IOT_CoreOtaClaim_t` | Your claim of an update: `IOT_CORE_OTA_CLAIM_NONE`, `_NOW`, `_AFTER_REBOOT` |

## IOT_CoreEventIr.h

`IOT_CoreEventIr.h` — `IOT_EVENT_IR`: IR send, learn, keysets, capability.

| Event | When |
|---|---|
| [`IOT_IR_EVENT_EXEC_RAW`](events.md#iot_ir_event_exec_raw--_raw_zip--_prtc--_ac) | The cloud or the app tells the device to transmit an IR code: raw timing pairs (EXEC_RAW), compressed timings (EXEC_RAW_ZIP), a protocol id with its code bytes (EXEC_PRTC), or a differential air-conditioner state (EXEC_AC). |
| [`IOT_IR_EVENT_EXEC_RAW_ZIP`](events.md#iot_ir_event_exec_raw--_raw_zip--_prtc--_ac) | The cloud or the app tells the device to transmit an IR code: raw timing pairs (EXEC_RAW), compressed timings (EXEC_RAW_ZIP), a protocol id with its code bytes (EXEC_PRTC), or a differential air-conditioner state (EXEC_AC). |
| [`IOT_IR_EVENT_EXEC_PRTC`](events.md#iot_ir_event_exec_raw--_raw_zip--_prtc--_ac) | The cloud or the app tells the device to transmit an IR code: raw timing pairs (EXEC_RAW), compressed timings (EXEC_RAW_ZIP), a protocol id with its code bytes (EXEC_PRTC), or a differential air-conditioner state (EXEC_AC). |
| [`IOT_IR_EVENT_QUERY_CAPABILITY`](events.md#iot_ir_event_query_capability) | The cloud asks what IR this device can do. |
| [`IOT_IR_EVENT_STORE_KEYSET`](events.md#iot_ir_event_store_keyset) | The cloud sends the packed keyset (the learned keys) for an IR appliance. |
| [`IOT_IR_EVENT_EXEC_AC`](events.md#iot_ir_event_exec_raw--_raw_zip--_prtc--_ac) | The cloud or the app tells the device to transmit an IR code: raw timing pairs (EXEC_RAW), compressed timings (EXEC_RAW_ZIP), a protocol id with its code bytes (EXEC_PRTC), or a differential air-conditioner state (EXEC_AC). |
| [`IOT_IR_EVENT_LEARN_START`](events.md#iot_ir_event_learn_start) | The app asks the device to open a learn / detect window and listen for a remote. |

| Type | What it is |
|---|---|
| `IOT_CoreIrEvent_t` | This category's event: `type` and a `data` union |
| `IOT_EventTypeIr_t` | The event types of this category |

## IOT_CoreEventService.h

`IOT_CoreEventService.h` — `IOT_EVENT_SERVICE`: messages from your own backend.

| Event | When |
|---|---|
| [`IOT_SERVICE_EVENT_MSG`](events.md#iot_service_event_msg) | Your own backend sent this device a service message. |

| Type | What it is |
|---|---|
| `IOT_CoreServiceEvent_t` | This category's event: `type` and a `data` union |
| `IOT_EventTypeService_t` | The event types of this category |

## IOT_CoreReporter.h

`IOT_CoreReporter.h` — umbrella for outbound calls: declares nothing, includes `IOT_CoreState.h`, `IOT_CoreSmart.h`, `IOT_CoreSensorLog.h` and `IOT_CoreIr.h`.

## IOT_CoreState.h

`IOT_CoreState.h` — state reports and answers, the device status, device command answers.

| Function | What it does |
|---|---|
| `IOT_CoreRequestReportDeviceStatus()` | Send the device status, unsolicited (after a cloud connect) |
| `IOT_CoreRequestReportState()` | Answer a state query (`IOT_EVENT_TYPE_STATE_GET`), before you return |
| `IOT_CoreRequestReportStateChanged()` | Report a state change of one or more devices to the cloud and the app |
| `IOT_CoreRespondDeviceCmd()` | Answer a join phase, removal, group bind/unbind or attribute set after the store work |
| `IOT_CoreRespondDeviceStatus()` | Answer `DEV_EVENT_DEVICE_STATUS_REQUEST` to whoever asked; write its return into `result` |

| Type | What it is |
|---|---|
| `IOT_CoreDeviceCmdResponse_t` | Argument of `IOT_CoreRespondDeviceCmd()` |
| `IOT_CoreDeviceStatusData_t` | The device status: version, uptime, `protocolFlags`, memory |
| `IOT_CoreStateChangeDeviceData_t` | One device in a state report: identity, full state, changed elements and attribute |
| `IOT_CoreStateChangeReport_t` | Argument of `IOT_CoreRequestReportStateChanged()`: devices and `ctx` |
| `IOT_CoreStateResponse_t` | Argument of `IOT_CoreRequestReportState()` |
| `IOT_DeviceCmdType_t` | Which device command an answer is for (`IOT_DEVICE_CMD_*`) |

## IOT_CoreSmart.h

`IOT_CoreSmart.h` — automation reports, sync-on-off, condition groups, Smart command answers.

| Function | What it does |
|---|---|
| `IOT_CoreRequestSmartCondNotMember()` | Tell an asking peer this device holds no seat in a group |
| `IOT_CoreRequestSmartReportCondState()` | Broadcast this device's seat of a condition group |
| `IOT_CoreRequestSmartSyncCondPush()` | Announce your condition-group seats after a bind |
| `IOT_CoreRequestSmartSyncCondRequest()` | After a reboot, ask the location for the state of your condition groups |
| `IOT_CoreRequestSmartSyncCondResponse()` | Answer a peer's condition-group sync request |
| `IOT_CoreRequestSmartSyncOnOffReport()` | Broadcast a sync-on-off value to the location |
| `IOT_CoreRequestSmartTriggerReport()` | Tell the cloud a local trigger fired, so it can run the automation on other devices |
| `IOT_CoreRespondSmartCheckSync()` | Answer `SMART_EVENT_CHECK_SYNC` |
| `IOT_CoreRespondSmartCmd()` | Answer a Smart bind, unbind, trigger mode or enable/disable after the store work |
| `IOT_CoreSmartStairSwitchMirror()` | Legacy stair-switch mirror; a stub returning `IOT_ERR_NOT_SUPPORTED` in the shipped library |

| Type | What it is |
|---|---|
| `IOT_CoreSmartCheckSyncResponse_t` | Argument of `IOT_CoreRespondSmartCheckSync()` |
| `IOT_CoreSmartCmdResponse_t` | Argument of `IOT_CoreRespondSmartCmd()` |
| `IOT_CoreSmartReportCondState_t` | One condition-group seat to broadcast |
| `IOT_CoreSmartSyncCondRequest_t` | Argument of `IOT_CoreRequestSmartSyncCondRequest()` |
| `IOT_CoreSmartSyncOnOffReport_t` | Argument of `IOT_CoreRequestSmartSyncOnOffReport()` |
| `IOT_CoreSmartTriggerReport_t` | Argument of `IOT_CoreRequestSmartTriggerReport()` |
| `IOT_SmartCmdType_t` | Which Smart command an answer is for (`IOT_SMART_CMD_*`) |

## IOT_CoreRecord.h

`IOT_CoreRecord.h` — history (Record) answers and pushes; the format is in the [Record rulebook](../../sdkcontract/docs/record.md#the-core-record-api).

| Function | What it does |
|---|---|
| `IOT_CoreReportRecordBlocks()` | Push 1 to `IOT_CORE_RECORD_PUSH_BLOCKS_MAX` closed history blocks of one series. Blocks; never from a timer callback |
| `IOT_CoreRequestRecordResponse()` | Answer a history query (`IOT_EVENT_TYPE_STATE_RECORD`), synchronously |

| Type | What it is |
|---|---|
| `IOT_CoreRecordBlock_t` | One history block, as fields |
| `IOT_CoreRecordPush_t` | Argument of `IOT_CoreReportRecordBlocks()`: blocks of one series, oldest first |
| `IOT_CoreRecordResponse_t` | Argument of `IOT_CoreRequestRecordResponse()`: blocks, newest first |
| `IOT_CoreRecordStateBlock_t` | A block plus its identity, attached to a state answer |

## IOT_CoreSensorLog.h

`IOT_CoreSensorLog.h` — the legacy day-keyed history answer; not in the shipped library.

| Function | What it does |
|---|---|
| `IOT_CoreRequestSensorLogResponse()` | Legacy history answer; sends nothing and returns `IOT_ERR_NOT_SUPPORTED` in the shipped library |

| Type | What it is |
|---|---|
| `IOT_CoreSensorLogResponse_t` | Argument of `IOT_CoreRequestSensorLogResponse()` |

## IOT_CoreIr.h

`IOT_CoreIr.h` — IR: ask for a keyset, report a learn result.

| Function | What it does |
|---|---|
| `IOT_CoreReportIrDetected()` | Report what a learn window captured, with your deep copy of the request's `ctx` |
| `IOT_CoreReportIrLearnFailed()` | Report that a learn window ended with no usable capture |
| `IOT_CoreRequestIrData()` | Ask the cloud to push a sub-device's IR keyset |

| Type | What it is |
|---|---|
| `IOT_CoreIrDetectResult_t` | What a learn window captured |

## IOT_CoreOta.h

`IOT_CoreOta.h` — the firmware update core holds for you, and the progress callback.

| Function | What it does |
|---|---|
| `IOT_CoreOtaClearPending()` | Decline the pending update and tell the cloud (deferred if offline) |
| `IOT_CoreOtaDiscardPending()` | Drop the pending update without telling the cloud; for a start that failed |
| `IOT_CoreOtaFreePending()` | Free the buffers of a request from `IOT_CoreOtaGetPending()` |
| `IOT_CoreOtaGetAttempts()` | Update-boot attempts already spent on the pending request |
| `IOT_CoreOtaGetPending()` | Read the update request core is holding (free with `IOT_CoreOtaFreePending()`) |
| `IOT_CoreOtaIsPending()` | Whether a firmware download is still owed |
| `IOT_CoreOtaPlatformProgressCb()` | Progress callback for the platform downloader; sends every progress and result message for you |
| `IOT_CoreOtaRecordAttempt()` | Count one more update-boot attempt |

| Type | What it is |
|---|---|
| `IOT_CoreOtaRequest_t` | The update request core holds: URL, token, certificate |

## IOT_CoreServiceMsg.h

`IOT_CoreServiceMsg.h` — publish to your own backend.

| Function | What it does |
|---|---|
| `IOT_CoreServiceMsgPublish()` | Publish a payload on one of your service channels. Blocks up to about ten seconds; dropped when offline |

## IOT_CoreBleAdv.h

`IOT_CoreBleAdv.h` — your own fields and name in core's BLE adverts. Walkthrough:
[how-to/ble-advertising.md](how-to/ble-advertising.md).

| Function | What it does |
|---|---|
| `IOT_CoreBleAdvAddField()` | Add one AD field to the advert being built; only from the customizer. Refuses fields core owns |
| `IOT_CoreBleAdvOmitName()` | Drop core's local name from the scan response to free its space |
| `IOT_CoreBleAdvRefresh()` | Re-run the customizer now and restart the advert that is on air |
| `IOT_CoreBleAdvSetCustomizer()` | Register (or, with NULL, remove) the callback that adds your fields at every advert start |

| Type | What it is |
|---|---|
| `IOT_CoreBleAdvPurpose_t` | Which advert is starting: set-up or change-WiFi |
| `IOT_CoreBleAdvWhere_t` | Which payload a field goes in: the packet or the scan response |
| `IOT_CoreBleAdvContext_t` | What the callback is told: purpose, free space in each payload, whether the name is required |
| `IOT_CoreBleAdvFields_t` | The fields being collected for one advert; opaque, valid only inside the callback |
| `IOT_CoreBleAdvCustomizeCb_t` | The callback type that adds your fields |

## IOT_CoreTimeRequest.h

`IOT_CoreTimeRequest.h` — whether a state query asked for the time.

| Function | What it does |
|---|---|
| `IOT_CoreTimeRequested()` | True when a state query's `unixTime` is a real request for the time |

## IOT_CoreHubRole.h

`IOT_CoreHubRole.h` — the Zigbee hub role; includes `IOT_CoreHubRoleFlags.h`.

| Function | What it does |
|---|---|
| `IOT_CoreHubZigbeeIsActive()` | Whether the Zigbee hub role is running |
| `IOT_CoreHubZigbeeStart()` | Start the Zigbee coordinator hub role; `IOT_ERR_NOT_SUPPORTED` with no coordinator driver |
| `IOT_CoreHubZigbeeStop()` | Stop the Zigbee hub role |

## IOT_CoreHubRoleFlags.h

`IOT_CoreHubRoleFlags.h` — which hub roles this device declares.

| Function | What it does |
|---|---|
| `IOT_CoreHasHubRole()` | Whether a hub role is declared |
| `IOT_CoreSetHubRole()` | Declare (or withdraw) a hub role; declare when you commit to it, not once the transport is up |

| Type | What it is |
|---|---|
| `IOT_HubRole_t` | `IOT_HUB_ROLE_MESH`, `IOT_HUB_ROLE_ZIGBEE` |

## IOT_CoreHeapProbe.h

`IOT_CoreHeapProbe.h` — heap sampling for diagnosis; compiled out unless enabled, leave it off in shipped firmware.

| Function | What it does |
|---|---|
| `IOT_HeapProbeFormatDelta()` | Format the change between two samples |
| `IOT_HeapProbeLogDelta()` | Log the change since a sample |
| `IOT_HeapProbeSample()` | Take a heap sample (free, largest block, holes) |

| Type | What it is |
|---|---|
| `IOT_HeapProbeSample_t` | One heap sample |

## IOT_ProvisionTypes.h

`IOT_ProvisionTypes.h` — deprecated: forwards to `IOT_Core.h`, which declares `IOT_ProvisionMode_t`. Declares nothing itself.
