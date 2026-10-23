#ifndef STORAGE_CSV_H
#define STORAGE_CSV_H

#include "common.h"

/* Mounts the `storage` SPIFFS partition and creates the CSV header if needed. */
_bool StorageInit(void);

/* Appends one record and removes records older than the preceding 24 hours. */
_bool StorageAppend(SI64 timestamp, const char *param, UI32 value);

#endif
