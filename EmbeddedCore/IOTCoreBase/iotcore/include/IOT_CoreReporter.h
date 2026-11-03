#pragma once
#include <inttypes.h>

#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventHandler.h"

/*
 * UMBRELLA HEADER — declares nothing of its own.
 *
 * This file used to hold every outbound call core offers. "Reporter" named only one subset
 * of them: it also carried command responses, peer broadcasts and a cross-device control, so
 * the name stopped describing the contents. The declarations now live in headers named for
 * the domain a caller actually thinks in.
 *
 * Including this header keeps working and is equivalent to including all four. Prefer the
 * specific one in new code; this exists so no partner's include line had to change.
 */

#include "IOT_CoreSmart.h"
#include "IOT_CoreState.h"
#include "IOT_CoreSensorLog.h"
#include "IOT_CoreIr.h"
