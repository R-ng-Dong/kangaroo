#include "IOT_AppManager.h"
#include "IOT_Memory.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "IOT_Log.h"
#include <string.h>
#include <strings.h>
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network/wifi.h"
#include "dao/IOT_Cached.h"

// Enable only for local diagnostics. Distribution sync preflight fails if these are enabled.
// #define IOT_OTA_TRACE_CERT   // dump the PEM cert used for the OTA TLS handshake
// #define IOT_OTA_TRACE_AUTH   // dump the Bearer auth token sent to the OTA server

#if defined(IOT_OTA_TRACE_CERT) || defined(IOT_OTA_TRACE_AUTH)
#include <stdio.h>
#endif

static const char *TAG = "IOT_AppManager";

/* ---- OTA state tracking (read by core via IOT_AppManagerGetOtaState) ---- */
static IOT_OtaState_t s_otaState = {
    .statusCode = IOT_OTA_STATUS_NOT_UPDATE,
    .statusData = 0,
    .targetVersion = {0},
};

typedef struct
{
    char *url;
    char *cert;
    size_t certLen;
    char *authToken;
    size_t authTokenLen;
    void *user_data;
    IOT_OtaProgressCb_t progressCb;
} ota_task_params_t;

static esp_err_t ota_http_client_init_cb(esp_http_client_handle_t client)
{
    char *token = NULL;
    esp_err_t err = esp_http_client_get_user_data(client, (void **)&token);
    if (err != ESP_OK || token == NULL)
    {
        return ESP_OK;
    }

    size_t len = strlen("Bearer ") + strlen(token) + 1;
    char *auth_header = Mem_SafeMalloc(len, TAG, "ota auth header");
    if (auth_header == NULL)
    {
        return ESP_FAIL;
    }

    snprintf(auth_header, len, "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    SAFE_FREE(auth_header);
    return ESP_OK;
}

/* The OTA endpoint 302-redirects to an S3 pre-signed URL. esp_http_client
 * carries request headers across the redirect, but S3 rejects a pre-signed
 * request that also sends an Authorization header ("Only one auth mechanism
 * allowed", HTTP 400) — which then surfaces as "Failed to read image
 * descriptor". esp_https_ota drives redirects itself (esp_http_client_set_
 * redirection) and never dispatches HTTP_EVENT_REDIRECT, but fetch_headers
 * still dispatches HTTP_EVENT_ON_HEADER for every response header. So when a
 * Location header is seen (a redirect is imminent), drop Authorization so it
 * is not replayed to the redirect target; the pre-signed query string is that
 * target's own auth. The origin request keeps its Authorization. */
static esp_err_t ota_http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key != NULL &&
        strcasecmp(evt->header_key, "Location") == 0)
    {
        esp_http_client_delete_header(evt->client, "Authorization");
    }
    return ESP_OK;
}

static void ota_update_task(void *pvParameter)
{
    ota_task_params_t *params = (ota_task_params_t *)pvParameter;
    IOT_OtaProgressCb_t progressCb = params->progressCb;

    IOT_LOGI(TAG, "OTA task started, url=%s", params->url);
    IOT_LOGW(TAG, "Heap before OTA: free=%lu, largest_block=%lu",
             (unsigned long) esp_get_free_heap_size(),
             (unsigned long) heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

    /* Wait for WiFi connection before downloading (matching WiLe behavior) */
    int waitCount = 0;
    while (IOT_WifiGetConnectionStatus(0) != WIFI_EVENT_STATUS_GOT_IP)
    {
        if (++waitCount > 30) /* 60s timeout */
        {
            IOT_LOGE(TAG, "OTA aborted: WiFi not connected after 60s");
            goto cleanup;
        }
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    esp_http_client_config_t http_config = {
        .url = params->url,
        .timeout_ms = 8000,
        .keep_alive_enable = true,
        .buffer_size_tx = 2048,
        .event_handler = ota_http_event_handler,
    };
#ifdef IOT_OTA_TRACE_CERT
    if (params->cert != NULL)
    {
        IOT_LOGW(TAG, "OTA cert (%u bytes):", (unsigned)params->certLen);
        for (size_t i = 0; i < params->certLen; i++)
        {
            printf("%c", params->cert[i]);
        }
        printf("\n");
    }
#endif
#ifdef IOT_OTA_TRACE_AUTH
    if (params->authToken != NULL)
    {
        IOT_LOGW(TAG, "OTA auth token: %.*s", (int)params->authTokenLen, params->authToken);
    }
#endif
    /* Try with NVS certificate first if provided */
    if (params->cert != NULL)
    {
        http_config.cert_pem = params->cert;
    }
    else
    {
        http_config.crt_bundle_attach = esp_crt_bundle_attach;
    }

    if (params->authToken != NULL && params->authTokenLen > 0)
    {
        http_config.user_data = (void *)params->authToken;
    }

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .http_client_init_cb = ota_http_client_init_cb,
    };

    /* Notify: download starting — (0x04, 0) */
    s_otaState.statusCode = IOT_OTA_STATUS_DOWNLOADING;
    s_otaState.statusData = 0;
    if (progressCb != NULL)
    {
        progressCb(IOT_OTA_STATUS_DOWNLOADING, 0, params->user_data);
    }

    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &ota_handle);

    /* Fallback: if NVS cert failed, retry with CA bundle */
    if (err != ESP_OK && params->cert != NULL)
    {
        IOT_LOGW(TAG, "OTA failed with NVS cert (err=0x%x), retrying with CA bundle for public server...", err);
        http_config.cert_pem = NULL;
        http_config.crt_bundle_attach = esp_crt_bundle_attach;
        err = esp_https_ota_begin(&ota_config, &ota_handle);
    }

    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(ota_handle, &app_desc);
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to read image descriptor");
        esp_https_ota_abort(ota_handle);
        goto cleanup;
    }

    /* Store OTA target version for INFO_SOFTWARE reporting */
    strncpy(s_otaState.targetVersion, app_desc.version, sizeof(s_otaState.targetVersion) - 1);
    s_otaState.targetVersion[sizeof(s_otaState.targetVersion) - 1] = '\0';

    int image_size = esp_https_ota_get_image_size(ota_handle);
    IOT_LOGI(TAG, "OTA image size: %d bytes, version: %s", image_size, app_desc.version);

    float progress = 0;
    while (1)
    {
        err = esp_https_ota_perform(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS)
        {
            break;
        }

        if (image_size > 0)
        {
            float current = (float)esp_https_ota_get_image_len_read(ota_handle) / image_size * 100;
            if (current - progress >= 10)
            {
                progress = current;
                s_otaState.statusData = (uint8_t)progress;
                IOT_LOGI(TAG, "OTA progress: %d%%", (int)progress);
                if (progressCb != NULL)
                {
                    /* (0x06, %) — progress ticks 10..90 during download+write */
                    progressCb(IOT_OTA_STATUS_SOFTWAREPROGRESS, (uint8_t)progress, params->user_data);
                }
            }
        }
    }

    if (!esp_https_ota_is_complete_data_received(ota_handle))
    {
        IOT_LOGE(TAG, "OTA: incomplete data received");
        esp_https_ota_abort(ota_handle);
        goto cleanup;
    }

    esp_err_t finish_err = esp_https_ota_finish(ota_handle);
    if (err != ESP_OK || finish_err != ESP_OK)
    {
        if (finish_err == ESP_ERR_OTA_VALIDATE_FAILED)
        {
            IOT_LOGE(TAG, "OTA image validation failed, corrupted");
        }
        IOT_LOGE(TAG, "OTA finish failed: 0x%x", finish_err);
        goto cleanup;
    }

    IOT_LOGW(TAG, "OTA successful. Restarting in 3 seconds...");
    s_otaState.statusCode = IOT_OTA_STATUS_UPDATING;
    s_otaState.statusData = 0;
    if (progressCb != NULL)
    {
        /* (0x05, 0) — flash/validate complete, about to reboot */
        progressCb(IOT_OTA_STATUS_UPDATING, 0, params->user_data);
    }
    s_otaState.statusCode = IOT_OTA_STATUS_SOFTWAREPROGRESS;
    s_otaState.statusData = 100;
    if (progressCb != NULL)
    {
        /* (0x06, 100) — 100% done, reboot imminent */
        progressCb(IOT_OTA_STATUS_SOFTWAREPROGRESS, 100, params->user_data);
    }
    s_otaState.statusCode = IOT_OTA_STATUS_SUCCESS;
    s_otaState.statusData = 100;
    if (progressCb != NULL)
    {
        /* SUCCESS signal — handler uses this for NVS persist + context cleanup; no wire message sent */
        progressCb(IOT_OTA_STATUS_SUCCESS, 100, params->user_data);
    }
    SAFE_FREE(params->url);
    SAFE_FREE(params->cert);
    SAFE_FREE(params->authToken);
    SAFE_FREE(params);
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    /* Push write-back caches to flash before the reboot. Element state is write-back with a
     * few-seconds window, so a restart that skips this silently discards the newest state. */
    IOT_CacheFlush(NULL);
    esp_restart();
    vTaskDelete(NULL);
    return;

cleanup:
    IOT_LOGE(TAG, "OTA failed");
    s_otaState.statusCode = IOT_OTA_STATUS_FAILURE;
    s_otaState.statusData = IOT_OTA_ERR_GENERAL;
    if (progressCb != NULL)
    {
        progressCb(IOT_OTA_STATUS_FAILURE, 0, params->user_data);
    }
    SAFE_FREE(params->url);
    SAFE_FREE(params->cert);
    SAFE_FREE(params->authToken);
    SAFE_FREE(params);
    vTaskDelete(NULL);
}

iot_err_t IOT_ApplicationRestartChip(void)
{
    IOT_LOGI(TAG, "Restarting chip...");
    /* Same reason as the OTA path: any dirty write-back cache entry is lost across
     * esp_restart(). Every controlled reboot must flush; only an uncontrolled power cut
     * is allowed to lose the last flush window. */
    IOT_CacheFlush(NULL);
    esp_restart();
    return IOT_OK;
}

iot_err_t IOT_AppManagerGetVersion(char *versionBuffer, size_t bufferLen)
{
    if (versionBuffer == NULL || bufferLen == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }

    const esp_partition_t *runningPartition = esp_ota_get_running_partition();
    esp_app_desc_t runningAppInfo;
    if (esp_ota_get_partition_description(runningPartition, &runningAppInfo) == ESP_OK)
    {
    }
    else
    {
        IOT_LOGE(TAG, "Failed to get running partition description");
        return IOT_ERR_FAIL;
    }
    strncpy(versionBuffer, runningAppInfo.version, bufferLen - 1);
    versionBuffer[bufferLen - 1] = '\0';
    return IOT_OK;
}

iot_err_t IOT_AppManagerGetOtaState(IOT_OtaState_t *state)
{
    if (state == NULL)
        return IOT_ERR_INVALID_ARG;
    *state = s_otaState;
    return IOT_OK;
}

iot_err_t IOT_AppManagerGetResetReason(IOT_ResetReason_t *out)
{
    if (out == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    switch (esp_reset_reason())
    {
    case ESP_RST_POWERON:
    case ESP_RST_EXT:
        *out = IOT_RESET_REASON_POWER_ON;
        break;
    case ESP_RST_SW:
    case ESP_RST_DEEPSLEEP:
        *out = IOT_RESET_REASON_SOFTWARE;
        break;
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        *out = IOT_RESET_REASON_CRASH;
        break;
    case ESP_RST_BROWNOUT:
        /* Kept apart from CRASH deliberately: this is the supply failing, and the
         * only remedy the boot guard has is erasing user data, which cannot help. */
        *out = IOT_RESET_REASON_BROWNOUT;
        break;
    default:
        *out = IOT_RESET_REASON_UNKNOWN;
        break;
    }
    return IOT_OK;
}

iot_err_t IOT_AppManagerMarkAppValid(void)
{
    /* Returns ESP_ERR_NOT_SUPPORTED when the build has no rollback enabled, and
     * ESP_ERR_INVALID_STATE when the image is not pending verification (an ordinary
     * boot). Neither is a failure worth reporting — only a genuine write error is. */
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK)
    {
        return IOT_OK;
    }
    if (err == ESP_ERR_NOT_SUPPORTED || err == ESP_ERR_INVALID_STATE)
    {
        return IOT_ERR_NOT_SUPPORTED;
    }
    IOT_LOGE(TAG, "Failed to confirm running image: %s", esp_err_to_name(err));
    return IOT_ERR_FAIL;
}

iot_err_t IOT_AppManagerStartUpdateProcess(const char *updateUrl, const char *cert, size_t certLen,
                                           const char *authToken, size_t authTokenLen,
                                           void *user_data, IOT_OtaProgressCb_t progressCb)
{
    if (updateUrl == NULL)
    {
        IOT_LOGE(TAG, "OTA URL is NULL");
        return IOT_ERR_INVALID_ARG;
    }

    ota_task_params_t *params = (ota_task_params_t *)Mem_SafeMalloc(sizeof(ota_task_params_t), TAG, "ota task params");
    if (params == NULL)
    {
        IOT_LOGE(TAG, "Failed to allocate OTA params");
        return IOT_ERR_NO_MEM;
    }

    params->url = strdup(updateUrl);
    if (params->url == NULL)
    {
        SAFE_FREE(params);
        return IOT_ERR_NO_MEM;
    }

#ifdef IOT_OTA_TRACE_CERT
    if (cert != NULL && certLen > 0)
    {
        IOT_LOGW(TAG, "IOT_OTA_TRACE_CERT enabled; dumping OTA cert PEM");
        for (size_t i = 0; i < certLen; i++)
        {
            printf("%c", cert[i]);
        }
        printf("\n");
    }
#endif

    params->cert = NULL;
    params->certLen = 0;
    if (cert != NULL && certLen > 0)
    {
        params->cert = (char *)Mem_SafeMalloc(certLen + 1, TAG, "ota cert");
        if (params->cert == NULL)
        {
            SAFE_FREE(params->url);
            SAFE_FREE(params);
            return IOT_ERR_NO_MEM;
        }
        memcpy(params->cert, cert, certLen);
        params->cert[certLen] = '\0';
        params->certLen = certLen;
    }

    params->authToken = NULL;
    params->authTokenLen = 0;
    if (authToken != NULL && authTokenLen > 0)
    {
        params->authToken = (char *)Mem_SafeMalloc(authTokenLen + 1, TAG, "ota auth token");
        if (params->authToken == NULL)
        {
            SAFE_FREE(params->url);
            SAFE_FREE(params->cert);
            SAFE_FREE(params);
            return IOT_ERR_NO_MEM;
        }
        memcpy(params->authToken, authToken, authTokenLen);
        params->authToken[authTokenLen] = '\0';
        params->authTokenLen = authTokenLen;
    }

    params->user_data = user_data;
    params->progressCb = progressCb;

    BaseType_t ret = xTaskCreate(ota_update_task, "ota_task", 8192, params, 5, NULL);
    if (ret != pdPASS)
    {
        IOT_LOGE(TAG, "Failed to create OTA task");
        SAFE_FREE(params->url);
        SAFE_FREE(params->cert);
        SAFE_FREE(params->authToken);
        SAFE_FREE(params);
        return IOT_ERR_FAIL;
    }

    IOT_LOGI(TAG, "OTA task created");
    return IOT_OK;
}
