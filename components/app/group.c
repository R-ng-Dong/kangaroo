#include "group.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

#define GROUP_NVS_NAMESPACE "group_ns"
#define KEY_CONFIG_GROUP "group_key"

static DaoGroup s_groups[MAX_NUM_GROUPS] = {0};

static _bool GroupSave(const DaoGroup *config)
{
    nvs_handle_t handle;
    if (nvs_open(GROUP_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return D_FALSE;

    esp_err_t err = nvs_set_blob(handle, KEY_CONFIG_GROUP, config, sizeof(*config));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return (err == ESP_OK) ? D_TRUE : D_FALSE;
}

void GroupInit(void)
{
    nvs_handle_t handle;
    if (nvs_open(GROUP_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return;

    DaoGroup groups[MAX_NUM_GROUPS];
    size_t group_size = sizeof(groups);
    esp_err_t err = nvs_get_blob(handle, KEY_CONFIG_GROUP, groups, &group_size);
    if (err == ESP_OK && group_size == sizeof(groups))
    {
        memcpy(s_groups, groups, group_size);
    }
    nvs_close(handle);
}

DaoGroup* GroupGetById(UI16 group_id)
{
    for (int i = 0; i < MAX_NUM_GROUPS; i++)
    {
        if (s_groups[i].is_used && s_groups[i].group_id == group_id)
        {
            return &s_groups[i];
        }
    }
    return NULL;
}

_bool GroupAdd(UI16 group_id)
{
    for (int i = 0; i < MAX_NUM_GROUPS; i++)
    {
        if (!s_groups[i].is_used)
        {
            s_groups[i].group_id = group_id;
            s_groups[i].is_used = 1;
            //save to DAO
            return GroupSave(s_groups);
        }
    }
    return D_FALSE; // No available slot
}

_bool GroupDelete(UI16 group_id)
{
    for (int i = 0; i < MAX_NUM_GROUPS; i++)
    {
        if (s_groups[i].is_used && s_groups[i].group_id == group_id)
        {
            s_groups[i].group_id = 0x0000;
            s_groups[i].is_used = 0;
            //save to DAO
            return GroupSave(s_groups);
        }
    }
    return D_FALSE; 
}

_bool GroupResetAll(void)
{
    nvs_handle_t handle;
    if (nvs_open(GROUP_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return D_FALSE;
    esp_err_t err = nvs_erase_key(handle, KEY_CONFIG_GROUP); 
    if(err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW("GROUP", "Key %s not found in namespace %s (nothing to erase)", KEY_CONFIG_GROUP, GROUP_NVS_NAMESPACE);
        nvs_close(handle);
        return D_FALSE;
    }
    if(err != ESP_OK)
    {
        ESP_LOGE("GROUP", "Error erasing key %s: %s", KEY_CONFIG_GROUP, esp_err_to_name(err));
        nvs_close(handle);
        return D_FALSE;  
    }
    err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK ? D_TRUE : D_FALSE;
}
