#pragma once

/*
 * The contract's read-only STATE answers and the full-state sync.
 *
 *   GET        -> the device's stored state, the time block when asked for and the clock is
 *                 synced, and Record's current history blocks.
 *   RECORD     -> served by Record, synchronously, with the request's own context.
 *   SENSOR_LOG -> the product answers when a Record append override keeps that history (the
 *                 router forwards it); otherwise the contract sends the empty reply below.
 *
 * Internal to the contract. The request handlers run on the task that delivers core events.
 */

#include "IOT_Contract.h"
#include "IOT_CoreEventHandler.h"
#include "IOT_ErrorManager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Answer a GET with the device's stored state.
 *
 * The time block is optional and the state is not: an unsynced clock drops the time block and
 * still answers. Record's current blocks are attached when it has any.
 *
 * @return IOT_OK, the Device store's error (nothing sent), or core's send result.
 */
iot_err_t IOT_ContractStateRespondGet(IOT_EventStateGetRequest_t *getData);

/**
 * @brief Answer a history query from Record, synchronously: the request's sender id lives in
 *        core's buffer only for the duration of the event.
 *
 * @return IOT_OK, IOT_ERR_INVALID_ARG for NULL, or Record's result.
 */
iot_err_t IOT_ContractStateRespondRecord(IOT_EventStateRecordRequest_t *recordData);

/**
 * @brief Answer a sensor-log request with no entries.
 *
 * The reply a device without a sensor-log history has always sent: the request echoed back, no
 * entries, and the value width Record would use for that element.
 *
 * @return IOT_OK, IOT_ERR_INVALID_ARG for NULL, or core's send result.
 */
iot_err_t IOT_ContractStateRespondSensorLogEmpty(IOT_EventStateSensorLogRequest_t *logData);

/**
 * @brief Report every stored device's full state now. Not provisioned or no devices: nothing.
 *
 * Blocks: yes (Device store reads and one report per device). Task: the core work task.
 * @return IOT_OK.
 */
iot_err_t IOT_ContractStateReportAllDevices(void);

/**
 * @brief Schedule IOT_ContractStateReportAllDevices() in 3 s, handed to the core work task.
 *
 * At most one sync is pending at a time; a call while one is pending does nothing.
 * Blocks: no. Safe from a timer callback: yes. Task: any.
 */
void IOT_ContractStateSyncSchedule(void);

#ifdef __cplusplus
}
#endif
