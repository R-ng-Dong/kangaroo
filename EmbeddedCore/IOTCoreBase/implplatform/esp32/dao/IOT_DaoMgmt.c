#include "dao/IOT_DaoMgmt.h"
#include "dao_common.h"
#include "dao/IOT_Cached.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "IOT_Log.h"

static const char *TAG_MGMT = "DAO-MGMT";

// ============================================================================
// Platform-owned: storage infrastructure init/deinit
// SDK-owned DAO inits are called by IOT_SDKInit() in rogosdk.
// ============================================================================

iot_err_t IOT_DaoInit(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        err = nvs_flash_erase();
        if (err != ESP_OK)
        {
            return IOT_ERR_FAIL;
        }
        err = nvs_flash_init();
    }
    err = DAO_InitStorage();
    if (err != ESP_OK)
    {
        return IOT_ERR_FAIL;
    }
    err = IOT_CacheInit();
    if (err != ESP_OK)
    {
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}

iot_err_t IOT_DaoDeinit(void)
{
    esp_err_t err = IOT_CacheDeinit();
    if (err != ESP_OK)
    {
        return IOT_ERR_FAIL;
    }
    err = DAO_DeinitStorage();
    if (err != ESP_OK)
    {
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}

iot_err_t IOT_DaoGetStorageStats(uint32_t *usedEntries, uint32_t *totalEntries)
{
    if (usedEntries == NULL || totalEntries == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    nvs_stats_t stats;
    esp_err_t err = nvs_get_stats(NULL, &stats);
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG_MGMT, "Failed to get NVS stats: %s", esp_err_to_name(err));
        return IOT_ERR_FAIL;
    }

    *usedEntries = stats.used_entries;
    *totalEntries = stats.total_entries;
    return IOT_OK;
}
