#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventTypes.h" /* IOT_SENDER_* */
#include "IOT_CoreEventHandler.h" /* IOT_CoreEvent_t, IOT_DevJoinPhase_t */
#include "IOT_ContractRecord.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Contract API version. Changes only with a documented partner-facing change. */
#define IOT_CONTRACT_VERSION_MAJOR 1
#define IOT_CONTRACT_VERSION_MINOR 0
#define IOT_CONTRACT_VERSION_PATCH 0
#define IOT_CONTRACT_VERSION_STRING "1.0.0"

/** @brief The most elements a device can declare in IOT_ContractConfig_t.elementIds. */
#define IOT_CONTRACT_MAX_ELEMENTS 16

/**
 * @brief What every device tells the contract at start. Zero-initialise, then set what you need.
 *
 * Optional features have their own call before IOT_ContractStart() (IOT_ContractIrConfigure(),
 * IOT_ContractRecordUseCustomStore()), not a field here.
 */
typedef struct
{
    const char *modelId; /**< The product's model id, as registered with the cloud. Required:
                              IOT_ContractStart() starts core with it. */
    const uint16_t *elementIds; /**< Your device's own element ids, in the order of the elements in
                                     the model registered with the cloud (first id = first element).
                                     Used by IOT_ContractStart() only, which keeps a copy: the
                                     contract translates between these and the ids the cloud
                                     assigned, by position, so the element ids you receive and pass
                                     are always these. An element the cloud's model has but you did
                                     not declare is never applied (it is skipped with a warning).
                                     NULL: ids are passed through untranslated. */
    uint8_t elementCount;       /**< Number of entries in elementIds; at most
                                     IOT_CONTRACT_MAX_ELEMENTS (more are ignored, with an error log). */
} IOT_ContractConfig_t;

/**
 * @brief Who caused a change the contract asks you to apply.
 */
typedef enum
{
    IOT_CONTRACT_SOURCE_CLOUD,      /**< The cloud, or a third-party service through it. */
    IOT_CONTRACT_SOURCE_APP_LOCAL,  /**< The user's phone app. */
    IOT_CONTRACT_SOURCE_AUTOMATION, /**< An automation or a scene. */
    IOT_CONTRACT_SOURCE_SCHEDULE,   /**< A schedule or a countdown timer. */
    IOT_CONTRACT_SOURCE_VOICE,      /**< A voice assistant. */
    IOT_CONTRACT_SOURCE_DEVICE,     /**< A device: this one, or another syncing its state. */
    IOT_CONTRACT_SOURCE_OTHER,      /**< Anything else, including a sender the contract does not know. */
} IOT_ContractSource_t;

/**
 * @brief One attribute value for one element, to drive on your hardware.
 *
 * Valid only for the duration of the apply call; copy what you keep.
 */
typedef struct
{
    uint16_t eid;           /**< Device EID: the root device's, or a sub-device's. */
    uint16_t elemId;        /**< Element id: for the root device, one of your declared
                                 IOT_ContractConfig_t.elementIds; for a sub-device, the id its hub
                                 assigned. Never the cloud's id. */
    uint16_t attrId;        /**< Attribute to set (IOT_ATTR_*). */
    const uint8_t *value;   /**< The new value bytes, in the attribute's stored layout. */
    uint16_t valueLen;      /**< Number of bytes in value. */
    IOT_ContractSource_t source; /**< Who asked for the change. */
} IOT_ContractApply_t;

/**
 * @brief What the product supplies to the contract. Zero-initialise, then set what you need.
 *
 * IOT_ContractStart() keeps its own copy, so the struct you pass may live on the stack.
 */
typedef struct IOT_ContractCallbacks IOT_ContractCallbacks_t;
struct IOT_ContractCallbacks
{
    /** Drive one element's attribute on the hardware (or, for a sub-device, send it on to the
     *  sub-device). Called once per element of a state command the contract accepted, in order,
     *  device by device for a command that addresses several devices (a group or the whole
     *  location). Return IOT_OK when the hardware took the value (for a sub-device: when it was
     *  sent). IOT_ERR_NOT_FOUND: this element/attribute is not driven by your product; treated
     *  as success. Any other error stops that device's remaining elements; the ones already
     *  applied are still committed. Other devices of the command carry on.
     *  After the last device the contract commits what was applied -- stores it, appends it to
     *  Record, sends one state report for the whole command, then feeds Smart with the
     *  command's sender -- so do not store or report the change yourself.
     *  A device attribute set reaches apply only for the device itself: one call (its element
     *  id translated the same way); applied (or IOT_ERR_NOT_FOUND), it is stored and recorded,
     *  the cloud is answered, then Smart is fed; any other error is the answer and nothing is
     *  stored. For a sub-device the value is stored without driving it: no apply call, then the
     *  same store, Record, answer and Smart.
     *  Blocks: keep it short and non-blocking -- the next request waits for it. Task: the task
     *  that delivers core events, or the timer task for an automation's actions (immediate,
     *  delayed or reverse), which has a small stack; guard hardware you also drive from your own
     *  tasks. Do not keep req or its value pointer after you return. Required. */
    iot_err_t (*apply)(const IOT_ContractApply_t *req);

    /** Every core event the contract does not handle itself, passed through unchanged. Called
     *  on the task that delivers core events; do not keep the pointer after you return. NULL:
     *  such events are dropped. The contract handles IOT_EVENT_SMART and, of IOT_EVENT_STATE, the
     *  state command, the state query and the history query (IOT_EVENT_TYPE_STATE_SET, _GET,
     *  _RECORD) itself, so they never arrive here. A sensor-log request
     *  (IOT_EVENT_TYPE_STATE_SENSOR_LOG) arrives here only when a custom history store is
     *  installed (IOT_ContractRecordUseCustomStore()); otherwise the contract answers it with an
     *  empty log, as it also does when this hook is NULL.
     *  Nor do the IR keyset store and IR capability question (see IOT_ContractIrConfigure()).
     *  A reboot or OTA request (IOT_EVENT_OS) arrives after the contract has written out what
     *  it holds in RAM (as IOT_ContractPrepareRestart()), so you may restart straight away.
     *  OS_EVENT_OTA_BOOT_STARTED is yours to claim: set data.otaBootStarted.handled here, or
     *  core discards the update (see IOT_CoreEventOs.h).
     *  Of IOT_EVENT_DEVICE, the contract handles a device joining, a device removed, group bind
     *  and unbind, and a device attribute set (DEV_EVENT_NEW_DEVICE_JOINED, _DEVICE_REMOVED,
     *  _GROUP_BIND, _GROUP_UNBIND, _DEVICE_ATTR_SET) itself, so they never arrive here: it
     *  updates its stores and answers core, and you hear of them through onDeviceJoined,
     *  onDeviceRemoving and onDeviceRemoved, and (for an attribute set on the device itself)
     *  through apply.
     *  DEV_EVENT_CLOUD_CONNECTED arrives here after the contract has run its connect duties
     *  (sending history recorded while offline, starting the Smart scheduler, re-syncing
     *  condition groups) and scheduled a report of every device's full state, so do neither. Every other DEVICE event (connection, provisioning, boot,
     *  identification, the device status request) arrives untouched. */
    void (*onCoreEvent)(const IOT_CoreEvent_t *event);

    /** A device joining this one (a hub's sub-device) reached one phase of its join. Called at
     *  most once per phase, in order:
     *  - IOT_DEV_JOIN_PHASE_DEVICE_INFO: as soon as the device's common information (type,
     *    protocol, network address) is in the Device store, so you can index it and attribute
     *    its traffic before the join finishes. Its identity and group entry are written after
     *    this returns; if either write then fails, the phase is not answered (the hook has
     *    already run). Not called when the common information could not be stored.
     *  - _ELM_INFO: after its elements are stored; not called when they could not be.
     *  - _SYNC_COMPLETE: the end of the join, whatever became of storing its network key (if it
     *    has one): refresh your radio's view of its nodes, ask for data the device needs. A
     *    failed key save is still reported to core as a failure.
     *  Not called for a phase the contract does not know. Read the device from the Device store.
     *  The contract answers core after this returns.
     *  Blocks: may, but keep it short -- the join waits for it. Task: the task that delivers core
     *  events. NULL: nothing to do. */
    void (*onDeviceJoined)(uint16_t eid, IOT_DevJoinPhase_t phase);

    /** A device is about to be removed: the cloud removed it (@p root true: this device itself,
     *  which is a factory reset), or a sub-device joined again under a new EID and its old
     *  entry goes. Called BEFORE the contract deletes anything, so the device's record is still
     *  in the Device store: release what you hold for it here, and do what needs its record
     *  (for example resetting it on its own network, with the key stored in that record).
     *  onDeviceRemoved follows once the contract's stores are cleared.
     *  Blocks: may, but keep it short. Task: the task that delivers core events. NULL: nothing
     *  to do. */
    void (*onDeviceRemoving)(uint16_t eid, bool root);

    /** The device named in onDeviceRemoving is gone from every contract store and, for a removal
     *  the cloud asked for, the cloud has its answer. @p root true: this device was removed and
     *  the contract's stores are wiped (as IOT_ContractFactoryReset()) -- reset your own product
     *  data and restart the chip from here; the contract does not restart. @p root false: a
     *  sub-device; do not restart. Called after a failed removal too.
     *  Blocks: may (a restart does not return). Task: the task that delivers core events. NULL:
     *  nothing to do. */
    void (*onDeviceRemoved)(uint16_t eid, bool root);
};

/**
 * @brief Start the contract and core: the one call that brings the device up.
 *
 * In order: brings the contract stores up, keeps a copy of the
 * declared elements (config->elementIds), takes the core event
 * callback for EVERY event category, then starts core with config->modelId (IOT_CoreInit()).
 * Smart events, the state command and the state and history queries, and the device events that
 * change its stores (join, removal, groups, attribute set) are handled by the contract (a state
 * command, or an attribute set on the device itself, drives your hardware through cb->apply; an
 * attribute set on a sub-device is stored without driving it; a join or a removal reaches
 * you through the device hooks); every other event goes to cb->onCoreEvent (see there for which,
 * and for what the contract did first).
 *
 * Do not register a core event callback yourself, before or after this call: core holds one
 * callback per category. One registered before this call makes Start fail with
 * IOT_ERR_INVALID_STATE (and core is not started); one registered after is refused by core
 * with IOT_ERR_INVALID_STATE. Pre-start core settings (IOT_CoreConfigure*()) go before it.
 *
 * Blocks: yes -- scans storage, then runs core's start-up. Safe from a timer callback: no.
 * Task: the boot task (app_main), once.
 *
 * @param config Boot-time choices; modelId is required.
 * @param cb     Your callbacks; copied, so it may be a stack variable.
 * @return IOT_OK. IOT_ERR_INVALID_ARG for a NULL config, NULL modelId, NULL cb or NULL
 *         cb->apply (nothing done). The error from bringing the stores up, or from registering a category (the
 *         category is logged; core is not started). Otherwise core's start-up result, unchanged.
 */
iot_err_t IOT_ContractStart(const IOT_ContractConfig_t *config, const IOT_ContractCallbacks_t *cb);

/**
 * @brief Declare what IR this device can do. Optional; call before IOT_ContractStart().
 *
 * The contract answers the cloud's IR capability question with these values. Not called: the
 * device declares no IR capability (as if called with all three false). A later call replaces
 * the earlier values and takes effect at the next question.
 *
 * With IOT_ContractStart(), the contract also stores the IR keysets the cloud sends for an IR
 * appliance (read them back through the IR store); sending IR and learning a remote stay yours
 * and arrive at cb->onCoreEvent (IOT_EVENT_IR).
 *
 * Blocks: no. Safe from a timer callback: yes. Task: the boot task (app_main), before
 * IOT_ContractStart().
 *
 * @param tx     The device can transmit IR.
 * @param rx     The device can receive IR (learn a remote).
 * @param detect The device can recognise a remote's protocol from what it receives.
 * @return IOT_OK.
 */
iot_err_t IOT_ContractIrConfigure(bool tx, bool rx, bool detect);

/**
 * @brief Wipe every contract store, and nothing else.
 *
 * Not a product's factory reset. A product's own reset (a long press, a console command) calls
 * IOT_CoreFactoryReset(): core wipes its own configuration and reports this device's removal,
 * the contract then wipes its stores (by this function) and calls onDeviceRemoved with root
 * true, where the product resets its own data and restarts. This function wipes only the
 * contract's stores, for an SDK's own recovery (for example a store found unreadable at boot):
 * core's configuration, the cloud's view of the device and your callbacks are untouched.
 *
 * The EID list is read first, because the Device store is wiped last and the per-device
 * stores (IR, Record) are keyed by it. Order: Smart triggers and their bind commands,
 * condition-group state, schedules, IR, Record
 * per device, then Device and Group.
 * Anything of yours that is keyed by the same EIDs must read them before calling this.
 *
 * Blocks: yes (storage erases). Safe from a timer callback: no. Task: any.
 *
 * @return IOT_OK. Each store is reset on a best-effort basis.
 */
iot_err_t IOT_ContractFactoryReset(void);

/**
 * @brief Write out what the contract holds in RAM before a planned restart (reboot command,
 *        OTA, deinit).
 *
 * Synchronous: Record's open blocks, then the Device store's element state, are in storage
 * when this returns. Not needed on a factory reset, which wipes the stores anyway.
 *
 * Blocks: yes -- waits up to about a second for Record's lock, then writes storage. Safe from a
 * timer callback: no. Task: any.
 *
 * @return IOT_OK, or the first error from the two flushes (for example IOT_ERR_TIMEOUT); both
 *         are always attempted. The restart may still go ahead; what was not flushed is lost.
 */
iot_err_t IOT_ContractPrepareRestart(void);

/**
 * @brief The product's own hardware changed an element (a button press, a sensor edge): store it,
 *        record it, tell the cloud and the app, and feed Smart.
 *
 * Runs the steps in this order: the Device store, Record, the state report, then Smart with the
 * sender "device". @p elemId is the product's own element number, as declared in
 * IOT_ContractConfig_t; the contract translates it. For a sub-device (@p eid is not this device's
 * own), @p elemId is the id its hub assigned and is used as given.
 *
 * Smart is skipped only when the report could not read the device (IOT_ERR_DATA_NOT_FOUND). A
 * failed send does not skip it, so local control keeps working while the device is offline.
 *
 * @param eid    Device EID: this device's, or a sub-device's own.
 * @param elemId The element that changed.
 * @param attrId The attribute that changed (IOT_ATTR_*).
 * @param value  The new value bytes. Not NULL.
 * @param len    Number of bytes in value. At least 1.
 *
 * @return IOT_OK. IOT_ERR_INVALID_ARG for a NULL value or a zero len (nothing done). The Device
 *         store's error when the change could not be stored (nothing recorded or reported). Or
 *         the report's own error; the change was stored and recorded in that case:
 *         IOT_ERR_DATA_NOT_FOUND or IOT_ERR_NO_MEM when the report could not be assembled from
 *         the stored state (nothing sent, Smart skipped), any other error when sending it failed.
 *
 * Blocks: yes -- writes the Device store and hands the report to the cloud connection. Safe from
 * a timer callback: no. Task: any task that is not a timer callback (typically the one that
 * handles your hardware input).
 */
iot_err_t IOT_ContractLocalChange(uint16_t eid, uint16_t elemId, uint16_t attrId, const uint8_t *value, uint16_t len);

/**
 * @brief The id the Device store keeps one of your elements under.
 *
 * The Device store (IOT_DaoDeviceGetDeviceElemsState()) lists a device's elements under the ids
 * the cloud assigned, which are not your element numbers. To find your own element in what it
 * returns, look it up by this id. The contract joins the two by position, exactly as it does for
 * apply and IOT_ContractLocalChange(), so a read and a write of one element always meet.
 *
 * @p elemId is returned unchanged for a sub-device (its ids are its hub's), when no elements were
 * declared, before the device is provisioned, for an id you did not declare, and for an element
 * with no counterpart in the store (the product declared more elements than the cloud's model
 * has). An unchanged id is not checked against the store: it may be the id another of your
 * elements is stored under, so read the result as "this element" only for a declared element of
 * a provisioned device.
 *
 * Blocks: yes -- reads the Device store's element list. Safe from a timer callback: no. Task: any.
 *
 * @param eid    Device EID: this device's, or a sub-device's own.
 * @param elemId Your element number, as declared in IOT_ContractConfig_t.elementIds.
 * @return The id to look for in the Device store.
 */
uint16_t IOT_ContractElementStoreId(uint16_t eid, uint16_t elemId);

/**
 * @brief A hub reports a sub-device's state as your radio received it.
 *
 * Call it with every state a sub-device reports, whether the sub-device changed itself (a button
 * pressed on it, a sensor firing) or is confirming a change the contract applied through your
 * apply callback.
 *
 * - A confirmation: the same element, attribute and value bytes as a change the contract applied
 *   to that sub-device within the last 3 seconds. The Device store is updated and nothing else
 *   happens: the change was already recorded, reported and given to Smart with the sender of the
 *   request that caused it. Each applied change is confirmed at most once.
 * - Anything else is the sub-device's own change and runs the same steps as
 *   IOT_ContractLocalChange(): the Device store, Record, the state report (with @p pushNotify),
 *   then Smart with the sender "device". Smart is skipped only when the report could not read the
 *   device (IOT_ERR_DATA_NOT_FOUND).
 *
 * The value must have the attribute's declared size (IOT_AttrGetSize()); a report of any other
 * size is refused, so one malformed report cannot shrink the stored value. @p elemId is the id the
 * hub assigned to the sub-device's element and is used as given. The element must already be known
 * to the Device store; an attribute it does not hold yet is added.
 *
 * @param eid        The sub-device's EID.
 * @param elemId     The element that reported.
 * @param attrId     The attribute reported (IOT_ATTR_*).
 * @param value      The value bytes. Not NULL.
 * @param len        Number of bytes in value. At least 1.
 * @param pushNotify Whether the state report may raise a push notification to the app.
 *
 * @return IOT_OK. IOT_ERR_INVALID_ARG for a NULL value or a zero len, IOT_ERR_INVALID_SIZE for a
 *         value of the wrong size (nothing done in either case). The Device store's error when the
 *         state could not be stored (nothing recorded or reported). Or the report's own error, as
 *         for IOT_ContractLocalChange(); the state was stored and recorded in that case.
 *
 * Blocks: yes -- writes the Device store and hands the report to the cloud connection. Safe from
 * a timer callback: no. Task: any task that is not a timer callback (typically the one that
 * receives your radio's messages).
 */
iot_err_t IOT_ContractSubDeviceReport(uint16_t eid, uint16_t elemId, uint16_t attrId, const uint8_t *value,
                                      uint16_t len, bool pushNotify);

/* ---- Contract events ------------------------------------------------------------------------
 *
 * Read-only notifications of what the contract has done, for products that keep their own
 * bookkeeping (an activity time, a display, a log). An event never changes what the contract
 * does: it is raised after the action finished, it is const, and a subscriber has no way to
 * answer it. New event types may be added at the end of the list; a subscriber ignores the
 * types it does not know.
 *
 * Element ids in an event are the DEVICE's own element numbers (the ones declared at start; a
 * sub-device's hub-assigned ids), as apply receives them. An element the product did not declare
 * is left out of an event, and an event that would name only such elements is not raised.
 */

/** Maximum number of subscribers (IOT_ContractSubscribe()). */
#define IOT_CONTRACT_EVENT_SUBSCRIBERS_MAX 4

/** What happened. Values are fixed: new types are only ever added at the end. */
typedef enum
{
    /** A device's new state was committed: stored, recorded, reported and given to Smart. Once
     *  per device per change, on every path (data.stateCommitted.cause says which). A
     *  sub-device confirming a change the contract applied raises nothing: nothing new was
     *  committed (the change already raised its own event). */
    IOT_CONTRACT_EVT_STATE_COMMITTED = 0,
    /** This device ran an automation's actions (a trigger, a schedule or a scene). Actions with
     *  a delay are scheduled by then, not yet carried out. */
    IOT_CONTRACT_EVT_AUTOMATION_FIRED = 1,
    /** A history block was closed and a new one is open. A block is closed at the next sample
     *  after it filled up or its time span ran out. */
    IOT_CONTRACT_EVT_RECORD_BLOCK_CLOSED = 2,
    /** Closed history blocks were sent to the cloud. */
    IOT_CONTRACT_EVT_RECORD_PUSHED = 3,
    /** A sub-device finished joining and is in the Device store. */
    IOT_CONTRACT_EVT_DEVICE_ADDED = 4,
    /** A device was removed from every contract store: by the cloud, or because it joined again
     *  under a new EID. Raised before onDeviceRemoved. */
    IOT_CONTRACT_EVT_DEVICE_REMOVED = 5,
    /** A mirrored on/off command (an automation keeping two devices' switches in step) named an
     *  element this device holds no mirror pairing for, and was ignored. Nothing was applied. */
    IOT_CONTRACT_EVT_MIRROR_REFUSED = 6,
} IOT_ContractEventType_t;

/** Which path committed a change (IOT_CONTRACT_EVT_STATE_COMMITTED). Values are fixed: new
 *  causes are only ever added at the end. */
typedef enum
{
    /** A state command from the cloud, the app or an automation, for one device, a group or a
     *  location (one event per device it moved). */
    IOT_CONTRACT_CAUSE_STATE_COMMAND = 0,
    /** Your IOT_ContractLocalChange(): this device's own hardware changed. */
    IOT_CONTRACT_CAUSE_LOCAL_CHANGE = 1,
    /** A sub-device reported a change of its own. */
    IOT_CONTRACT_CAUSE_SUBDEVICE_REPORT = 2,
    /** A device attribute set. */
    IOT_CONTRACT_CAUSE_ATTRIBUTE_SET = 3,
} IOT_ContractCause_t;

/** One contract event: @c type says which member of @c data is valid. */
typedef struct
{
    IOT_ContractEventType_t type;
    union
    {
        /** IOT_CONTRACT_EVT_STATE_COMMITTED. */
        struct
        {
            uint16_t eid;                                 /**< The device that changed. */
            uint16_t elemIds[IOT_CONTRACT_MAX_ELEMENTS];  /**< Its elements that changed. */
            uint8_t numElems;                             /**< Entries used in elemIds; at least 1. */
            uint16_t attrId;                              /**< The attribute that changed. */
            const uint8_t *value;                         /**< The new value; valid during the call only. */
            uint16_t valueLen;                            /**< Bytes in value. */
            IOT_ContractSource_t source;                  /**< Who caused the change. */
            IOT_ContractCause_t cause;                    /**< Which path committed it. */
        } stateCommitted;
        /** IOT_CONTRACT_EVT_AUTOMATION_FIRED. */
        struct
        {
            uint16_t smid; /**< The automation's id, as the Smart store holds it. */
        } automationFired;
        /** IOT_CONTRACT_EVT_RECORD_BLOCK_CLOSED. */
        struct
        {
            uint16_t eid;    /**< The device whose history it is. */
            uint16_t elemId; /**< Its element. */
            uint16_t attrId; /**< The recorded attribute. */
        } recordBlockClosed;
        /** IOT_CONTRACT_EVT_RECORD_PUSHED. */
        struct
        {
            uint16_t eid;        /**< The device whose history it is. */
            uint16_t elemId;     /**< Its element. */
            uint16_t attrId;     /**< The recorded attribute. */
            uint16_t blockCount; /**< Number of blocks sent. */
        } recordPushed;
        /** IOT_CONTRACT_EVT_DEVICE_ADDED. */
        struct
        {
            uint16_t eid; /**< The new sub-device. */
        } deviceAdded;
        /** IOT_CONTRACT_EVT_DEVICE_REMOVED. */
        struct
        {
            uint16_t eid; /**< The device removed. */
            bool root;    /**< true: this device itself (every contract store was wiped). */
        } deviceRemoved;
        /** IOT_CONTRACT_EVT_MIRROR_REFUSED. */
        struct
        {
            uint16_t eid;    /**< The device the command named. */
            uint16_t elemId; /**< The first element it named that has no mirror pairing. */
        } mirrorRefused;
    } data;
} IOT_ContractEvent_t;

/** A subscriber: @p event is valid during the call only; @p arg is the pointer given at
 *  subscription. */
typedef void (*IOT_ContractEventFn)(const IOT_ContractEvent_t *event, void *arg);

/**
 * @brief Subscribe to contract events.
 *
 * Every subscriber receives every event, in subscription order, synchronously on the task that
 * did the action: the task that delivers core events, or the timer task for an automation's
 * actions (immediate, delayed or reverse) and for evaluating automation triggers. Keep the callback short and non-blocking (the timer
 * task has a small stack).
 *
 * An event raised from inside your subscriber is not delivered: if the callback causes another
 * contract action (for example IOT_ContractLocalChange()), that action is carried out in full,
 * but nobody hears of it. Events raised meanwhile by other tasks are delivered as usual.
 *
 * Call it at boot, before IOT_ContractStart(): the subscriber list is not locked against events
 * being raised.
 *
 * @param fn  The callback. Not NULL.
 * @param arg Passed back to @p fn unchanged; may be NULL.
 *
 * @return IOT_OK. IOT_ERR_INVALID_ARG for a NULL @p fn. IOT_ERR_NO_MEM when
 *         IOT_CONTRACT_EVENT_SUBSCRIBERS_MAX subscribers are already registered.
 *
 * Blocks: no. Safe from a timer callback: not applicable (boot only). Task: the boot task.
 */
iot_err_t IOT_ContractSubscribe(IOT_ContractEventFn fn, void *arg);

#ifdef __cplusplus
}
#endif
