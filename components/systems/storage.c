#include "storage.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define CSV_PARTITION_LABEL "storage" // partition.csv
#define CSV_MOUNT_POINT "/storage"
#define CSV_PATH        CSV_MOUNT_POINT "/data.csv" // "/storage/data.csv"
#define CSV_RETENTION_SECONDS (24 * 3600) //24 hours
#define CSV_LINE_MAX 128

static const char *TAG = "storage_csv";
static SemaphoreHandle_t s_lock;
static bool s_mounted;

static esp_err_t RewriteFreshRecords(int64_t cutoff);

static esp_err_t EnsureMounted(void)
{
    if (s_mounted) return ESP_OK;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = CSV_MOUNT_POINT,
        .partition_label = CSV_PARTITION_LABEL,
        .max_files = 2,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE)
    {
        s_mounted = true;
        return ESP_OK;
    }
    ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
    return err;
}

_bool StorageInit(void)
{
    if (s_lock == NULL)
    {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) return D_FALSE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = EnsureMounted();
    if (err == ESP_OK)
    {
        FILE *file = fopen(CSV_PATH, "r");
        if (file == NULL)
        {
            file = fopen(CSV_PATH, "w");
            if (file == NULL) err = ESP_FAIL;
            else
            {
                if (fputs("timestamp,param,value\n", file) < 0) 
                    err = ESP_FAIL;
                if (fclose(file) != 0 && err == ESP_OK) 
                    err = ESP_FAIL;
            }
        }
        else fclose(file);
        /* Remove expired rows at boot, before new records need the available space. */
        time_t now = time(NULL);
        if (err == ESP_OK && now > 0)
            err = RewriteFreshRecords((int64_t)now - CSV_RETENTION_SECONDS);
    }
    xSemaphoreGive(s_lock);
    return (err == ESP_OK) ? D_TRUE : D_FALSE;
}

static _bool IsRecordFresh(const char *line, int64_t cutoff)
{
    char *end = NULL;
    long long timestamp = strtoll(line, &end, 10);
    return end != line && *end == ',' && timestamp >= cutoff;
}

static esp_err_t RewriteFreshRecords(int64_t cutoff)
{
    FILE *source = fopen(CSV_PATH, "r");
    if (source == NULL) 
        return ESP_FAIL;
    FILE *temp = fopen(CSV_MOUNT_POINT "/data.tmp", "w");
    if (temp == NULL) 
    { 
        fclose(source); 
        return ESP_FAIL; 
    }

    char line[CSV_LINE_MAX];
    esp_err_t err = ESP_OK;
    if (fgets(line, sizeof(line), source) == NULL || fputs("timestamp,param,value\n", temp) < 0)
        err = ESP_FAIL;
    while (err == ESP_OK && fgets(line, sizeof(line), source) != NULL)
    {
        if (IsRecordFresh(line, cutoff) && fputs(line, temp) < 0) err = ESP_FAIL;
    }
    if (ferror(source)) err = ESP_FAIL;
    if (fclose(source) != 0) err = ESP_FAIL;
    if (fclose(temp) != 0) err = ESP_FAIL;
    if (err == ESP_OK && rename(CSV_MOUNT_POINT "/data.tmp", CSV_PATH) != 0) err = ESP_FAIL;
    return err;
}

_bool StorageAppend(SI64 timestamp, const char *param, UI32 value)
{
    if (timestamp <= 0 || param == NULL || param[0] == '\0' ||
        strchr(param, ',') != NULL || strchr(param, '\n') != NULL || strchr(param, '\r') != NULL)
        return D_FALSE;//ESP_ERR_INVALID_ARG;
    if (s_lock == NULL)
    {
        if (StorageInit() != D_TRUE) return D_FALSE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = EnsureMounted();
    time_t now = time(NULL);
    if (err == ESP_OK && now > 0)
    {
        int64_t cutoff = (int64_t)now - CSV_RETENTION_SECONDS;
        if (timestamp < cutoff)
        {
            xSemaphoreGive(s_lock);
            return D_FALSE; //ESP_ERR_INVALID_ARG;
        }
        /* Compact first so expired history cannot prevent today's record being saved. */
        err = RewriteFreshRecords(cutoff);
    }
    if (err == ESP_OK)
    {
        FILE *file = fopen(CSV_PATH, "a");
        if (file == NULL) err = ESP_FAIL;
        else
        {
            if (fprintf(file, "%lld,%s,%lu\n", (long long)timestamp, param, (unsigned long)value) < 0) err = ESP_FAIL;
            if (fclose(file) != 0 && err == ESP_OK) err = ESP_FAIL;
        }
    }
    xSemaphoreGive(s_lock);
    return (err == ESP_OK) ? D_TRUE : D_FALSE;
}
