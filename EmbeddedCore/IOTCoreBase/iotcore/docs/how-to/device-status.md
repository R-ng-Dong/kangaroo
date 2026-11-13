# Report the device status

**Both paths.** The device tells the cloud its firmware version, uptime, capabilities and memory:
unsolicited after each cloud connect, and in answer to a status request from the cloud or the app.
Core sends neither by itself, and on path A the contract does not send it either: both are yours.

## When It Fires

| Trigger | Mechanism |
|---------|-----------|
| Cloud (re)connect | `DEV_EVENT_CLOUD_CONNECTED` → your SDK schedules a delayed report (~5s) |
| Status request (cloud or app) | `DEV_EVENT_DEVICE_STATUS_REQUEST` → your SDK answers with `IOT_CoreRespondDeviceStatus()` |

## Data Structure

```c
#include "IOT_CoreReporter.h"

typedef struct {
    const char *firmwareVersion;  // Firmware version string (e.g., "1.2.3")
    int64_t uptimeSeconds;        // Seconds since boot
    int64_t lastActivationTime;   // Unix timestamp of last state change (0 if unknown)
    uint8_t protocolFlags[6];     // [mode, mesh, zigbee, matter, reserved, reserved]
    uint8_t ramUsagePercent;      // RAM usage 0-100%
    uint8_t nvsUsagePercent;      // NVS usage 0-100%
    uint32_t totalRamKB;          // Total RAM in KB
    uint32_t totalNvsKB;          // Total NVS in KB
} IOT_CoreDeviceStatusData_t;

// The periodic report, unsolicited
iot_err_t IOT_CoreRequestReportDeviceStatus(const IOT_CoreDeviceStatusData_t *status);
// The answer to a status request, to whoever asked
iot_err_t IOT_CoreRespondDeviceStatus(const IOT_RequestContext_t *ctx, const IOT_CoreDeviceStatusData_t *status);
```

## Handling Cloud Reconnect (scheduled report)

In your device event handler, schedule a delayed device status report after MQTT reconnect:

```c
case DEV_EVENT_CLOUD_CONNECTED:
{
    // ... existing reconnect logic (smart scheduler, state sync) ...

    // Schedule device status report (5s delay to avoid congestion). The timer callback only
    // hands sendDeviceStatusReport() (below) to core's task with IOT_CoreDeferWork(): the
    // report blocks while it is sent, which a timer callback must never do.
    scheduleDeviceStatusReport();
    break;
}
```

## Handling a Status Request

When the cloud or the app asks for the device status, iotcore dispatches
`DEV_EVENT_DEVICE_STATUS_REQUEST` with the request in `data.deviceStatusRequest`. Answer it with
`IOT_CoreRespondDeviceStatus()` on the request's `ctx`: the reply goes back on the transport the
request came on (BLE, the LAN or the cloud), with the request's transaction id. Then write the
call's return into `result`, the request's answer. Core presets `result` to
`IOT_ERR_NOT_SUPPORTED`: unclaimed, the asker is told the request is not supported, and on any other
error it is told the request failed.

```c
case DEV_EVENT_DEVICE_STATUS_REQUEST:
{
    IOT_CoreDeviceEventStatusRequestData_t *req = &deviceEvent->data.deviceStatusRequest;
    char versionBuf[32] = {0};
    IOT_CoreDeviceStatusData_t status;
    collectDeviceStatus(&status, versionBuf, sizeof(versionBuf)); // see Collecting and Sending Status
    req->result = IOT_CoreRespondDeviceStatus(&req->ctx, &status);
    break;
}
```

Answer before returning: `ctx` is valid only during the call. Answer whether or not the device is
provisioned, since a phone may ask over BLE during setup. Do not answer with
`IOT_CoreRequestReportDeviceStatus()`: that report is unsolicited, goes to the cloud only, and the
asker cannot match it to its request.

## Collecting and Sending Status

One function collects the status; the periodic report and the answer to a request both use it.

```c
#include "IOT_CoreReporter.h"
#include "IOT_Osal.h"
#include "IOT_AppManager.h"
#include "dao/IOT_DaoMgmt.h"

/* versionBuf holds the version string the status points at. */
static void collectDeviceStatus(IOT_CoreDeviceStatusData_t *status, char *versionBuf, size_t versionBufSize)
{
    // 1. Firmware version
    IOT_AppManagerGetVersion(versionBuf, versionBufSize);

    // 2. RAM stats
    uint32_t freeHeap = IOT_OsalGetFreeHeapSize();
    uint32_t totalHeap = IOT_OsalGetTotalHeapSize();
    uint8_t ramPercent = 0;
    if (totalHeap > 0) {
        ramPercent = (uint8_t)(((totalHeap - freeHeap) * 100) / totalHeap);
    }

    // 3. NVS stats
    uint32_t nvsUsed = 0, nvsTotal = 0;
    uint8_t nvsPercent = 0;
    IOT_DaoGetStorageStats(&nvsUsed, &nvsTotal);
    if (nvsTotal > 0) {
        nvsPercent = (uint8_t)((nvsUsed * 100) / nvsTotal);
    }

    // 4. Uptime, last activation, capabilities
    *status = (IOT_CoreDeviceStatusData_t) {
        .firmwareVersion = versionBuf,
        .uptimeSeconds = IOT_OsalGetUptimeUs() / 1000000,
        .lastActivationTime = s_lastActivationTime,
        .protocolFlags = {IOT_DEVICE_MODE_INDEPENDENT}, // [mode, mesh, zigbee, matter, 0, 0]; never mode 0
        .ramUsagePercent = ramPercent,
        .nvsUsagePercent = nvsPercent,
        .totalRamKB = totalHeap / 1024,
        .totalNvsKB = (nvsTotal * 32) / 1024,
    };
}

/* The periodic, unsolicited report. */
static void sendDeviceStatusReport(void)
{
    char versionBuf[32] = {0};
    IOT_CoreDeviceStatusData_t status;
    collectDeviceStatus(&status, versionBuf, sizeof(versionBuf));
    IOT_CoreRequestReportDeviceStatus(&status);
}
```

## Tracking Last Activation Time

Update a timestamp in your state SET handler to track when the device was last actively controlled:

```c
#include <time.h>
static int64_t s_lastActivationTime = 0;

// In your state SET handler, after successful DAO update:
s_lastActivationTime = (int64_t)time(NULL);
```

## protocolFlags

`protocolFlags[0]` is the device's mode, one of `IOT_DEVICE_MODE_MASTERSLAVE`, `_MASTER`, `_SLAVE`
or `_INDEPENDENT` (the usual value). There is no zero mode: a device reporting 0 cannot be classified
by the app and is not shown as a hub. `protocolFlags[1..3]` (mesh, Zigbee, Matter) are 1 only for a
transport this build can actually serve; the cloud offers what you claim, and a claim the build
cannot honour fails later.

