#include "cloud_sync.h"

#include "app.h"
#include "cloud_event_handler.h"
#include "device_element_manager.h"
#include "led_manager.h"
#include "system_config.h"
#include "report_alert.h"

#include "IOT_Core.h"
#include "IOT_Contract.h"
#include "IOT_ContractInternal.h"
#include "IOT_ContractStateQuery.h"
#include "IOT_Log.h"

#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "warning_manager.h"

#include "tds_task.h"

static const char *TAG = "CLOUD_SYNC";

/* The engine starts one MQTT connect by itself when provisioning reaches
 * syncProgress=9. At that instant the BLE stack is still up: measured on the
 * bench as free=4132 B / largest=1280 B, which cannot hold esp-mqtt's 6144 B
 * task stack ("Error create mqtt task"), and on a tighter boot cannot even
 * hold its 2048 B input buffer (esp_mqtt_set_config returns ESP_OK with a
 * NULL config in IDF 5.5, then the task load-faults). The supervisor below
 * runs after the BLE release and retries with backoff. */
#define CLOUD_SYNC_SETTLE_US         (1000LL * 1000LL)
#define CLOUD_SYNC_BACKOFF_MIN_S     2U
#define CLOUD_SYNC_BACKOFF_MAX_S     60U
/* How long the TDS tasks may stay stopped while the cloud session takes the
 * heap. They are the largest reclaimable block and the TLS handshake needs a
 * single ~16.7 KB arena right after the BLE release. */
#define CLOUD_SYNC_TDS_GRACE_US      (15LL * 1000LL * 1000LL)

/* esp-mqtt init + task + TLS handshake. Below these floors do not start the
 * client at all: the failure mode is a panic inside the closed engine, not a
 * recoverable error we can handle. */
#define CLOUD_SYNC_MIN_FREE_BYTES    15000U
#define CLOUD_SYNC_MIN_LARGEST_BLOCK 4096U

static volatile _bool s_bleReleased = D_FALSE;
static _bool s_cloudReady = D_FALSE;
static UI32 s_attempt = 0U;
static UI64 s_nextAttemptUs = 0U;
static _bool s_tdsPending = D_FALSE;
static UI64 s_tdsDeadlineUs = 0U;
static UI64 s_nextReportUs = 0U;

static void CloudSyncPublishReady(void)
{
    /* The reference CloudSync_Task's EVT_MQTT_READY block: on every ready,
     * re-introduce the device rather than only on the first connect. A fresh
     * server record, a reconnect and a warranty activation all end registered
     * without a special case. */
    g_cloud_is_connected = 1U;
    
    UI32 warning = WarningManagerGetMask();
    if(warning)
    {
        LedManagerSetError();
    }
    else
    {
        LedManagerSetNormal();
    }
    AppPushWarningToCloud();     /* element 1 issues mask + full state report */
    AppRspToCloud(ELEMENT_ID_1, ATTR_INFOMATION_PRODUCT_USAGE_TIME);
    AppRspToCloud(ELEMENT_ID_2, ATTR_INFOMATION_PRODUCT_USAGE_TIME);
    AppRspToCloud(ELEMENT_ID_3, ATTR_INFOMATION_PRODUCT_USAGE_TIME);
    AppRspToCloud(ELEMENT_ID_4, ATTR_INFOMATION_PRODUCT_USAGE_TIME);
    AppRspToCloud(ELEMENT_ID_5, ATTR_INFOMATION_PRODUCT_USAGE_TIME);

    AppRspToCloud(ELEMENT_ID_2, ATTR_ENERGY_SAVING_SCHEDULE);

    AppRspToCloud(ELEMENT_ID_4, ATTR_ONOFF);
    AppRspToCloud(ELEMENT_ID_5, ATTR_ONOFF);
    AppRspToCloud(ELEMENT_ID_4, ATTR_TEMP_SET);
    AppRspToCloud(ELEMENT_ID_5, ATTR_TEMP_SET);

    AppRspToCloud(ELEMENT_ID_2, ATTR_ENERGY);
    AppRspToCloud(ELEMENT_ID_3, ATTR_ENERGY);
    AppRspToCloud(ELEMENT_ID_1, ATTR_INFOMATION_WATER_CONSUMER);

    AppRspToCloud(ELEMENT_ID_2, ATTR_EVENT_TDS_STATUS);
    AppRspToCloud(ELEMENT_ID_3, ATTR_EVENT_TDS_STATUS);


    /* service/RAL report: the contract asks for a random 0-30 s after connect
     * so a site-wide reconnect does not hit the server in one burst. */
    s_nextReportUs = (UI64)(esp_timer_get_time()) +
                     ((UI64)(esp_random() % 30000U) * 1000ULL);

    IOT_ContractOnCloudConnected();   /* Record kick + Smart scheduler */
    IOT_ContractStateSyncSchedule();  /* contract full-state sync (3 s) */

    IOT_LOGI(TAG, "cloud ready: device state published");
}

void CloudSyncInit(void)
{
    s_bleReleased = D_FALSE;
    s_cloudReady = D_FALSE;
    s_attempt = 0U;
    s_nextAttemptUs = 0U;
    s_tdsPending = D_FALSE;
    s_tdsDeadlineUs = 0U;
    s_nextReportUs = 0U;
}

void CloudSyncSetBleReleased(void)
{
    s_bleReleased = D_TRUE;
    s_attempt = 0U;
    s_nextAttemptUs = (UI64)(esp_timer_get_time()) + CLOUD_SYNC_SETTLE_US;
    IOT_LOGI(TAG, "BLE released, first cloud attempt in 1 s");
}

void CloudSyncStartTdsWhenRoomy(void)
{
    s_tdsPending = D_TRUE;
    s_tdsDeadlineUs = (UI64)(esp_timer_get_time()) + CLOUD_SYNC_TDS_GRACE_US;
}

void CloudSyncNotifyCloudConnected(void)
{
    /* Only the log here: publishing waits for IOT_CoreIsCloudConnected()
     * (subscriptions confirmed), which the tick polls. A publish on the bare
     * broker-connect edge is silently lost. */
    IOT_LOGI(TAG, "MQTT connected, waiting for subscriptions");
}

void CloudSyncNotifyCloudDisconnected(void)
{
    s_cloudReady = D_FALSE;
    g_cloud_is_connected = 0U;
    s_nextReportUs = 0U;
    LedManagerSetError();
}

_bool CloudSyncIsCloudReady(void)
{
    return s_cloudReady;
}

static UI32 CloudSyncBackoffSeconds(UI32 attempt)
{
    UI32 delay = CLOUD_SYNC_BACKOFF_MIN_S;

    for (UI32 step = 1U; (step < attempt) && (delay < CLOUD_SYNC_BACKOFF_MAX_S); ++step)
    {
        delay *= 2U;
    }

    return (delay > CLOUD_SYNC_BACKOFF_MAX_S) ? CLOUD_SYNC_BACKOFF_MAX_S : delay;
}

void CloudSyncTick(void)
{
    const UI64 nowUs = (UI64)(esp_timer_get_time());

    /* TDS restart is deferred behind the first cloud attempt: its two tasks
     * take 16 KB, which is the space the TLS handshake needs. Start it once
     * the session is ready, or after the grace period so a broken cloud can
     * never disable the sensor path. TdsTaskStart() is idempotent. */
    if ((D_TRUE == s_tdsPending) &&
        ((D_TRUE == s_cloudReady) || (nowUs >= s_tdsDeadlineUs)))
    {
        s_tdsPending = D_FALSE;
        TdsTaskStart();
    }

    if (D_FALSE == IOT_CoreIsProvisioned())
    {
        return;
    }

    if (D_TRUE == IOT_CoreIsCloudConnected())
    {
        s_attempt = 0U;
        s_nextAttemptUs = 0U;
        if (D_FALSE == s_cloudReady)
        {
            s_cloudReady = D_TRUE;
            CloudSyncPublishReady();
            if (D_TRUE == s_tdsPending)
            {
                s_tdsPending = D_FALSE;
                TdsTaskStart();
            }
        }

        /* Periodic service/RAL report, after the connect jitter above. */
        if ((0U != s_nextReportUs) && (nowUs >= s_nextReportUs))
        {
            UI32 intervalMs = SystemConfigGetReportIntervalMs();
            if (intervalMs < 60000U)
            {
                intervalMs = 60000U;
            }
            const iot_err_t reportErr = ReportAlertPublishReport();
            if (IOT_OK != reportErr)
            {
                IOT_LOGW(TAG, "periodic report failed: %s", iot_err_to_name(reportErr));
            }
            s_nextReportUs = nowUs + ((UI64)intervalMs * 1000ULL);
        }
        return;
    }

    s_cloudReady = D_FALSE;
    g_cloud_is_connected = 0U;

    /* The engine's own provisioning-time attempt is the only one allowed
     * while BLE is up; ours start after the release. */
    if (D_FALSE == s_bleReleased)
    {
        return;
    }

    if (nowUs < s_nextAttemptUs)
    {
        return;
    }

    const size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if ((freeBytes < CLOUD_SYNC_MIN_FREE_BYTES) || (largestBlock < CLOUD_SYNC_MIN_LARGEST_BLOCK))
    {
        IOT_LOGW(TAG, "cloud connect deferred: free=%u largest=%u",
                 (unsigned)freeBytes, (unsigned)largestBlock);
    }
    else
    {
        const iot_err_t err = IOT_CoreCloudConnect();
        IOT_LOGW(TAG, "cloud connect attempt %u (free=%u largest=%u): %s",
                 (unsigned)(s_attempt + 1U), (unsigned)freeBytes, (unsigned)largestBlock,
                 iot_err_to_name(err));
    }

    s_attempt++;
    s_nextAttemptUs = nowUs + ((UI64)CloudSyncBackoffSeconds(s_attempt) * 1000000ULL);
}
