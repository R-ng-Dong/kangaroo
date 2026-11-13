#include "IOT_Contract.h"
#include "IOT_ContractInternal.h"

#include <stddef.h>
#include <stdint.h>
#include "IOT_Common.h"
#include "IOT_Log.h"
#include "IOT_ContractStatus.h"
#include "IOT_ContractPending.h"
#include "IOT_Record.h"
#include "dao/IOT_DaoDeviceMgmt.h"
#include "dao/IOT_DaoGroupMgmt.h"
#include "dao/IOT_DaoIRMgmt.h"
#include "dao/IOT_DaoStoresInternal.h"
#include "dao/IOT_DaoRecord.h"
#include "dao/IOT_DaoSmartMgmt.h"
#include "dao/IOT_DaoSmartScheduleMgmt.h"
#include "dao/IOT_DaoSmartTriggerMgmt.h"
#include "services/IOT_ServiceSmart.h"
#include "services/IOT_SmartCondGroup.h"

static const char *TAG = "ContractLifecycle";

/* The product's history store, installed before start (IOT_ContractRecordUseCustomStore()). */
static IOT_ContractRecordAppendOverrideFn s_customStore;

iot_err_t IOT_ContractRecordUseCustomStore(IOT_ContractRecordAppendOverrideFn fn)
{
    s_customStore = fn;
    return IOT_OK;
}

iot_err_t IOT_ContractInit(const IOT_ContractConfig_t *config)
{
    (void) config;
    /* Set explicitly on every init, so a second init cannot keep a stale hook. The resolver is
     * the contract's own, installed once the product's elements are declared. The custom store
     * goes in before Record sets up, which skips its own history while one is installed. */
    (void) IOT_RecordSetElementIdResolver(NULL);
    (void) IOT_RecordSetAppendOverride(s_customStore);
    iot_err_t err = IOT_DaoDeviceInit();
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to init device DAO");
        return err;
    }
#if 0
    IOT_DaoSmartTriggerInit();
    IOT_DaoSmartScheduleInit();
    /* Create the condition-group lock while still single-threaded, before any task can
     * dispatch a condition-group callback. */
    SmartService_CondGroupInit();
    SmartService_CondAnswerPoolInit();
    /* The holding table's lock, for the same reason. */
    IOT_ContractPendingInit();
    /* And the device status's lock. */
    IOT_ContractStatusInit();
    /* Unconditional: a partner's Kconfig never declares an IR option, and the store costs nothing
     * until a product writes IR data. */
    IOT_DaoIRInit();
#endif
    IOT_RecordInit();
    return IOT_OK;
}

iot_err_t IOT_ContractRemoveDevice(uint16_t eid)
{
    iot_err_t removeRes = IOT_DaoDeviceRemoveDevice(eid);
    if (removeRes != IOT_OK)
        IOT_LOGE(TAG, "Failed to remove device with EID %04x", eid);

    iot_err_t groupRes = IOT_DaoGroupRemoveDeviceFromAllGroup(eid);
    if (groupRes != IOT_OK)
        IOT_LOGE(TAG, "Failed to remove device from all groups");

    iot_err_t smartRes = IOT_DaoSmartRemoveDeviceData(eid);
    if (smartRes != IOT_OK)
        IOT_LOGE(TAG, "Failed to remove device smart bind data");

    if (IOT_DaoSmartTriggerRemoveByEid(eid) != IOT_OK)
        IOT_LOGE(TAG, "Failed to remove device smart triggers");

    /* Record keeps a namespace per device. Without this a hub that pairs and unpairs
     * children leaks one namespace per child against the storage namespace ceiling. */
    if (IOT_DaoRecordRemoveDevice(eid) != IOT_OK)
        IOT_LOGE(TAG, "Failed to remove device record data");

    if (IOT_DaoIRRemoveDevice(eid) != IOT_OK)
        IOT_LOGE(TAG, "Failed to remove device IR data");

    return (removeRes != IOT_OK || groupRes != IOT_OK || smartRes != IOT_OK) ? IOT_ERR_FAIL : IOT_OK;
}

iot_err_t IOT_ContractFactoryReset(void)
{
    /* Collect the EID list BEFORE the Device store is wiped: the per-device stores are keyed by it. */
    uint16_t *eids = NULL;
    uint16_t eidCount = 0;
    IOT_DaoDeviceGetAllEids(&eids, &eidCount);

    /* Smart triggers give the SMID list for bind command cleanup. */
    {
        uint16_t *smids = NULL;
        uint16_t smidCount = 0;
        IOT_DaoSmartTriggerFactoryReset(&smids, &smidCount);
        IOT_DaoSmartFactoryReset(smids, smidCount);
        SAFE_FREE(smids);
    }
    /* Clear the in-RAM condition-group scoreboard alongside the persisted triggers. */
    SmartService_CondGroupReset();

    IOT_DaoSmartScheduleFactoryReset();

    IOT_DaoIRFactoryReset(eids, eidCount);

    for (uint16_t i = 0; i < eidCount; i++)
        IOT_DaoRecordRemoveDevice(eids[i]);
    SAFE_FREE(eids);

    IOT_DaoDeviceFactoryReset();
    IOT_DaoGroupFactoryReset();

    /* The device has no root any more (a root removal runs this reset too, and the product may not
     * restart): reports are held again until the next provisioning. */
    IOT_ContractPendingReset();
    return IOT_OK;
}

void IOT_ContractOnCloudConnected(void)
{
    /* Push kick coalesces and SmartService_SchedulerStart is idempotent, so a reconnect storm
     * adds neither timers nor queued work. */
    IOT_RecordPushKick();
    SmartService_SchedulerStart();
    SmartService_CondGroupSyncSchedule();
}

iot_err_t IOT_ContractPrepareRestart(void)
{
    /* Record first, then the Device store's element-state write-back cache. Both always run;
     * the first error is the one reported. */
    iot_err_t err = IOT_RecordFlushAllSync();
    iot_err_t devErr = IOT_DaoDeviceFlushCache();
    return err != IOT_OK ? err : devErr;
}
