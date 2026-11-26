#ifndef TDS_TASK_H
#define TDS_TASK_H

#include "common.h"

void TdsTaskStart(void);
void TdsTaskStop(void);
void TdsTaskContinue(void);

UI16 TdsInletGet(void);
UI16 TdsRoGet(void);

UI16 TdsInletGetValue(void);
UI16 TdsRoGetValue(void);

#endif
