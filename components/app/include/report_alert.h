#ifndef REPORT_ALERT_H_
#define REPORT_ALERT_H_

#include "common.h"
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventService.h"

/* Publish a fresh state snapshot and the currently open alert set. Call from an
 * application task after MQTT connects and on the configured report interval. */
iot_err_t ReportAlertPublishReport(void);

/* Call from the application loop to advance queued alert retransmissions. */
void ReportAlertPoll(void);
/* Register as the IOT_EVENT_SERVICE callback target for alert.ack messages. */
void ReportAlertHandleServiceEvent(IOT_CoreServiceEvent_t *event);

#endif  // REPORT_ALERT_H_
