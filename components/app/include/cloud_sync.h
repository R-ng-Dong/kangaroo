#ifndef CLOUD_SYNC_H_
#define CLOUD_SYNC_H_

#include "common.h"

/* Cloud session supervisor.

 * Mirrors the reference product's cloud_sync task (products/purifier/
 * purifier.c, CloudSync_Task) on top of the closed iotcore engine:
 *
 *   - the first connect is never started while the BLE provisioning link
 *     still holds the heap (the engine cannot be told to defer it, so the
 *     retry below is what recovers that attempt);
 *   - every connect attempt needs real headroom before it is started, because
 *     ESP-IDF 5.5's esp_mqtt_set_config() returns ESP_OK after a failed
 *     allocation and leaves client->config NULL (panic in the MQTT task);
 *   - on every transition to cloud-ready (transport up AND every subscription
 *     confirmed, IOT_CoreIsCloudConnected()) the device re-introduces itself:
 *     warning mask, full element/attribute state, contract state sync and the
 *     service/RAL report. This is the reference EVT_MQTT_READY block. */

void CloudSyncInit(void);

/* Called once the BLE stack has really been released (RAL_DeinitBle). */
void CloudSyncSetBleReleased(void);

/* Restart the TDS tasks once the cloud session owns the heap (or after a
 * 15 s grace). The two TDS tasks take 16 KB and would otherwise consume the
 * space the TLS handshake needs right after the BLE release. */
void CloudSyncStartTdsWhenRoomy(void);

/* Device events; the tick still polls IOT_CoreIsCloudConnected() itself. */
void CloudSyncNotifyCloudConnected(void);
void CloudSyncNotifyCloudDisconnected(void);

/* 1 s tick, call from the application task (no extra task/heap). */
void CloudSyncTick(void);

_bool CloudSyncIsCloudReady(void);

#endif /* CLOUD_SYNC_H_ */
