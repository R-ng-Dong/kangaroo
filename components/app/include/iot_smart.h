#ifndef IOT_SMART_H_
#define IOT_SMART_H_

#include "common.h"
#include "IOT_CoreEventHandler.h"


void IotSmartInit(void);
_bool IOTSmartClear(void);
_bool IotSmartBindCommand(IOT_CoreSmartEventBindTriggerData_t *data);
_bool IotSmartUnBindCommand(UI16 smid);
_bool IotSmartEnableCommand(UI16 smid, UI8 enable);
_bool IOTSetRoFlushingTime(UI16 smid, UI16 time_s);

void IotSmartEvaluateCondition(void);

#endif /* IOT_SMART_H_ */
