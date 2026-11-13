# iotPlatform

The platform interface `iotcore` runs on: OS abstraction, timers, logging, storage and cache,
network, MQTT, TCP server, mDNS, BLE and the optional driver interfaces (mesh, Zigbee
coordinator). `iotcore` and an SDK call these headers; an implementation for one chip or OS
(`implplatform/<target>/`) provides them. The ESP32 implementation ships as the worked reference.

**It is for** platform writers porting the stack to a new chip or OS, and for SDK writers who need
storage, timers or the OS abstraction. It depends on nothing above it: never on `iotcore` or an SDK.

## Documentation

| Read | When you want to |
|---|---|
| [docs/getting-started.md](docs/getting-started.md) | Port the stack to a new chip or OS: the order to bring things up, and how to know each step works |
| [docs/implementation-guide.md](docs/implementation-guide.md) | Implement every platform interface, with the ESP32 implementation as the reference; ends in a checklist |
| [docs/ble-driver.md](docs/ble-driver.md) | Implement the BLE driver vtable and register it |
| [docs/sdkconfig-reference.md](docs/sdkconfig-reference.md) | Know the ESP-IDF settings that are load-bearing, and what breaks if you change them |

## Headers

| Header | What it gives you |
|---|---|
| `include/IOT_Platform.h` | `IOT_InitPlatform()`, `IOT_InitPlatformLean()`, `IOT_DeinitPlatform()` |
| `include/IOT_Osal.h` | Tasks, queues, mutexes, critical sections, heap information, uptime |
| `include/IOT_TimerMgmt.h` | One-shot timers (milliseconds), time, time zone |
| `include/IOT_Log.h`, `include/IOT_ErrorManager.h` | Logging (`IOT_LOG*`) and the `iot_err_t` codes |
| `include/IOT_AppManager.h` | Firmware version, restart, the firmware-update download |
| `include/dao/` | Key-value storage (`IOT_DaoStorage.h`), the cache (`IOT_Cached.h`), storage init and statistics |
| `include/network/`, `include/IOT_MqttMgmt.h`, `include/IOT_TcpServer.h`, `include/IOT_MdnsMgmt.h` | WiFi and Ethernet, the cloud connection, the local network |
| `include/ble/` | The BLE façade and the driver vtable a BLE stack registers |
| `include/mesh/`, `include/IOT_ZigbeeCoordinator*.h`, `include/IOT_MdnsDriver.h` | Optional driver interfaces, registered only when the feature is present |
| `include/IOT_PlatformCapabilities.h` | Which optional services are actually working on this build |
