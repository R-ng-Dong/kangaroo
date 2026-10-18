#include "event_manager.h"
#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "esp_event.h"


ESP_EVENT_DEFINE_BASE(EVENT_MANAGER_BASE);
static esp_event_loop_handle_t s_event_loop = NULL;
static EventManagerCallback s_event_cb = NULL;

static void DummyHandler(void *arg, esp_event_base_t event_base, SI32 event_id, void *data)
{
    if(event_base == EVENT_MANAGER_BASE)
    {
        if(s_event_cb) s_event_cb((EventID)event_id, data);
    }
}

_bool EventManagerInit(EventManagerCallback cb)
{
    esp_event_loop_args_t loop_args = {
        .queue_size = 5,
        .task_name = "ral_event_loop",
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = 2048*2,
        .task_core_id = tskNO_AFFINITY
    };
    if(cb) s_event_cb = cb;
    esp_err_t err = esp_event_loop_create(&loop_args, &s_event_loop);
    if(err != ESP_OK) return D_FALSE;
    err = esp_event_handler_register_with(s_event_loop, EVENT_MANAGER_BASE, ESP_EVENT_ANY_ID, DummyHandler, NULL);    
    return err == ESP_OK ? D_TRUE : D_FALSE;
}

_bool EventManagerPost(EventID event_id, const void *event_data, UI16 event_data_size)
{
    if(!s_event_loop) return D_FALSE;
    esp_err_t err = esp_event_post_to(s_event_loop, EVENT_MANAGER_BASE, (SI32)event_id, event_data, event_data_size, pdMS_TO_TICKS(100));
    return err == ESP_OK ? D_TRUE : D_FALSE;
}