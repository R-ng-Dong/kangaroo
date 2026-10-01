#include "mesh/IOT_MeshMgmt.h"
#include "mesh/IOT_MeshControl.h"
#include "mesh/IOT_MeshDriver.h"
#include "IOT_Log.h"

static const char *TAG = "MESH_DRV";

static const IOT_MeshDriver_t *s_meshDriver = NULL;

void IOT_MeshRegisterDriver(const IOT_MeshDriver_t *driver)
{
    s_meshDriver = driver;
    IOT_LOGI(TAG, "Mesh driver registered");
}

// ============================================================================
// Lifecycle
// ============================================================================

iot_err_t IOT_MeshInit(void)
{
    if (s_meshDriver == NULL || s_meshDriver->init == NULL)
    {
        IOT_LOGD(TAG, "No mesh driver registered");
        return IOT_ERR_NOT_SUPPORTED;
    }
    return s_meshDriver->init();
}

iot_err_t IOT_MeshDeinit(void)
{
    if (s_meshDriver == NULL || s_meshDriver->deinit == NULL)
    {
        return IOT_ERR_NOT_SUPPORTED;
    }
    return s_meshDriver->deinit();
}

bool IOT_MeshIsProvisioned(void)
{
    if (s_meshDriver == NULL || s_meshDriver->isProvisioned == NULL)
    {
        return false;
    }
    return s_meshDriver->isProvisioned();
}

// ============================================================================
// Network
// ============================================================================

iot_err_t IOT_MeshJoinNetwork(const IOT_MeshCredentials_t *credentials)
{
    if (s_meshDriver == NULL || s_meshDriver->joinNetwork == NULL)
    {
        IOT_LOGD(TAG, "No mesh driver registered");
        return IOT_ERR_NOT_SUPPORTED;
    }
    return s_meshDriver->joinNetwork(credentials);
}

iot_err_t IOT_MeshDeleteNode(uint16_t nodeAddr, const uint8_t *devKey)
{
    if (s_meshDriver == NULL || s_meshDriver->deleteNode == NULL)
    {
        return IOT_ERR_NOT_SUPPORTED;
    }
    return s_meshDriver->deleteNode(nodeAddr, devKey);
}

// ============================================================================
// Persistence
// ============================================================================

iot_err_t IOT_MeshSaveSequence(void)
{
    if (s_meshDriver == NULL || s_meshDriver->saveSequence == NULL)
    {
        return IOT_ERR_NOT_SUPPORTED;
    }
    return s_meshDriver->saveSequence();
}

// ============================================================================
// Node Cache
// ============================================================================

iot_err_t IOT_MeshSetNodeCache(const IOT_MeshNodeEntry_t *entries, uint8_t count)
{
    if (s_meshDriver == NULL || s_meshDriver->setNodeCache == NULL)
    {
        IOT_LOGD(TAG, "No mesh driver registered");
        return IOT_ERR_NOT_SUPPORTED;
    }
    return s_meshDriver->setNodeCache(entries, count);
}

const uint8_t *IOT_MeshGetDevKey(uint16_t nodeAddr)
{
    if (s_meshDriver == NULL || s_meshDriver->getDevKey == NULL)
    {
        return NULL;
    }
    return s_meshDriver->getDevKey(nodeAddr);
}

// ============================================================================
// Callbacks
// ============================================================================

iot_err_t IOT_MeshRegisterMessageCb(IOT_MeshMessageCb_t cb)
{
    if (s_meshDriver == NULL || s_meshDriver->registerMessageCb == NULL)
    {
        return IOT_ERR_NOT_SUPPORTED;
    }
    return s_meshDriver->registerMessageCb(cb);
}

iot_err_t IOT_MeshRegisterTopologyCb(IOT_MeshTopologyCb_t cb)
{
    if (s_meshDriver == NULL || s_meshDriver->registerTopologyCb == NULL)
    {
        return IOT_ERR_NOT_SUPPORTED;
    }
    return s_meshDriver->registerTopologyCb(cb);
}

iot_err_t IOT_MeshNotifyTopologyChange(const IOT_MeshTopologyEvent_t *event)
{
    if (s_meshDriver == NULL || s_meshDriver->notifyTopologyChange == NULL)
    {
        return IOT_ERR_NOT_SUPPORTED;
    }
    return s_meshDriver->notifyTopologyChange(event);
}

// ============================================================================
// Control (Send)
// ============================================================================

iot_err_t IOT_MeshSendOnOff(uint16_t nwkAddr, uint8_t onoff, uint16_t appIdx)
{
    if (s_meshDriver == NULL || s_meshDriver->sendOnOff == NULL)
    {
        IOT_LOGD(TAG, "No mesh driver registered");
        return IOT_ERR_NOT_SUPPORTED;
    }
    return s_meshDriver->sendOnOff(nwkAddr, onoff, appIdx);
}

iot_err_t IOT_MeshSendLightCTL(uint16_t nwkAddr, uint16_t lightness, uint16_t temperature, uint16_t appIdx)
{
    if (s_meshDriver == NULL || s_meshDriver->sendLightCTL == NULL)
    {
        return IOT_ERR_NOT_SUPPORTED;
    }
    return s_meshDriver->sendLightCTL(nwkAddr, lightness, temperature, appIdx);
}

iot_err_t IOT_MeshSendLightHSL(uint16_t nwkAddr, uint16_t hue, uint16_t saturation, uint16_t lightness,
                                uint16_t appIdx)
{
    if (s_meshDriver == NULL || s_meshDriver->sendLightHSL == NULL)
    {
        return IOT_ERR_NOT_SUPPORTED;
    }
    return s_meshDriver->sendLightHSL(nwkAddr, hue, saturation, lightness, appIdx);
}

iot_err_t IOT_MeshSendVendor(uint16_t deviceType, uint16_t nwkAddr, uint8_t *data, uint16_t dataLen)
{
    if (s_meshDriver == NULL || s_meshDriver->sendVendor == NULL)
    {
        return IOT_ERR_NOT_SUPPORTED;
    }
    return s_meshDriver->sendVendor(deviceType, nwkAddr, data, dataLen);
}
