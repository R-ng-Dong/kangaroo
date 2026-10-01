#include "cloud_event_handler.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"
#include "IOT_Core.h"

static const char *TAG = "DEVCIE";
static esp_timer_handle_t device_reset_timer_handle = NULL;

static void periodicCb(void *arg){
    //TODO
    if(IOT_CoreIsProvisioned())
    {
        root_device_factory_reset();
    }
    esp_timer_delete(device_reset_timer_handle);
    device_reset_timer_handle = NULL;
}
esp_err_t device_reset_timer_start(uint32_t interval_ms)
{
    return ESP_OK;
    // ESP_LOGW(TAG, "Start timer");
    if(device_reset_timer_handle != NULL)
    {
        return ESP_OK;
    }
    esp_timer_create_args_t dev_reset_timer = {0};
    dev_reset_timer.arg = NULL;
    dev_reset_timer.callback = periodicCb;
    dev_reset_timer.dispatch_method = ESP_TIMER_TASK;
    dev_reset_timer.name = "device_reset";
    esp_err_t err = esp_timer_create(&dev_reset_timer, &device_reset_timer_handle);
    if(err != ESP_OK)
        return err;
    return esp_timer_start_once(device_reset_timer_handle, interval_ms * 1000U);
}

esp_err_t device_reset_timer_stop(void)
{
    if(!device_reset_timer_handle) return ESP_OK;
    // ESP_LOGW(TAG, "Stop timer!");
    if(esp_timer_stop(device_reset_timer_handle) == ESP_OK)
    {
        return esp_timer_delete(device_reset_timer_handle);
    }
    return ESP_FAIL;
}