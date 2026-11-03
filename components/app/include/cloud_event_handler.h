#ifndef _APP_DEVICE_H__
#define _APP_DEVICE_H__

#include "IOT_ErrorManager.h"
#include "common.h"

#define APP_SYNC_ON_OFF_SUBTYPE      71
#define APP_ATTR_SYNC_ONOFF_ELEMENTS 315    // value format: list of 2-byte element IDs

typedef void (*CloudUpdateStateCallback)(UI16 elemId, UI16 attrId, UI8 *attrValue, UI8 lenAttrValue);

extern uint8_t g_cloud_is_connected;

void RAL_DeinitBle(void);
iot_err_t DeviceRegisterEvent(void);
void RootDeviceFactoryReset(void);
void CloudRegisterUpdateStateCallback(CloudUpdateStateCallback cb); 

#endif /*  */
