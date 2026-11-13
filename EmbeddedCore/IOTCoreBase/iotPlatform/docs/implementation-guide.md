# Tài liệu hướng dẫn implement IoT Platform APIs

## Tổng quan

Tài liệu này hướng dẫn chi tiết cách implement tất cả Platform APIs cho các platform khác nhau (ESP32, nRF52, STM32, Linux,...). File examples từ `implplatform/esp32/` được sử dụng làm tham khảo.

**Platform Abstraction Layers**:
- **Core Utilities**: OS abstraction (mutexes, heap), Timer, Log, Error manager
- **Network**: WiFi, Ethernet, Network Management
- **Transport**: MQTT, TCP Server, mDNS
- **Storage (DAO)**: Config, Device, Group, Smart data persistence
- **Peripheral**: GPIO, I/O Management
- **BLE**: Bluetooth Low Energy

> **C++ callers.** Every header in `iotPlatform/include/` wraps its declarations in an
> `#ifdef __cplusplus` / `extern "C"` block, so a C++ translation unit links against the C
> symbols without wrappers of your own. If you write your own platform headers, do the same,
> and never use `namespace`, `class`, `new`, `template` or `this` as an identifier — `extern
> "C"` fixes linkage, not syntax. (This is why `IOT_CacheConfig_t.namespace` became
> `.nsName`; see §4.3.)

> **Removed: `IOT_DeviceConfiguration.h`.** It mapped `CONFIG_IDF_TARGET_*` to
> `CONFIG_DEVICE_SUPPORT_BLE` / `CONFIG_DEVICE_SUPPORT_INTERNAL_EMAC_ETHERNET`, nothing read
> either macro, and on ESP32-C3 (and C2, H2, P4) neither was ever defined. Delete any include
> of it and test capabilities with ESP-IDF's own `CONFIG_IDF_TARGET_*` / `CONFIG_BT_*`
> symbols. Note that `#ifdef CONFIG_DEVICE_SUPPORT_BLE` was already taking the "no BLE" branch
> on a C3.

---

## Kiến trúc Tổng thể

```
┌────────────────────────────────────────────────┐
│          Application / IoT Core                │
└────────────────┬───────────────────────────────┘
                 │ Platform-Independent APIs
┌────────────────▼───────────────────────────────┐
│        IoT Platform Abstraction Layer          │
│  (iotPlatform/include/*.h)                     │
├────────────────────────────────────────────────┤
│ • Core: OSAL, Timer, Log                       │
│ • Network: WiFi, Ethernet, NetworkMgmt         │
│ • Transport: MQTT, TCP, mDNS                   │
│ • Storage: DAO (Config, Device, Group, Smart)  │
│ • Peripheral: GPIO, IO                         │
│ • BLE: Bluetooth Management                    │
└────────────────┬───────────────────────────────┘
                 │ Platform-Specific Implementation
┌────────────────▼───────────────────────────────┐
│     Platform Implementation Layer              │
│  (implplatform/esp32/*.c)                      │
└────────────────┬───────────────────────────────┘
                 │
┌────────────────▼───────────────────────────────┐
│     Platform SDK / HAL / RTOS                  │
│  ESP-IDF / Nordic SDK / STM32Cube / POSIX      │
└────────────────────────────────────────────────┘
```

---

## 1. Core Utilities

### 1.1 Mutexes and timers (`IOT_Osal.h`, `IOT_TimerMgmt.h`)

Mutexes are part of the OS abstraction: implement `IOT_OsalMutexCreate()`,
`IOT_OsalMutexCreateRecursive()`, `IOT_OsalMutexDelete()`, `IOT_OsalMutexLock(mutex, timeout_ms)`,
`IOT_OsalMutexUnlock()` and the recursive lock/unlock pair, as declared in
`iotPlatform/include/IOT_Osal.h`. (An earlier, separate mutex wrapper header no longer exists.)

The timer service is `iotPlatform/include/IOT_TimerMgmt.h`. `IOT_TimerInsertCb()` takes
**milliseconds** and fires once; its callbacks run on a small timer stack, so they only re-arm,
set a flag or hand work to a task.

---

#### APIs Cần Implement

##### IOT_TimerInit()
```c
iot_err_t IOT_TimerInit(void);
```


**ESP32**: the reference implementation starts SNTP with up to three configured servers,
tried in order:

| Kconfig symbol | default |
|---|---|
| `CONFIG_IOT_NTP_SERVER_1` | `"216.239.35.0"` (an IP literal, on purpose) |
| `CONFIG_IOT_NTP_SERVER_2` | `"vn.pool.ntp.org"` |
| `CONFIG_IOT_NTP_SERVER_3` | `"pool.ntp.org"` |
| `CONFIG_IOT_NTP_USE_DHCP_SERVER` | `n` |

The defaults are compiled in with `#ifndef`, so a project that declares none of these
symbols builds and behaves as the table says. To change them, declare the symbols in your
own `main/Kconfig.projbuild`.

- **Keep an IP in slot 1.** DNS is not up yet when time sync starts, and lwIP only moves to
  the next server on timeout, so the first entry decides how fast the clock arrives. The
  clock gates TLS certificate validity, so a slow sync is a failed first cloud connection,
  not just a delay.
- **`CONFIG_IOT_NTP_USE_DHCP_SERVER` is opt-in** and also needs
  `CONFIG_LWIP_DHCP_GET_NTP_SRV=y`. When on, the DHCP-supplied server takes slot 0 and the
  configured ones shift down. On a network that does not actually supply one, SNTP waits
  on the empty slot first — enable it only on a site that serves NTP by DHCP and blocks
  outbound UDP/123.
- The list is fitted to `CONFIG_LWIP_SNTP_MAX_SERVERS`; with a smaller limit the later
  entries are simply not used.
- **No holes in the server list.** ESP-IDF `strdup`s every entry from 0 to
  `num_of_servers`, so a NULL in the middle is a crash at `IP_EVENT_STA_GOT_IP`, not an
  ignored slot. If you build your own list, keep it dense.
- **A failed SNTP init returns `IOT_ERR_FAIL`; it must not abort.** The device then runs on
  without a synced clock and time-gated features stay off — callers already check
  `IOT_TimerIsTimeSynced()`.

##### IOT_TimerGetCurrentTimeMs()
```c
uint32_t IOT_TimerGetCurrentTimeMs(void);
```

**ESP32**:
```c
uint32_t IOT_TimerGetCurrentTimeMs(void);
```

**Platform-Specific**:
- **nRF52**: `app_timer_cnt_get()` or `NRF_RTC0->COUNTER`
- **Linux**: `clock_gettime(CLOCK_MONOTONIC, &ts)`
- **STM32**: `HAL_GetTick()`

**Contract:** milliseconds since boot, truncated to 32 bits, so it wraps every ~49.7
days. Callers may use it only for durations (`end - start` survives one wrap); for an
absolute monotonic time they use `IOT_OsalGetUptimeUs()`. It must be safe to call before
the timer subsystem is initialised, so do not take a lock in it.

**If you implement the timer heap yourself, key pending timers on a 64-bit deadline.**
Storing `IOT_TimerGetCurrentTimeMs() + timeMs` as a 32-bit value and comparing it raw
breaks at the wrap: a timer armed shortly before 49.7 days of uptime does not fire for a
further ~49.7 days, and one armed across the wrap fires early. The shipped ESP32
implementation (`implplatform/esp32/timer_heap.c`) uses 64-bit uptime deadlines.

##### IOT_TimerDelayMs()
```c
void IOT_TimerDelayMs(uint32_t delayMs);
```

**ESP32**:
```c
void IOT_TimerDelayMs(uint32_t delayMs) {
    vTaskDelay(pdMS_TO_TICKS(delayMs));
}
```

##### IOT_TimerSetTimeZone()
```c
iot_err_t IOT_TimerSetTimeZone(const char *timeZone);
```

**ESP32**:
```c
iot_err_t IOT_TimerSetTimeZone(const char *timeZone) {
    if (timeZone == NULL) {
        return IOT_ERR_INVALID_ARG;
    }

    // Set timezone for localtime conversion
    setenv("TZ", timeZone, 1);
    tzset();

    return IOT_OK;
}
```

**Example timezones**: `"UTC-7"`, `"GMT+0"`, `"CST-8"`

##### IOT_TimerGetUnixTimeMs()
```c
iot_err_t IOT_TimerGetUnixTimeMs(uint8_t *outTimeBytes);
```

**Purpose**: Get Unix timestamp in milliseconds as 8-byte array (big-endian)

**ESP32**:
```c
iot_err_t IOT_TimerGetUnixTimeMs(uint8_t *outTimeBytes) {
    if (outTimeBytes == NULL) {
        return IOT_ERR_INVALID_ARG;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);

    uint64_t ms = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    // Convert to big-endian byte array
    for (int i = 0; i < 8; i++) {
        outTimeBytes[7 - i] = (ms >> (i * 8)) & 0xFF;
    }

    return IOT_OK;
}
```

---

### 1.2 IOT_AppManager - Application Lifecycle

**Header**: `iotPlatform/include/IOT_AppManager.h`
**ESP32 Implementation**: `implplatform/esp32/IOT_AppManager.c`

#### Purpose
Restart the chip, report the running firmware version, run a firmware download,
and tell the application why the chip started.

##### IOT_AppManagerGetResetReason()
```c
typedef enum {
    IOT_RESET_REASON_POWER_ON,  // cold start, or power physically removed
    IOT_RESET_REASON_SOFTWARE,  // deliberate restart — OTA, reboot command
    IOT_RESET_REASON_CRASH,     // panic or watchdog: the app died unplanned
    IOT_RESET_REASON_BROWNOUT,  // supply sagged. A power problem, not a data problem
    IOT_RESET_REASON_UNKNOWN,
} IOT_ResetReason_t;

iot_err_t IOT_AppManagerGetResetReason(IOT_ResetReason_t *out);
```

Never fails destructively — an unmappable reason is reported as
`IOT_RESET_REASON_UNKNOWN`.

The distinction that matters to a boot-loop guard is *unplanned death* versus
everything else. A user cutting power and a deliberate restart both look like a
reboot but say nothing about the firmware's health, so a guard that counts every
boot will eventually erase a healthy device's data because someone flicked a
switch eight times.

`BROWNOUT` is deliberately its own case rather than folded into `CRASH`: it means
the supply is failing, and no amount of erasing the device's data will fix a
supply.

**Platform Notes**:
- **ESP32**: map from `esp_reset_reason()` — `ESP_RST_PANIC` / `ESP_RST_TASK_WDT`
  / `ESP_RST_INT_WDT` / `ESP_RST_WDT` → `CRASH`, `ESP_RST_BROWNOUT` → `BROWNOUT`,
  `ESP_RST_SW` → `SOFTWARE`, `ESP_RST_POWERON` → `POWER_ON`
- Any platform without the distinction: return `IOT_RESET_REASON_UNKNOWN` rather
  than guessing — a wrong `CRASH` costs the user their configuration

##### IOT_AppManagerMarkAppValid()
```c
iot_err_t IOT_AppManagerMarkAppValid(void);
```

Confirms the running image works, cancelling any pending rollback. No-op on
platforms without rollback support.

> ⚠️ With `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` set, a freshly-updated image
> boots in a pending-verify state and the bootloader reverts to the previous slot
> unless the app confirms itself. **Enabling that option without calling this
> makes every OTA revert on its second boot.**

Call it once the device has demonstrably settled — after the cloud connection is
up and the app has run for a while — not at the top of `app_main()`, where you
would confirm an image that has not yet proven anything.

**Platform Notes**:
- **ESP32**: `esp_ota_mark_app_valid_cancel_rollback()`
- Bootloader rollback and a boot-loop guard solve *different* problems and never
  race: the bootloader owns bad firmware and reverts it on the second boot, while
  a guard owns bad persisted data — the one case a revert cannot help, because
  the data survives it.

---

### 1.3 IOT_Osal - Heap Information

**Header**: `iotPlatform/include/IOT_Osal.h`
**ESP32 Implementation**: `implplatform/esp32/IOT_Osal.c`

#### Mục đích
Đọc mức chiếm dụng heap hiện tại — bao gồm hai con số cho phép phân biệt
**phân mảnh (fragmentation)** với **cạn kiệt (exhaustion)**.

```c
typedef struct {
    uint32_t totalFreeBytes;   /* tổng số byte còn trống */
    uint32_t largestFreeBlock; /* khối liên tục lớn nhất có thể cấp phát */
    uint32_t minimumFreeBytes; /* mức thấp nhất của totalFreeBytes kể từ khi boot */
    uint32_t freeBlocks;       /* số lỗ trống trong heap */
    uint32_t allocatedBlocks;  /* số vùng đang được cấp phát */
} IOT_OsalHeapInfo_t;

iot_err_t IOT_OsalGetHeapInfo(IOT_OsalHeapInfo_t *outInfo);
```

`totalFreeBytes` có thể đứng yên trong khi `largestFreeBlock` sụt xuống — đó là
phân mảnh, và **`largestFreeBlock` mới là con số một phiên TLS thực sự cần**.
`freeBlocks` tăng lên cùng lúc `largestFreeBlock` giảm nghĩa là heap đang bị cắt
thành nhiều lỗ nhỏ hơn.

**Platform Notes**:
- **ESP32**: `heap_caps_get_info(&info, MALLOC_CAP_8BIT)`.
- Platform không có các bộ đếm này: trả về `IOT_ERR_NOT_SUPPORTED` và để nguyên
  các trường bằng 0 (ví dụ glibc trên Linux không cho biết khối trống lớn nhất).

---

### 1.4 IOT_Platform - Lean Init (tuỳ chọn)

**Header**: `iotPlatform/include/IOT_Platform.h`
**ESP32 Implementation**: `implplatform/esp32/IOT_Esp32Platform.c`

```c
iot_err_t IOT_InitPlatformLean(void);
```

Khởi tạo platform **không có BLE stack**: cùng driver, storage và network như
`IOT_InitPlatform()`, chỉ bỏ `IOT_BleInit()`. Dành cho một lần boot chỉ để chạy
OTA download — bỏ BLE để lại nhiều heap liên tục hơn hẳn so với ~16.7 KB mà một
phiên TLS download cần.

Thiết bị khởi tạo theo cách này **không thể** advertise, không thể được
provision, và không có đường lùi về BLE khi mất WiFi — nên nó phải reboot chứ
không được chạy tiếp.

> **TUỲ CHỌN, không phải mặc định.** `IOT_CoreInit()` hiện tự phát hiện boot này
> có phải OTA-only hay không (`IOT_CoreOtaIsPending()`) và tự giải phóng heap của
> BLE (`IOT_CoreBleDeinit()`) khi đúng là vậy. Vì thế lựa chọn bình thường cho
> mọi lần boot vẫn là `IOT_InitPlatform()`, kể cả trên phần cứng có nhận OTA.
> Chỉ dùng `IOT_InitPlatformLean()` nếu muốn headroom tối đa trên đường OTA và
> chấp nhận các ràng buộc trên cho **toàn bộ** lần boot, chứ không chỉ đoạn mà
> core giải phóng BLE.

---

### 1.5 IOT_Log - Log levels

**Header**: `iotPlatform/include/IOT_Log.h`
**ESP32 Implementation**: `implplatform/esp32/IoTLog.c`

**The default level is INFO** (it was DEBUG). On ESP32 every console line blocks the task
that logs it until the UART has sent it, and DEBUG output on the per-message path made a
control command take several times longer than its real work. To get DEBUG back:

```c
log_set_level(LOG_LEVEL_DEBUG);                 /* at runtime */
```

or add `-DLOG_DEFAULT_LEVEL=LOG_LEVEL_DEBUG` to your compile definitions —
`LOG_DEFAULT_LEVEL` is now wrapped in `#ifndef`. Keep anything you log once per command at
DEBUG, or it costs the same time again on every command.

The `LOG_TRACE()` … `LOG_FATAL()` macros now pass the level through the header's own
`IOT_LOG_LVL_*` macros instead of the bare `LOG_LEVEL_*` enum names. NimBLE's
`log_common.h` defines `LOG_LEVEL_DEBUG/INFO/WARN/ERROR` as macros one step lower than this
enum, so in any file that also reached a NimBLE header every line was logged one level
below what it said (an error arrived as a warning, an info line as debug) — silently, with
no compiler warning. `log_level_t` and `log_set_level()` are unchanged. **If you keep your
own copy of `IOT_Log.h`, take this change.**

---

### 1.6 IOT_ErrorManager - new codes

**Header**: `iotPlatform/include/IOT_ErrorManager.h`

| code | value | meaning |
|---|---|---|
| `IOT_ERR_BUSY` | `0x118` | the resource is in use; retry later (e.g. a WiFi scan already in flight — §2.1) |
| `IOT_ERR_NOT_PROVISIONED` | `0xe04` | the device has never been onboarded; wait for provisioning |

Both are additive; no existing code changed value.

---

### 1.7 Task stacks (ESP32 reference)

**ESP32 Implementation**: `implplatform/esp32/task_stacks.h`

The reference platform's task stacks are now sized against measured high-water marks, and
the sizes live together in one header. Three went down:

| task | was | now | measured peak |
|---|---|---|---|
| `timer` | 6144 | 4096 | 2364 |
| `dao_task` | 4096 | 3072 | 1272 |
| `cache_flush` | 4096 | 2048 | 504 |

Task stacks come out of the heap early and sit in the middle of it, so each reduction is
also a larger largest-free-block — the number a TLS handshake needs (§1.3).

**If you re-sized these yourself, re-check against the table rather than taking our
numbers.** A high-water mark is the worst case *seen*, not the worst case possible:

- `timer` runs your `IOT_TimerInsertCb()` callbacks, so its peak is set by **your** logic.
  The 2364 figure came from a device evaluating automations and schedules; a device with
  none configured shows a few hundred bytes, which says nothing about yours.
- Keep at least 1 KB over the peak you measure. A stack overflow on a sealed unit with no
  serial access is not recoverable by reflashing — if you cannot measure, leave these alone.

On ESP-IDF the `vTaskList()` / high-water-mark figures are in **bytes** (vanilla FreeRTOS
reports words), and they report the smallest free space ever seen: subtract from the
configured size to get the peak.

---

## 2. Network Management

### 2.1 IOT_NetworkMgmt - Unified Network Interface

**Header**: `iotPlatform/include/network/IOT_NetworkMgmt.h`
**ESP32 Implementation**: `implplatform/esp32/network/IOT_NetworkMgmt.c`

#### Mục đích
Quản lý WiFi và Ethernet interfaces thống nhất với event-driven architecture.

#### Key Structures

```c
typedef enum {
    NETWORK_TYPE_NONE = 0,  // End-of-array marker
    ETHERNET = 1,
    WIFI = 2,
    MOBILE_3G_4G = 6
} IOT_NetworkIfaceType;

typedef struct {
    IOT_NetworkIfaceType type;
    uint8_t number;  // Interface number (e.g., WiFi 0, Ethernet 0)
} IOT_NetworkIface;

typedef enum {
    IOT_NWK_EVENT_STATUS_NONE,
    IOT_NWK_EVENT_STATUS_CONNECTING,
    IOT_NWK_EVENT_STATUS_CONNECTED,     // Got IP
    IOT_NWK_EVENT_STATUS_DISCONNECTING,
    IOT_NWK_EVENT_STATUS_DISCONNECTED,
    IOT_NWK_EVENT_STATUS_LINKLOSS,
    IOT_NWK_EVENT_STATUS_RETRY_CONNECTING,
    IOT_NWK_EVENT_STATUS_MAX
} IOT_NwkEventStatus_t;
```

> ⚠️ **Interface arrays are terminated, not counted.** Every function that takes
> `IOT_NetworkIface *ifaces` (`IOT_NwkInit`, `IOT_NwkDeinit`, `IOT_NwkStop`,
> `IOT_NwkConnectWifi`, …) takes an **array** and walks it until the first entry whose
> `type` is `NETWORK_TYPE_NONE` — nothing else stops it. Pass a terminated array, never the
> address of a single struct:
>
> ```c
> IOT_NetworkIface ifaces[2] = {{.type = WIFI, .number = 0},
>                               {.type = NETWORK_TYPE_NONE, .number = 0}};
> IOT_NwkConnectWifi(ifaces, ssid, pwd, 5);
> ```
>
> Without the terminator the walk reads whatever stack bytes follow the struct; a byte that
> happens to read as `2` (`WIFI`) starts a second connect that tears down the first. The
> symptom is WiFi joins and provisioning that fail intermittently and differently on every
> build. **Implementations must also stop after `IOT_NWK_MAX_IFACES` (4) entries** — a
> backstop that caps the damage of a caller's mistake; it does not make an unterminated
> array correct.

#### APIs Cần Implement

##### IOT_NwkInit()
```c
iot_err_t IOT_NwkInit(IOT_NetworkIface *ifaces);
```

**Purpose**: Initialize network interfaces (WiFi, Ethernet, or both)

**ESP32**:
```c
iot_err_t IOT_NwkInit(IOT_NetworkIface *ifaces) {
    if (ifaces == NULL) {
        return IOT_ERR_INVALID_ARG;
    }

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize based on interface type
    for (uint8_t i = 0; i < IOT_NWK_MAX_IFACES && ifaces[i].type != NETWORK_TYPE_NONE; i++) {
        const IOT_NetworkIface *iface = &ifaces[i];
        switch (iface->type) {
            case WIFI:
                wifi_init();
                break;
            case ETHERNET:
                ethernet_init();
                break;
            default:
                return IOT_ERR_NOT_SUPPORTED;
        }
    }

    return IOT_OK;
}
```

##### IOT_NwkConnectWifi()
```c
iot_err_t IOT_NwkConnectWifi(
    IOT_NetworkIface *ifaces,
    char *ssid,
    char *pwd,
    uint8_t retryTimes
);
```

**ESP32**:
```c
iot_err_t IOT_NwkConnectWifi(
    IOT_NetworkIface *ifaces,
    char *ssid,
    char *pwd,
    uint8_t retryTimes);
```

Walk `ifaces` with the same bound as `IOT_NwkInit()`. If the connect fails part-way (for
example `esp_wifi_start()` fails), restore auto-reconnect to the state it was in before the
call — leaving it suppressed means the device can never recover from a later link loss for
the rest of the boot.

##### IOT_NwkScanWifiAsync()
```c
typedef void (*IOT_NwkScanDoneCb_t)(IOT_NetworkIface iface, iot_err_t err,
                                    wifi_scan_res_require_t *results, uint8_t count,
                                    void *arg);

iot_err_t IOT_NwkScanWifiAsync(IOT_NetworkIface iface, uint8_t interval, uint8_t maxResults,
                               IOT_NwkScanDoneCb_t cb, void *arg);
```

**New, and required** — core uses it for WiFi scans requested by the app. A scan takes
seconds; the blocking `IOT_NwkScanWifi()` (still declared and supported) holds its caller
for all of it, which stalled every queued cloud command behind a scan.

| parameter | meaning |
|---|---|
| `iface` | interface to scan (must be `WIFI`) |
| `interval` | scan dwell hint, seconds |
| `maxResults` | capacity hint; `0` = platform default |
| `cb` | result callback, required |
| `arg` | passed back to `cb` |

**Contract** — core relies on every line of it:

- Return `IOT_OK` only if the scan started; then `cb` is called **exactly once**.
- Return `IOT_ERR_BUSY` if a scan is already in flight, or another `IOT_ERR_*` if it could
  not start. In both cases `cb` is **not** called.
- `cb` runs on the **platform's** own task (on ESP32, the WiFi event task), never the
  caller's. A platform with no asynchronous scan may call `cb` **before**
  `IOT_NwkScanWifiAsync()` returns; callers tolerate both orders.
- **Ownership of `results`, and of every `results[i].ssid`, passes to the callback**, which
  frees them on every path — including `err != IOT_OK`, where `results` is `NULL` and
  `count` is 0.
- The callback body must be short and non-blocking; building and sending protocol messages
  from it is not safe.

**ESP32**: `IOT_WifiScanAsync()` (`network/wifi.h`) starts the scan and fires its
`iot_wifi_scan_done_cb_t` on `WIFI_EVENT_SCAN_DONE`; `IOT_NwkScanWifiAsync()` adapts that to
`IOT_NwkScanDoneCb_t`. `ESP_ERR_WIFI_STATE` from `esp_wifi_scan_start()` maps to
`IOT_ERR_BUSY`.

> Never wrap `esp_wifi_scan_start()` / `esp_wifi_scan_get_ap_records()` in
> `ESP_ERROR_CHECK()`: it calls `abort()`, so a scan that cannot start (WiFi not started,
> or a scan already running) reboots the device. Return the error.

##### IOT_NwkRegisterEventCb()
```c
iot_err_t IOT_NwkRegisterEventCb(
    IOT_NetworkIface nwkInf,
    IOT_NwkEventStatus_t eventType,
    IOT_NwkEventCb cb,
    void *arg
);
```

**Pattern**: Similar to BLE event callbacks - maintain callback registry per interface and event type.

**ESP32**:
```c

iot_err_t IOT_NwkRegisterEventCb(
    IOT_NetworkIface nwkInf,
    IOT_NwkEventStatus_t eventType,
    IOT_NwkEventCb cb,
    void *arg)
{
    if (eventType >= IOT_NWK_EVENT_STATUS_MAX || cb == NULL) {
        return IOT_ERR_INVALID_ARG;
    }

    // ... 

    return IOT_OK;
}
```

##### IOT_NwkUnregisterEventCb()
```c
iot_err_t IOT_NwkUnregisterEventCb(
    IOT_NetworkIface nwkInf,
    IOT_NwkEventStatus_t eventType,
    IOT_NwkEventCb cb
);
```

Unregister only the matching callback for the interface and event type. Pass the same callback function pointer that was registered.

##### Auto Reconnect
```c
void IOT_NwkSetAutoReconnect(IOT_NetworkIface iface, bool enable);
bool IOT_NwkGetAutoReconnect(IOT_NetworkIface iface);
```

Use these APIs to enable or disable platform-managed reconnect behavior for a network interface.

**Back off, never give up.** Do not reconnect immediately and unconditionally on every
disconnect: when the AP is genuinely gone, each attempt is a full scan every few seconds,
forever, and on a single-radio chip that starves BLE — the phone can still see the
device's recovery advert, but a GATT connection never completes, and BLE is the only way to
give a device new WiFi credentials. The ESP32 reference waits

```
0 (first retry), then 1 s, 2 s, 4 s, 8 s, 16 s, then 30 s for every later attempt
```

and resets the count on a successful association. It is capped, not abandoned: an AP that
returns hours later is still rejoined without a reboot.

**Event Handler** (called from WiFi/Ethernet events):
```c
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    IOT_NwkEventStatus_t iot_event = map_esp_event_to_iot(event_id);

    if (wifi_callbacks[iot_event].registered) {
        IOT_NetworkIface iface = {.type = WIFI, .number = 0};

        // gọi callback 
    }
}
```

---

## 3. Transport Layer

### 3.1 IOT_MqttMgmt - MQTT Client

**Header**: `iotPlatform/include/IOT_MqttMgmt.h`
**ESP32 Implementation**: `implplatform/esp32/IOT_MqttMgmt.c`

#### Key Structures

```c
typedef enum {
    IOT_MQTT_EVENT_DISCONNECTED = 0,
    IOT_MQTT_EVENT_CONNECTED,
    IOT_MQTT_EVENT_SUBSCRIBED,
    IOT_MQTT_EVENT_UNSUBSCRIBED,
    IOT_MQTT_EVENT_PUBLISHED,
    IOT_MQTT_EVENT_DATA,
    IOT_MQTT_EVENT_ERROR,
    IOT_MQTT_EVENT_MAX
} IOT_MqttEventStatus_t;

typedef struct {
    char *uri;       // mqtt://broker.hivemq.com:1883
    uint16_t port;
    char *clientId;
    char *username;
    char *password;
    char *cert;      // Broker certificate (PEM format)
} IOT_MqttClientConfig_t;

typedef void (*IOT_MqttEventCb_t)(
    void *arg,
    IOT_MqttEventStatus_t eventType,
    void *eventData
);
```

#### APIs Cần Implement

##### IOT_MqttInit()
```c
iot_err_t IOT_MqttInit(void);
```

**ESP32**:
```c
static esp_mqtt_client_handle_t mqtt_client = NULL;
static mqtt_cb_entry_t callbacks[IOT_MQTT_EVENT_STATUS_TYPE_MAX];

iot_err_t IOT_MqttInit(void) {
    // Initialize callback registry
    memset(callbacks, 0, sizeof(callbacks));
    return IOT_OK;
}
```

##### IOT_MqttConfigClient()
```c
iot_err_t IOT_MqttConfigClient(const IOT_MqttClientConfig_t config);
```

**ESP32**:
```c
iot_err_t IOT_MqttConfigClient(const IOT_MqttClientConfig_t config) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = config.uri,
        .credentials = {
            .username = config.username,
            .client_id = config.clientId,
            .authentication = {
                .password = config.password,
            },
        },
    };

    if (config.cert != NULL) {
        mqtt_cfg.broker.verification.certificate = config.cert;
    }

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        return IOT_ERR_FAIL;
    }

    // Register internal event handler
    esp_mqtt_client_register_event(
        mqtt_client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL
    );

    return IOT_OK;
}
```

##### IOT_MqttConnect()
```c
iot_err_t IOT_MqttConnect(void);
```

**ESP32**:
```c
iot_err_t IOT_MqttConnect(void) {
    if (mqtt_client == NULL) {
        return IOT_ERR_NOT_INIT;
    }

    esp_err_t err = esp_mqtt_client_start(mqtt_client);
    return (err == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}
```

##### IOT_MqttSendMessage()
```c
iot_err_t IOT_MqttSendMessage(char *topic, uint8_t *payload, uint16_t len);
```

**ESP32**:
```c
iot_err_t IOT_MqttSendMessage(char *topic, uint8_t *payload, uint16_t len) {
    if (mqtt_client == NULL || topic == NULL) {
        return IOT_ERR_INVALID_ARG;
    }

    int msg_id = esp_mqtt_client_publish(
        mqtt_client,
        topic,
        (const char *)payload,
        len,
        1,      // QoS 1
        0       // Retain = false
    );

    return (msg_id >= 0) ? IOT_OK : IOT_ERR_FAIL;
}
```

**Event Handler**:
```c
static void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    IOT_MqttEventStatus_t iot_event;
    void *iot_event_data = NULL;

    // ...

    if (callbacks[iot_event].registered) {
        // gọi callback
    }
}
```

---

## 4. Data Access Object (DAO) Layer

### 4.1 IOT_DaoMgmt - DAO Initialization

**Header**: `iotPlatform/include/dao/IOT_DaoMgmt.h`
**ESP32 Implementation**: `implplatform/esp32/dao/IOT_DaoMgmt.c`

#### Purpose
Initialize NVS (Non-Volatile Storage) for persistent data.

##### IOT_DaoInit()
```c
iot_err_t IOT_DaoInit(void);
```

**ESP32**:
```c
#include "nvs_flash.h"

iot_err_t IOT_DaoInit(void) {
    // Initialize NVS
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Erase and retry
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    return (err == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}
```

**Platform Notes**:
- **ESP32**: Use NVS (Non-Volatile Storage) API
- **nRF52**: Use `fds` (Flash Data Storage) or `nvmc`
- **STM32**: Use Flash API or external EEPROM
- **Linux**: Use file system (SQLite, files in `/var/lib/`)

---

### 4.2 IOT_DaoStorage - Key/Value Storage

**Header**: `iotPlatform/include/dao/IOT_DaoStorage.h`
**ESP32 Implementation**: `implplatform/esp32/dao/IOT_DaoStorage.c`

#### Purpose
The platform-independent key/value store that every DAO is built on — core's own
configuration, the `sdkcontract/` stores (Device, Group, IR, Smart, Record), and anything
your own SDK needs to persist.

```c
iot_err_t IOT_DaoStorageGet(const char *ns, const char *key, IOT_DaoType type,
                            void **outData, uint16_t *outLen);        // BLOCKING
iot_err_t IOT_DaoStorageSet(const char *ns, const char *key, IOT_DaoType type,
                            void *data, uint16_t len);                // BLOCKING
iot_err_t IOT_DaoStorageSetAsync(const char *ns, const char *key, IOT_DaoType type,
                                 void *data, uint16_t len);           // NON-BLOCKING
iot_err_t IOT_DaoStorageSetBatch(const char *ns, const IOT_DaoBatchEntry_t *entries,
                                 uint16_t count);                     // BLOCKING, one commit
iot_err_t IOT_DaoStorageRemoveByKey(const char *ns, const char *key);
iot_err_t IOT_DaoStorageRemoveNamespace(const char *ns);
```

**Usage rules**:
- The blocking calls must **not** be made from a BLE/MQTT callback, and event
  handlers run on exactly those tasks. Read at startup, then write through with
  the `*Async` variants from handlers.
- Prefer `IOT_DaoStorageSetBatch()` over a loop of `Set` when writing many keys:
  the backing store opens and commits the namespace once instead of per key.
- Storing a C struct as a blob is only safe while its layout never changes. The
  firmware that WROTE the old layout is the one that installs the firmware that
  READS it, so a changed struct silently reinterprets old bytes. Store the new
  layout under a **new key**, and check the returned length before trusting a blob.

> Core's own configuration storage is internal to `iotcore.a` and is not part of
> the platform interface — you do not implement or call it.

**Platform Notes**:
- **ESP32**: NVS, with the async queue handled by a FreeRTOS task
- **nRF52**: `fds` / `nvmc`
- **STM32**: Flash API or external EEPROM
- **Linux**: file system (SQLite, files in `/var/lib/`)

---

### 4.3 IOT_Cached - In-memory cache over storage

**Header**: `iotPlatform/include/dao/IOT_Cached.h`
**ESP32 Implementation**: `implplatform/esp32/dao/IOT_Cached.c`

> ⚠️ **Breaking: `IOT_CacheConfig_t.namespace` is now `.nsName`.** `namespace` is a C++
> keyword, so the header could not be compiled as C++ at all. Same fields, same order, same
> sizes and semantics; only the name changed:
>
> ```c
> const IOT_CacheConfig_t cfg = { .nsName = MY_NVS_NAMESPACE, .maxEntries = 16 };
> ```
>
> Ignore it and the build fails with `'IOT_CacheConfig_t' has no member named 'namespace'`.
> The `IOT_Cache*()` functions that took a parameter named `namespace` now spell it
> `nsName`; that is a prototype parameter name, so no call site changes. You have work here
> only if you build that struct yourself or wrote your own cache layer.

## 5. Optional Driver Interfaces (Driver-Registration Model)

**Header locations**: `iotPlatform/include/` (interfaces) and `iotPlatform/src/` (dispatch)

Some platform features are **optional** and use a *driver-registration* model
instead of requiring you to implement every function directly. This lets a leaf
device (e.g. a plain ESP32 switch) link against the SDK without pulling in mesh,
Zigbee, or mDNS support at all.

> **Cost of unused optional features.** `iotcore.a` is a **pure protocol /
> orchestration layer** — it contains no hardware or RTOS code, only message
> handlers and state machines that call the dispatch layer. The handlers for
> optional features (hub role, Zigbee, mesh) are a few KB of this protocol glue,
> and they are **completely inert unless you register a driver**: every dispatch
> call returns `IOT_ERR_NOT_SUPPORTED` and does nothing. So leaving a feature
> unregistered costs only a small, fixed amount of flash and has **zero runtime
> behavior and no risk of crashing** — you do not need a build flag to "turn it
> off." The actual hardware drivers live in the platform sources you compile
> (`implplatform/`), which *are* excluded by their `CONFIG_IOT_*` flags.

### 5.0 How the driver-registration model works

For each optional feature, the SDK ships:

1. A **public interface header** in `iotPlatform/include/` that declares:
   - a driver vtable struct (a table of function pointers), and
   - a `..._RegisterDriver()` function.
2. A **dispatch source** in `iotPlatform/src/` (already compiled into the
   platform component — you do **not** edit it). Each public API function in the
   dispatch layer forwards the call to the registered driver's matching function
   pointer:

   ```c
   static const IOT_MeshDriver_t *s_meshDriver = NULL;

   void IOT_MeshRegisterDriver(const IOT_MeshDriver_t *driver) {
       s_meshDriver = driver;
   }

   iot_err_t IOT_MeshInit(void) {
       if (s_meshDriver == NULL || s_meshDriver->init == NULL) {
           return IOT_ERR_NOT_SUPPORTED;   // no driver → safe no-op
       }
       return s_meshDriver->init();
   }
   ```

**Key rule**: if no driver is registered (or the driver leaves a function pointer
NULL), the dispatch layer returns `IOT_ERR_NOT_SUPPORTED` and takes no action.
This is the intended no-op behavior — callers treat `IOT_ERR_NOT_SUPPORTED` as a
non-fatal "feature not present" and continue.

To provide an implementation, a partner:
1. Fills a static/global driver vtable with their function pointers.
2. Calls `..._RegisterDriver(&myDriver)` — typically from a
   `__attribute__((constructor))` so it runs before `IOT_CoreInit()` (see the BLE
   Implementation Guide for the force-link detail this requires in a static lib).

### 5.1 IOT_MdnsDriver — mDNS

**Interface**: `iotPlatform/include/IOT_MdnsDriver.h`
**Dispatch**: `iotPlatform/src/IOT_MdnsDriver.c`

```c
typedef struct {
    iot_err_t (*startService)(const char *hostname, const char *domain);
    iot_err_t (*stopService)(void);
    iot_err_t (*addAdvertisement)(char *serviceName, char *serviceType, uint16_t port);
    iot_err_t (*removeAdvertisement)(const char *serviceType);
    iot_err_t (*removeAllAdvertisements)(void);
    iot_err_t (*startDiscovery)(char *serviceType);
    iot_err_t (*stopDiscovery)(void);
    iot_err_t (*registerEventCallback)(mdns_event_status_type_t eventType,
                                       mdns_event_callback_t cb, void *arg);
    iot_err_t (*unregisterEventCallback)(mdns_event_status_type_t eventType);
} IOT_MdnsDriver_t;

void IOT_MdnsRegisterDriver(const IOT_MdnsDriver_t *driver);
```

The public `IOT_Mdns*()` API (declared in `IOT_MdnsMgmt.h`) dispatches to this
driver. Unregistered → every call returns `IOT_ERR_NOT_SUPPORTED`.

**`removeAdvertisement` takes the service TYPE**, not the instance name (the parameter was
misleadingly called `serviceName`; the signature is otherwise unchanged). It must remove
exactly that one service and leave the others advertised — core withdraws the provisioning
service once the device reaches the cloud, and the running-mode service has to survive
that. Expected behaviour:

- removing a type that is not registered returns `IOT_OK` (the caller's intent is already
  satisfied);
- `NULL` returns `IOT_ERR_INVALID_ARG` — never "remove everything";
- `removeAllAdvertisements` is the only call that clears every service.

On ESP32 this is `mdns_service_remove(serviceType, proto)`. The bundled Linux backend does
not yet track services individually and still clears all of them; do not rely on
per-service removal there.

### 5.2 IOT_ZigbeeCoordinator — Zigbee coordinator (optional)

> 🚧 **Hub role is IN DEVELOPMENT — NOT READY.** This driver is only used by the
> hub-role feature (`IOT_CoreHubRole.h`), which is not finished yet. SDK writers
> can ignore this interface for now; leaf devices leave it unregistered.

**Interfaces**: `iotPlatform/include/IOT_ZigbeeCoordinator.h` (public API +
callback/payload types) and `iotPlatform/include/IOT_ZigbeeCoordinatorDriver.h`
(driver vtable)
**Dispatch**: `iotPlatform/src/IOT_ZigbeeCoordinatorDriver.c`

```c
typedef struct {
    iot_err_t (*init)(const IOT_ZigbeeCoordinatorCallbacks_t *cbs);
    iot_err_t (*deinit)(void);
    iot_err_t (*openPairing)(uint16_t gatewayEid, uint8_t timeSec,
                             uint8_t mode, uint16_t deviceTypeFilter);
    iot_err_t (*closePairing)(uint16_t gatewayEid);
} IOT_ZigbeeCoordinatorDriver_t;

void IOT_ZigbeeCoordinatorRegisterDriver(const IOT_ZigbeeCoordinatorDriver_t *driver);
```

Only hub-role platforms implement this. **Leaf ESP32 devices leave it
unregistered** — the public `IOT_ZigbeeCoordinator*()` API then returns
`IOT_ERR_NOT_SUPPORTED` (a no-op), and `IOT_CoreHubZigbeeStart()` propagates that
as a non-fatal skip. The driver reports device join/interview/leave events back
through the `IOT_ZigbeeCoordinatorCallbacks_t` bundle passed to `init()`.

### 5.3 IOT_MeshDriver — BLE mesh (optional; implementation NOT shipped)

**Interface**: `iotPlatform/include/mesh/IOT_MeshDriver.h`
**Dispatch**: `iotPlatform/src/mesh/IOT_MeshDriver.c`

> **Note**: Only the abstract mesh **interface + dispatch layer** ships. The
> actual mesh *implementation* (e.g. the ESP32 BLE-mesh driver / `libiotmesh.a`)
> is Rogo-internal and is **not** included in this package.

```c
typedef struct {
    // Lifecycle
    iot_err_t (*init)(void);
    iot_err_t (*deinit)(void);
    iot_err_t (*registerGattServices)(void);  // optional — NULL = nothing to register
    iot_err_t (*radioStarted)(void);          // optional — NULL = nothing to re-arm
    bool      (*isProvisioned)(void);
    // Network
    iot_err_t (*joinNetwork)(const IOT_MeshCredentials_t *credentials);
    iot_err_t (*deleteNode)(uint16_t nodeAddr, const uint8_t *devKey);
    // Persistence
    iot_err_t (*saveSequence)(void);
    // Node cache
    iot_err_t (*setNodeCache)(const IOT_MeshNodeEntry_t *entries, uint8_t count);
    const uint8_t *(*getDevKey)(uint16_t nodeAddr);
    // Callbacks
    iot_err_t (*registerMessageCb)(IOT_MeshMessageCb_t cb);
    iot_err_t (*registerTopologyCb)(IOT_MeshTopologyCb_t cb);
    iot_err_t (*notifyTopologyChange)(const IOT_MeshTopologyEvent_t *event);
    // Control (send)
    iot_err_t (*sendOnOff)(uint16_t nwkAddr, uint8_t onoff, uint16_t appIdx);
    iot_err_t (*sendLightCTL)(uint16_t nwkAddr, uint16_t lightness,
                              uint16_t temperature, uint16_t appIdx);
    iot_err_t (*sendLightHSL)(uint16_t nwkAddr, uint16_t hue, uint16_t saturation,
                              uint16_t lightness, uint16_t appIdx);
    iot_err_t (*sendVendor)(uint16_t deviceType, uint16_t nwkAddr,
                            uint8_t *data, uint16_t dataLen);
} IOT_MeshDriver_t;

void IOT_MeshRegisterDriver(const IOT_MeshDriver_t *driver);
```

The public `IOT_Mesh*()` API (declared in `mesh/IOT_MeshMgmt.h` /
`mesh/IOT_MeshControl.h`) dispatches to this driver. Unregistered → every call
returns `IOT_ERR_NOT_SUPPORTED` (control/send functions) or a safe default
(`IOT_MeshIsProvisioned()` → `false`, `IOT_MeshGetDevKey()` → `NULL`).

> ⚠️ **Two members were inserted after `deinit`**, not appended. A driver table built with
> designated initialisers (`.init = …`) needs no change; one built positionally must be
> rewritten.

Two new public calls dispatch to them. Unlike the rest of this interface, both return
**`IOT_OK`** when no driver is registered or the slot is `NULL` — they are lifecycle steps,
not feature probes, and a build without mesh is fine:

| call | when | what the driver does |
|---|---|---|
| `IOT_MeshRegisterGattServices()` | once during platform init, after `IOT_BleInit()` and before anything can advertise | puts the mesh stack's own GATT services on the pending list the BLE host consumes when it starts (see `IOT_BleStart()` in the BLE Implementation Guide). A service offered later is never registered. Idempotent; a non-`IOT_OK` result fails the boot. |
| `IOT_MeshRadioStarted()` | after the BLE radio comes up, when the mesh join may already have happened | re-arms anything the stack armed at join time while the radio was still down (e.g. its scan). Safe to call unconditionally. |

### 5.4 IOT_PlatformCapabilities — runtime service health

**Header**: `iotPlatform/include/IOT_PlatformCapabilities.h`
**ESP32 Implementation**: `implplatform/esp32/IOT_PlatformCapabilities.c`

Unlike the interfaces above, this is a **single function** you implement directly
(no vtable). It answers exactly one kind of question: **is a platform service
working right now?**

```c
typedef enum {
    IOT_SERVICE_DOWN     = 0, // absent, or present but not responding
    IOT_SERVICE_UP       = 1, // present and responding
    IOT_SERVICE_DEGRADED = 2, // present but not fully responsive — answer, but log it
} IOT_ServiceHealth_t;

typedef enum {
    IOT_PLATFORM_SERVICE_ZIGBEE,
} IOT_PlatformService_t;

IOT_ServiceHealth_t IOT_PlatformGetServiceHealth(IOT_PlatformService_t service);
```

Only services with a genuine runtime answer belong here. On ESP32 the Zigbee entry
reports whether the coordinator is up; on Linux it can probe the zigbee2mqtt service,
which really does come and go while the firmware runs.

**Two questions this deliberately does NOT answer**, because conflating them caused
real bugs:

- *"Is this feature compiled in?"* — driver registration already answers this, and
  answers it truthfully. Every `IOT_<Driver>*` call forwards through a null-checked
  vtable and returns `IOT_ERR_NOT_SUPPORTED` when nothing is registered; see also
  `IOT_BleIsAvailable()`. A compile-time check cannot see a driver that was compiled
  but failed to link — which is exactly how a mesh driver once stayed unregistered
  while a capability query cheerfully reported it present.
- *"Does this device play the hub role?"* — that is your SDK's decision, declared with
  `IOT_CoreSetHubRole()` and read back with `IOT_CoreHasHubRole()`. A device can have a
  transport compiled in without playing a hub role at all.

---

## 6. Common Patterns (REFERENCE ONLY)

### 6.1 Callback Registry Pattern

Used in: MQTT, TCP, Network, BLE

```c
typedef struct {
    callback_fn_t cb;
    void *arg;
    bool registered;
} callback_entry_t;

static callback_entry_t callbacks[EVENT_TYPE_MAX];

iot_err_t register_callback(event_type_t event, callback_fn_t cb, void *arg) {
    if (event >= EVENT_TYPE_MAX || cb == NULL) {
        return IOT_ERR_INVALID_ARG;
    }

    callbacks[event].cb = cb;
    callbacks[event].arg = arg;
    callbacks[event].registered = true;

    return IOT_OK;
}

// In platform event handler:
void platform_event_handler(...) {
    event_type_t iot_event = map_platform_event(...);

    if (callbacks[iot_event].registered) {
        callbacks[iot_event].cb(
            callbacks[iot_event].arg,
            iot_event,
            event_data
        );
    }
}
```

### 6.2 Opaque Handle Pattern

Used for opaque platform handles (for example the OSAL mutex handle).

```c
// Header (platform-independent):
typedef struct IOT_Thing_s *IOT_Thing;

// Implementation (platform-specific):
struct IOT_Thing_s {
    platform_handle_t platform_thing;
    // ... other platform-specific fields
};
```

---

## 7. Checklist

Platform Implementation Checklist:

- [ ] **Core Utilities**
  - [ ] OSAL mutexes (`IOT_OsalMutex*`)
  - [ ] TimerMgmt (callbacks, time, delays)
  - [ ] Log (if applicable)
  - [ ] `IOT_OsalGetHeapInfo()` (or return `IOT_ERR_NOT_SUPPORTED`)

- [ ] **Network**
  - [ ] NetworkMgmt (WiFi/Ethernet init & events)
  - [ ] Interface arrays walked to `NETWORK_TYPE_NONE`, bounded by `IOT_NWK_MAX_IFACES`
  - [ ] WiFi connect/scan, including `IOT_NwkScanWifiAsync()` (exactly-once callback, `IOT_ERR_BUSY`)
  - [ ] Auto-reconnect with backoff
  - [ ] Ethernet init (if supported)

- [ ] **Transport**
  - [ ] MQTT client (connect, pub/sub, events)
  - [ ] TCP server (listen, send, receive)
  - [ ] mDNS (if applicable)

- [ ] **DAO**
  - [ ] DaoMgmt init (NVS/Flash)
  - [ ] `IOT_CacheConfig_t` built with `.nsName`
  - [ ] `IOT_DaoStorage` (key-value storage: core keeps its own configuration on it)
  - [ ] `IOT_Cached` (the cache in front of storage)

- [ ] **BLE** (PRIORITY)
  - [ ] BleMgmt driver vtable + `IOT_BleRegisterDriver()` (see BLE Implementation Guide)
  - [ ] `start` slot if your stack needs a separate host-activation step (`IOT_BleStart()`)

- [ ] **Optional driver interfaces** (only if the feature is present)
  - [ ] mDNS driver (`IOT_MdnsRegisterDriver`)
  - [ ] Zigbee coordinator driver (`IOT_ZigbeeCoordinatorRegisterDriver`) — hub role only
  - [ ] Mesh driver (`IOT_MeshRegisterDriver`) — implementation not shipped; `registerGattServices` / `radioStarted` if your stack needs them
  - [ ] `IOT_PlatformGetServiceHealth()`

---
