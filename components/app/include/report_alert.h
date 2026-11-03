#ifndef REPORT_ALERT_H_
#define REPORT_ALERT_H_

#include "common.h"
#include "IOT_ErrorManager.h"

/* Publish a fresh state snapshot and the currently open alert set. Call from an
 * application task after MQTT connects and on the configured report interval. */
iot_err_t ReportAlertPublishReport(void);

#endif  // REPORT_ALERT_H_
