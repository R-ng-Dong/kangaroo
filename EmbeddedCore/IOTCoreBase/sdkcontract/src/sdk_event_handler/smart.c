#include "IOT_Contract.h"
#include "IOT_ContractInternal.h"
#include <string.h>
#include "IOT_Common.h"
#include "IOT_CoreReporter.h"
#include "IOT_Log.h"
#include "dao/IOT_DaoSmartMgmt.h"
#include "dao/IOT_DaoSmartScheduleMgmt.h"
#include "dao/IOT_DaoSmartTriggerMgmt.h"
#include "IOT_ContractAttrsInternal.h"
#include "services/IOT_ServiceSmart.h"
#include "services/IOT_SmartCondGroup.h"

static const char *TAG = "SDK_SMART";

iot_err_t IOT_ContractHandleSmartEvent(IOT_CoreSmartEvent_t *smartEvent)
{
    if (smartEvent == NULL)
    {
        IOT_LOGE(TAG, "Invalid smartEvent pointer");
        return IOT_ERR_INVALID_ARG;
    }

    iot_err_t iot_err = IOT_OK;

    switch (smartEvent->type)
    {
    case SMART_EVENT_REMOVE_ANNOUNCE:
    {
        IOT_CoreSmartEventRemoveAnnounceData_t *data = &smartEvent->data.removeAnnounce;
        IOT_LOGI(TAG, "SMART event: REMOVE_ANNOUNCE, SMID=%04x", data->smid);
        SmartService_NoteAutomationChanged(data->smid); // its waiting actions are dropped
        iot_err = IOT_DaoSmartRemoveSmid(data->smid);
        IOT_DaoSmartTriggerRemove(data->smid);
        IOT_DaoSmartScheduleRemove(data->smid);
        SmartService_CondGroupRemove(data->smid);
        break;
    }
    case SMART_EVENT_ACTIVE_BY_USER:
    {
        IOT_CoreSmartEventActiveByUserData_t *data = &smartEvent->data.activeByUser;
        IOT_LOGI(TAG, "SMART event: ACTIVE_BY_USER, SMID=%04x", data->smid);
        iot_err = SmartService_ExecuteCommands(data->smid);
        // ACTIVE_BY_USER (running a scene) is broadcast to every device in the account, so
        // most devices have no bind-cmds for this SMID — a benign miss, not a handler
        // failure (mirrors the ENABLE_DISABLE precedent below and the CHECK_SYNC "not found
        // locally" warning). Escalating IOT_ERR_NOT_FOUND would report a handler failure
        // from every non-owning device on every scene run.
        if (iot_err == IOT_ERR_NOT_FOUND)
            iot_err = IOT_OK;
        break;
    }
    case SMART_EVENT_ACTIVE_BY_SCHEDULE:
    {
        IOT_CoreSmartEventActiveByScheduleData_t *data = &smartEvent->data.activeBySchedule;
        IOT_LOGI(TAG,
                 "SMART event: ACTIVE_BY_SCHEDULE, SMID=%04x, MinuteOfWeek=%u",
                 data->smid, data->minuteOfWeekScheduleUTC0);

        /* Store schedule time for offline execution (dedup handled by DAO).
         * Every value 0..10079 is a legal minute-of-week, INCLUDING 0 — that is Sunday
         * 00:00 UTC. Skipping 0 would leave a schedule due then unpersisted, so it would
         * never fire with the WAN down. */
        IOT_LOGI(TAG, "  Storing schedule time for offline execution");
        IOT_DaoSmartScheduleSet(data->smid, data->minuteOfWeekScheduleUTC0);
        // Start scheduler if not already running
        SmartService_SchedulerStart();

        // Deduped against the offline scheduler tick, which independently matches this
        // same minute-of-week against the stored schedule within the next 60 s.
        iot_err = SmartService_ExecuteSchedule(data->smid, data->minuteOfWeekScheduleUTC0);
        break;
    }
    case SMART_EVENT_ACTIVE_BY_TRIGGER:
    {
        IOT_CoreSmartEventActiveByTriggerData_t *data = &smartEvent->data.activeByTrigger;
        IOT_LOGI(TAG,
                 "SMART event: ACTIVE_BY_TRIGGER, SMID=%04x, TriggerType=%04x, ControlCount=%u",
                 data->smid, data->triggerType, data->controlCount);

        // A fire for this group arrived as a location-wide broadcast — cancel any pending
        // deferred elected fire of ours and record the time (suppression + dedup). Harmless
        // for groups we don't participate in (no scoreboard entry) or for our own echo.
        SmartService_CondGroupOnActiveByTrigger(data->smid, 0);

        // Run the bind-cmds at most once per dedup window. This covers both the cloud/own
        // echo AND a duplicate ACTIVE_BY_TRIGGER from a second cond-group device that fired
        // in the cond-group convergence race (protects non-idempotent actions). Cross-device (no
        // bind cmds here) returns OK — normal for a location broadcast.
        iot_err = SmartService_ExecuteTriggerDeduped(data->smid,
                                                     data->attrValue.attrValue, data->attrValue.attrSize);
        break;
    }
    case SMART_EVENT_ACTIVE_BY_SYNC_ON_OFF:
    {
        IOT_CoreSmartEventActiveBySyncOnOffData_t *data = &smartEvent->data.activeBySyncOnOff;
        IOT_LOGI(TAG, "SMART event: ACTIVE_BY_SYNC_ON_OFF, SMID=%04x, attr=%04x, senderEid=%04x",
                 data->smid, data->attrValue.attrId, data->senderEid);
        iot_err = SmartService_ApplySyncOnOff(data->smid, data->attrValue.attrId,
                                              data->attrValue.attrValue, data->attrValue.attrSize,
                                              data->senderEid);
        break;
    }
    case SMART_EVENT_REPORT_COND_STATE:
    {
        IOT_CoreSmartEventReportCondStateData_t *data = &smartEvent->data.reportCondState;
        IOT_LOGI(TAG, "SMART event: REPORT_COND_STATE, SMID=%04x v=%u idx=%u mask=%02x root=%04x",
                 data->smid, data->version, data->condIndex, data->mask, data->senderRootEid);
        IOT_CoreSmartReportCondState_t rep = {
            .smid = data->smid,
            .version = data->version,
            .condCount = data->condCount,
            .fireCount = data->fireCount,
            .eid = data->eid,
            .condIndex = data->condIndex,
            .mask = data->mask,
            .departing = data->departing,
        };
        // A higher-version report means the config generation advanced (a condition
        // was added or removed, and the bind may have reached only the changed device).
        // Re-flood our own bit at the new generation so the full mask converges.
        if (SmartService_CondGroupOnPeerReport(&rep, data->senderRootEid))
            SmartService_CondGroupScheduleReannounce(rep.smid);
        break;
    }
    case SMART_EVENT_SYNC_COND_REQUEST:
    {
        IOT_CoreSmartEventSyncCondRequestData_t *data = &smartEvent->data.syncCondRequest;
        IOT_LOGI(TAG, "SMART event: SYNC_COND_REQUEST, %u group(s), requesterEid=%04x",
                 data->groupCount, data->requesterEid);
        SmartService_CondGroupHandleCheckQuery(data->groups, data->groupCount, data->requesterEid);
        break;
    }
    case SMART_EVENT_TRIGGER_UPDATE_ANNOUNCE:
    {
        IOT_CoreSmartEventTriggerUpdateAnnounceData_t *data = &smartEvent->data.triggerUpdateAnnounce;
        IOT_LOGI(TAG,
                 "SMART event: TRIGGER_UPDATE_ANNOUNCE, EID=%04x, RootEID=%04x, ElemID=%04x, ElemType=%04x",
                 data->eid, data->rootEid, data->elemId, data->elemType);

        // Load existing triggers for this EID and compare trigger keys
        uint16_t *smids = NULL;
        uint16_t smidCount = 0;
        iot_err_t getErr = IOT_DaoSmartTriggerGetByEid(data->eid, &smids, &smidCount);
        if (getErr == IOT_OK && smids != NULL)
        {
            bool found = false;
            for (uint16_t i = 0; i < smidCount; i++)
            {
                IOT_SmartTrigger_t trig;
                if (IOT_DaoSmartTriggerGet(smids[i], &trig) == IOT_OK)
                {
                    if (trig.triggerKeyOwnerEid == data->triggerKey.ownerEid &&
                        trig.triggerKeyOwnerEidCount == data->triggerKey.ownerEidCount &&
                        trig.triggerKeyExtEid == data->triggerKey.extEid &&
                        trig.triggerKeyExtEidCount == data->triggerKey.extEidCount)
                    {
                        found = true;
                        IOT_LOGI(TAG, "  Trigger key matches SMID=%04x", smids[i]);
                        break;
                    }
                }
            }
            if (!found)
            {
                IOT_LOGW(TAG, "  Announced trigger key not found locally for EID=%04x", data->eid);
            }
            SAFE_FREE(smids);
        }
        else
        {
            IOT_LOGW(TAG, "  No triggers stored for EID=%04x", data->eid);
        }
        break;
    }
    case SMART_EVENT_TRIGGER_MODE:
    {
        IOT_CoreSmartEventTriggerModeData_t *data = &smartEvent->data.triggerMode;
        IOT_LOGI(TAG, "SMART event: TRIGGER_MODE, Type=%d, Mode=%02x", data->type, data->mode);

        bool enabled = (data->mode != 0);

        switch (data->type)
        {
        case IOT_CORE_SMART_TRIGGER_MODE_SET_TRIGGER:
        {
            uint16_t smid = data->data.setTrigger.smid;
            uint16_t minuteDisable = data->data.setTrigger.minuteDisable;
            IOT_LOGI(TAG, "  SET_TRIGGER: SMID=%04x, enabled=%d, minuteDisable=%u",
                     smid, enabled, minuteDisable);
            iot_err = IOT_DaoSmartTriggerSetEnabled(smid, enabled, minuteDisable);
            break;
        }
        case IOT_CORE_SMART_TRIGGER_MODE_SCHEDULE:
        {
            uint16_t smid = data->data.schedule.smid;
            IOT_LOGI(TAG, "  SCHEDULE: SMID=%04x, enabled=%d", smid, enabled);
            iot_err = IOT_DaoSmartScheduleSetEnabled(smid, enabled);
            break;
        }
        case IOT_CORE_SMART_TRIGGER_MODE_ENABLE_DISABLE_AUTOMATION:
        {
            uint16_t eid = data->data.enableDisableAutomation.eid;
            IOT_LOGI(TAG, "  ENABLE_DISABLE_AUTOMATION: EID=%04x, enabled=%d", eid, enabled);
            iot_err = IOT_DaoSmartTriggerSetEnabledByEid(eid, enabled);
            break;
        }
        case IOT_CORE_SMART_TRIGGER_MODE_ENABLE_DISABLE_ALL_AUTOMATION:
            IOT_LOGI(TAG, "  ENABLE_DISABLE_ALL_AUTOMATION: enabled=%d", enabled);
            iot_err = SmartService_SetEnabledAll(enabled);
            break;
        default:
            IOT_LOGW(TAG, "  Unknown trigger mode type: %d", data->type);
            break;
        }
        {
            IOT_CoreSmartCmdResponse_t rsp = {
                .cmdType = IOT_SMART_CMD_TRIGGER_MODE,
                .cfm     = 0,
                .result  = iot_err,
                .ctx     = data->ctx,
            };
            IOT_CoreRespondSmartCmd(&rsp);
        }
        break;
    }
    case SMART_EVENT_ENABLE_DISABLE:
    {
        IOT_CoreSmartEventEnableDisableData_t *data = &smartEvent->data.enableDisable;
        IOT_LOGI(TAG, "SMART event: ENABLE_DISABLE, SMID=%04x, enabled=%d", data->smid, data->enabled);
        iot_err = SmartService_SetEnabledBySmid(data->smid, data->enabled != 0);
        // Re-enabling a cond-group refreshes its (stale) mask via a re-sync.
        if (iot_err == IOT_OK)
            SmartService_CondGroupOnEnableChange(data->smid, data->enabled != 0);
        {
            /* The answer carries the SMID and this device's root EID, so the cloud knows who
             * answered. Hence .smid, which the other cmdTypes leave at 0. Core sends no
             * answer for the NOT_FOUND case (see below). */
            IOT_CoreSmartCmdResponse_t rsp = {
                .cmdType = IOT_SMART_CMD_ENABLE_DISABLE,
                .smid    = data->smid,
                .cfm     = 0,
                .result  = iot_err,
                .ctx     = data->ctx,
            };
            IOT_CoreRespondSmartCmd(&rsp);
        }
        // A device that does not own this SMID is a benign miss — a location-broadcast
        // enable/disable naturally reaches non-owners, and the SMID may have just been
        // unbound here. Core sends no answer for that case (one reply per command, not
        // one per device), and we do NOT propagate
        // it as a handler failure either (it would spam a spurious "Failed to handle
        // SMART event" error). Mirrors how SmartService_ApplySyncOnOff silently no-ops
        // for an SMID this device isn't a participant of.
        if (iot_err == IOT_ERR_NOT_FOUND)
            iot_err = IOT_OK;
        break;
    }
    case SMART_EVENT_BIND_TRIGGER:
    {
        IOT_CoreSmartEventBindTriggerData_t *data = &smartEvent->data.bindTrigger;
        IOT_LOGI(TAG,
                 "SMART event: BIND_TRIGGER, SMID=%04x, EID=%04x, cond=%u, attr=%04x",
                 data->bindTriggerElmInfo.smid, data->bindTriggerElmInfo.eid,
                 data->condition, data->primaryAttrValue.attrId);

        // Reject malformed cond-group (distributed AND/OR) binds before storing.
        if (!SmartCondGroupBindIsValid(data->hasCondGroup, data->condIndex,
                                       data->condCount, data->fireCount))
        {
            // condIndex is 0-based (it is the bit position in the 1-byte group mask), so a
            // sender numbering seats 1..N gets its last seat rejected here — spell that out
            // rather than leaving the reader to work out why idx=N is invalid.
            IOT_LOGE(TAG,
                     "BIND_TRIGGER: invalid cond-group params (version=%u idx=%u N=%u K=%u), "
                     "rejecting SMID=%04x — need 2<=N<=8, 1<=K<=N, and condIndex 0..N-1 (0-based)",
                     data->condVersion, data->condIndex, data->condCount, data->fireCount,
                     data->bindTriggerElmInfo.smid);
            IOT_CoreSmartCmdResponse_t rsp = {
                .cmdType = IOT_SMART_CMD_BIND_TRIGGER,
                .cfm     = 0,
                .result  = IOT_ERR_INVALID_ARG,
                .ctx     = data->ctx,
            };
            IOT_CoreRespondSmartCmd(&rsp);
            break;
        }

        // Accepted, but the condition-group description declares fewer than 2 seats, so this
        // is NOT a distributed group — it runs as an ordinary single-seat trigger. N==1 is a
        // legitimate single-condition automation; N==0 means the condition-group description
        // declares no seat count; running as an ordinary trigger. Warn loudly on N==0 so a
        // sender that does this on every bind is greppable rather than silent.
        if (data->hasCondGroup && data->condCount == 0)
        {
            IOT_LOGW(TAG,
                     "BIND_TRIGGER SMID=%04x: condition-group description declares no seat "
                     "count; running as an ordinary trigger",
                     data->bindTriggerElmInfo.smid);
        }

        IOT_SmartTrigger_t trigger;
        memset(&trigger, 0, sizeof(trigger));

        // Device-owned cfm: start at 1 on first save, increment on each update.
        // The cloud-sent cfm (in bindTriggerElmInfo.cfm) is intentionally ignored.
        {
            IOT_SmartTrigger_t existing;
            if (IOT_DaoSmartTriggerGet(data->bindTriggerElmInfo.smid, &existing) == IOT_OK)
            {
                trigger.cfm = existing.cfm + 1;
            }
            else
            {
                trigger.cfm = 1;
            }
        }

        // Identity
        trigger.smid = data->bindTriggerElmInfo.smid;
        trigger.eid = data->bindTriggerElmInfo.eid;

        // The trigger element info is authoritative for the condition
        trigger.type = data->bindTriggerElmInfo.type;
        trigger.smartType = data->bindTriggerElmInfo.smartType;
        trigger.subSmartType = data->bindTriggerElmInfo.subSmartType;
        trigger.elmIdInfo = data->bindTriggerElmInfo.elmId;
        trigger.conditionInfo = data->bindTriggerElmInfo.condition;
        // cfm is device-owned (set above, before this block)

        // The trigger element info is authoritative for the condition; the extended info
        // supplies only a non-zero element id (for element-specific targeting).
        trigger.elmId = (data->elmId != 0) ? data->elmId : data->bindTriggerElmInfo.elmId;
        trigger.condition = data->bindTriggerElmInfo.condition;

        // Primary attribute
        trigger.attrId = data->primaryAttrValue.attrId;
        trigger.attrSize = data->primaryAttrValue.attrSize;
        if (trigger.attrSize > sizeof(trigger.attrValue))
        {
            trigger.attrSize = sizeof(trigger.attrValue);
        }
        if (data->primaryAttrValue.attrValue != NULL && trigger.attrSize > 0)
        {
            Mem_SafeCopy(trigger.attrValue, sizeof(trigger.attrValue),
                         data->primaryAttrValue.attrValue, trigger.attrSize);
        }

        // Sync-on-off (subSmartType 71): the primary attr-value carries the
        // element LIST (attrId IOT_ATTR_SYNC_ONOFF_ELEMENTS 315), not a match
        // value. Parse it into elemList and set the real match attr to ONOFF; the
        // "IN" condition is element membership, evaluated from elemList, so no
        // packed match value is stored (attrValue cleared).
        if (trigger.subSmartType == SMART_SUB_TYPE_SYNC_ON_OFF)
        {
            // Parse into a local aligned buffer, then memcpy into the packed
            // struct (avoids an unaligned uint16_t store into elemList).
            uint16_t parsed[SMART_TRIGGER_ELM_CAP];
            uint8_t n = SmartService_ParseSyncOnOffElements(data->primaryAttrValue.attrValue,
                                                            data->primaryAttrValue.attrSize, parsed,
                                                            SMART_TRIGGER_ELM_CAP);
            trigger.elmNum = n;
            memcpy(trigger.elemList, parsed, n * sizeof(uint16_t));
            if (n > 0)
            {
                trigger.elmId = parsed[0];
            }
            trigger.attrId = IOT_ATTR_ONOFF;
            trigger.attrSize = 0;
            memset(trigger.attrValue, 0, sizeof(trigger.attrValue));
        }

        // Secondary attribute (for COND_BETWEEN)
        trigger.secondaryAttrSize = data->secondaryAttrValue.attrSize;
        if (trigger.secondaryAttrSize > sizeof(trigger.secondaryAttrValue))
        {
            trigger.secondaryAttrSize = sizeof(trigger.secondaryAttrValue);
        }
        if (data->secondaryAttrValue.attrValue != NULL && trigger.secondaryAttrSize > 0)
        {
            Mem_SafeCopy(trigger.secondaryAttrValue, sizeof(trigger.secondaryAttrValue),
                         data->secondaryAttrValue.attrValue, trigger.secondaryAttrSize);
        }

        // Time config
        trigger.timCfgType = data->timeConfigMix.timCfgType;
        trigger.timCfgValue = data->timeConfigMix.timCfgValue;
        trigger.timeStart = data->timeStart;
        trigger.timeStop = data->timeStop;
        trigger.weekDay = data->weekDay;

        // Trigger key
        trigger.triggerKeyOwnerEid = data->bindTriggerKey.ownerEid;
        trigger.triggerKeyOwnerEidCount = data->bindTriggerKey.ownerEidCount;
        trigger.triggerKeyExtEid = data->bindTriggerKey.extEid;
        trigger.triggerKeyExtEidCount = data->bindTriggerKey.extEidCount;

        // Mix
        trigger.eidMix = data->eidMix;
        trigger.rootEidMix = data->rootEidMix;
        trigger.elemIdMix = data->elemIdMix;

        // Time window reference frame: isDeviceUtc=1 means timeStart/timeStop/weekDay
        // are UTC0 and evaluated against UTC time; 0 means local (device TZ).
        trigger.isDeviceUtc = data->isDeviceUtc ? 1 : 0;
        // Reserved/unused — UTC evaluation needs no offset. Kept for struct/NVS stability.
        trigger.timeZoneQuarterHours = (int16_t)(data->timeZoneFraction * 4.0f);

        // Cond-group (distributed AND/OR, subSmartType 2048) — validated above
        trigger.condIndex = data->condIndex;
        trigger.condCount = data->condCount;
        trigger.fireCount = data->fireCount;
        trigger.condVersion = data->condVersion;

        // Default enabled
        trigger.enabled = 1;
        trigger.minuteDisable = 0;

        /* The trigger store is keyed (smid, condIndex), and a stair-switch bind carries no
         * cond-group block, so condIndex is 0 for all of them. Two stair binds under one
         * SMID would therefore collide and the second would silently replace the first,
         * losing half the pairing.
         *
         * A stair-switch pairing normally spans two devices, each receiving exactly one
         * bind, so this collision is not expected — but nothing on the device guarantees it.
         *
         * Warn rather than refuse. Refusing would break a legitimate edit that repoints a
         * pairing at a different element, and a broken edit is worse than a collision. The
         * warning makes the case diagnosable if it ever does occur. */
        if (trigger.subSmartType == SMART_SUB_TYPE_STAIR_SWITCH)
        {
            IOT_SmartTrigger_t existing;
            if (IOT_DaoSmartTriggerGet(trigger.smid, &existing) == IOT_OK &&
                existing.subSmartType == SMART_SUB_TYPE_STAIR_SWITCH &&
                existing.elmId != trigger.elmId)
            {
                IOT_LOGW(TAG,
                         "  SMID=%04x: second stair-switch bind for elm=%04x replaces elm=%04x "
                         "(same storage key) -- one half of the pairing is being lost",
                         trigger.smid, trigger.elmId, existing.elmId);
            }
        }

        SmartService_NoteAutomationChanged(trigger.smid); // An action the old version scheduled must not be applied to the new one.
        iot_err = IOT_DaoSmartTriggerSet(trigger.smid, &trigger);
        if (iot_err == IOT_OK)
        {
            IOT_LOGI(TAG,
                     "  Trigger stored successfully: timeStart=%u timeStop=%u weekDay=0x%02x timCfg={type=%u,value=%u} "
                     "isDeviceUtc=%u (%s window)",
                     trigger.timeStart, trigger.timeStop, trigger.weekDay, trigger.timCfgType,
                     trigger.timCfgValue, trigger.isDeviceUtc, trigger.isDeviceUtc ? "UTC" : "local");
            // Drop any stale scoreboard mask so the (re)bound group rebuilds fresh
            // (lazily on the next flip / peer state / boot re-sync).
            SmartService_CondGroupRemove(trigger.smid);
            // A config change (add/remove of a condition) may arrive as a bind to THIS
            // device alone. Proactively re-flood our own bit at the (possibly new)
            // version so peers pick up the new N/K/version without waiting for a
            // natural flip. Jittered; never fires.
            if (SmartTriggerIsCondGroup(&trigger))
                SmartService_CondGroupScheduleReannounce(trigger.smid);
        }
        else
        {
            IOT_LOGE(TAG, "  Failed to store trigger: %d", iot_err);
        }
        {
            IOT_CoreSmartCmdResponse_t rsp = {
                .cmdType = IOT_SMART_CMD_BIND_TRIGGER,
                .cfm     = trigger.cfm,  // device-owned counter, not cloud-sent value
                .result  = iot_err,
                .ctx     = data->ctx,
            };
            IOT_CoreRespondSmartCmd(&rsp);
        }
        break;
    }
    case SMART_EVENT_COND_NOT_MEMBER:
    {
        IOT_CoreSmartEventCondNotMemberData_t *data = &smartEvent->data.condNotMember;
        IOT_LOGI(TAG, "SMART event: COND_NOT_MEMBER, SMID=%04x from eid=%04x",
                 data->smid, data->responderEid);
        /* Only an EXPLICIT answer may retire a seat; the service itself checks that we
         * actually asked and that exactly one seat was in question. */
        SmartService_CondGroupOnNotMember(data->smid, data->responderEid);
        break;
    }

    case SMART_EVENT_UNBOUND_TRIGGER:
    {
        IOT_CoreSmartEventUnboundTriggerData_t *data = &smartEvent->data.unboundTrigger;
        IOT_LOGI(TAG, "SMART event: UNBOUND_TRIGGER, SMID=%04x", data->smid);
        SmartService_NoteAutomationChanged(data->smid); // its waiting actions are dropped
        /* Only the leaving device receives the unbind when a condition goes away, so the survivors
         * would keep the old N and wait forever on a seat that no longer exists (for example
         * a 3-of-3 that lost one member and the other two stayed at N=3 K=3). The
         * unbind carries the new shape, so relay it on the way out — BEFORE the trigger is
         * deleted, because the announcement speaks from our own seat. */
        if (data->hasCondGroup)
        {
            SmartService_CondGroupAnnounceRemoval(data->smid, data->condVersion,
                                                  data->condCount, data->fireCount);
        }
        iot_err = IOT_DaoSmartTriggerRemove(data->smid);
        SmartService_CondGroupRemove(data->smid);
        {
            IOT_CoreSmartCmdResponse_t rsp = {
                .cmdType = IOT_SMART_CMD_UNBOUND_TRIGGER,
                .cfm     = 0,
                .result  = iot_err,
                .ctx     = data->ctx,
            };
            IOT_CoreRespondSmartCmd(&rsp);
        }
        break;
    }
    case SMART_EVENT_SCHEDULE_UPDATE_ANNOUNCE:
    {
        IOT_CoreSmartEventScheduleUpdateAnnounceData_t *data = &smartEvent->data.scheduleUpdateAnnounce;
        IOT_LOGI(TAG, "SMART event: SCHEDULE_UPDATE_ANNOUNCE, SMID=%04x, timeCount=%u",
                 data->smid, data->scheduleTimeCount);

        /* Replace all schedule times for this SMID — clear old, store new. ClearTimes, not
         * Remove: Remove also deletes the "e<smid>" enable flag, and IsEnabled defaults to
         * true, so editing the times of a schedule the user switched OFF would switch it
         * back on. The automation still exists here; only its times are being replaced. */
        IOT_DaoSmartScheduleClearTimes(data->smid);

        for (uint16_t i = 0; i < data->scheduleTimeCount; i++)
        {
            IOT_DaoSmartScheduleSet(data->smid, data->scheduleTimes[i]);
        }

        if (data->scheduleTimeCount > 0)
        {
            SmartService_SchedulerStart();
        }
        break;
    }
    case SMART_EVENT_SCHEDULE_REMOVE_ANNOUNCE:
    {
        IOT_CoreSmartEventScheduleRemoveAnnounceData_t *data = &smartEvent->data.scheduleRemoveAnnounce;
        IOT_LOGI(TAG, "SMART event: SCHEDULE_REMOVE_ANNOUNCE, SMID=%04x", data->smid);
        iot_err = IOT_DaoSmartScheduleRemove(data->smid);
        break;
    }
    case SMART_EVENT_BIND_CMD:
    {
        IOT_CoreSmartEventBindCmdData_t *data = &smartEvent->data.bindCmd;
        IOT_LOGI(TAG,
                 "SMART event: BIND_CMD, SMID=%04x, EID=%04x, RootEID=%04x, Protocol=%04x, CmdSize=%u",
                 data->smid, data->eid, data->rootEid, data->prtc, data->cmdSize);

        if (data->attrValueList == NULL || data->cmdSize == 0)
        {
            IOT_LOGW(TAG, "BIND_CMD: no commands to store");
            break;
        }

        // Log command details
        for (uint16_t i = 0; i < data->cmdSize; i++)
        {
            IOT_CoreSmartBlockAttrValueReverseDelayElm_t *item = &data->attrValueList[i];
            IOT_LOGI(TAG, "  Cmd[%u]: attrId=%04x, attrSize=%u, reverse=%04x, delay=%u, elmId=%04x",
                     i, item->attrValue.attrId, item->attrValue.attrSize,
                     item->reverse, item->delay, item->elmId);
        }

        uint16_t bindCmdCfm = 1;
        SmartService_NoteAutomationChanged(data->smid); // An action the old version scheduled must not be applied to the new one.
        iot_err = SmartService_StoreBindCmd(data, &bindCmdCfm);
        {
            IOT_CoreSmartCmdResponse_t rsp = {
                .cmdType = IOT_SMART_CMD_BIND_CMD,
                .cfm     = bindCmdCfm,
                .result  = iot_err,
                .ctx     = data->ctx,
            };
            IOT_CoreRespondSmartCmd(&rsp);
        }
        break;
    }
    case SMART_EVENT_UNBOUND_CMD:
    {
        IOT_CoreSmartEventUnboundCmdData_t *data = &smartEvent->data.unboundCmd;
        IOT_LOGI(TAG, "SMART event: UNBOUND_CMD, SMID=%04x, EID=%04x", data->smid, data->eid);
        SmartService_NoteAutomationChanged(data->smid); // its waiting actions are dropped
        iot_err = IOT_DaoSmartRemoveBindCmd(data->smid, data->eid);
        {
            IOT_CoreSmartCmdResponse_t rsp = {
                .cmdType = IOT_SMART_CMD_UNBOUND_CMD,
                .cfm     = 0,
                .result  = iot_err,
                .ctx     = data->ctx,
            };
            IOT_CoreRespondSmartCmd(&rsp);
        }
        break;
    }
    case SMART_EVENT_CHECK_SYNC:
    {
        IOT_CoreSmartEventCheckSyncData_t *data = &smartEvent->data.checkSync;
        IOT_LOGI(TAG, "SMART event: CHECK_SYNC, SMID=%04x", data->smid);

        IOT_SmartTrigger_t trig;
        iot_err_t getErr = IOT_DaoSmartTriggerGet(data->smid, &trig);

        if (getErr == IOT_OK)
        {
            IOT_LOGI(TAG, "  Trigger exists: EID=%04x, cfm=%u, enabled=%d",
                     trig.eid, trig.cfm, trig.enabled);
            IOT_CoreSmartCheckSyncResponse_t rsp = {
                .smid       = data->smid,
                .exists     = true,
                .triggerEid = trig.eid,
                .enabled    = (trig.enabled != 0),
                .cfm        = trig.cfm,
                .ctx        = data->ctx,
            };
            IOT_CoreRespondSmartCheckSync(&rsp);
        }
        else
        {
            IOT_LOGW(TAG, "  Trigger SMID=%04x not found locally", data->smid);
            IOT_CoreSmartCheckSyncResponse_t rsp = {
                .smid   = data->smid,
                .exists = false,
                .ctx    = data->ctx,
            };
            IOT_CoreRespondSmartCheckSync(&rsp);
        }
        break;
    }
    default:
        IOT_LOGW(TAG, "Unknown SMART event type: %d", smartEvent->type);
        iot_err = IOT_ERR_INVALID_ARG;
        break;
    }

    return iot_err;
}
