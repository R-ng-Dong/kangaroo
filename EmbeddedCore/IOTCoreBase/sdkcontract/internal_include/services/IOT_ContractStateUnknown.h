/*
 * An attribute that is not kept across a power cut (IOT_AttrIsPersisted() false) reads as size 0
 * from the Device store until the product reports it: "unknown". Inside the device that stays
 * absent; on the wire it goes as the attribute's default, exactly as right after a join, so the
 * cloud and the app see the same message shape as before.
 */

#ifndef D81890CC_B61C_44A0_A768_E3D1ED91E87A
#define D81890CC_B61C_44A0_A768_E3D1ED91E87A
#ifndef IOT_CONTRACT_STATE_UNKNOWN_H
#define IOT_CONTRACT_STATE_UNKNOWN_H

#include "IOT_ErrorManager.h"
#include "dao/IOT_DaoDeviceMgmt.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Give every unknown, not-persisted attribute of @p state its table default (own attributes: the
 * registered default). Persisted attributes, empty or not, are left alone.
 *
 * Call it on a state read for the wire, just before handing it to core; never on a state that is
 * stored or used for a decision.
 *
 * @return IOT_OK; IOT_ERR_INVALID_ARG for NULL; IOT_ERR_NO_MEM, with @p state still freeable by
 *         IOT_DaoDeviceFreeElemsInfo().
 */
iot_err_t IOT_ContractStateFillUnknown(IOT_DaoDeviceElemsAttrsInfo_t *state);

#ifdef __cplusplus
}
#endif

#endif


#endif /* D81890CC_B61C_44A0_A768_E3D1ED91E87A */
