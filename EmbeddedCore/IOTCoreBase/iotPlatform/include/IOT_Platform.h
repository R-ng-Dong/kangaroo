#pragma once
#include "IOT_ErrorManager.h"

iot_err_t IOT_InitPlatform(void);

/**
 * @brief Bring up the platform WITHOUT the BLE stack.
 *
 * Same drivers, storage and network as IOT_InitPlatform(), minus IOT_BleInit().
 * Intended for a boot that exists only to run an OTA download: skipping BLE (and,
 * in the SDK, mDNS / the TCP server / the smart scheduler) leaves far more
 * contiguous heap than the ~16.7 KB a TLS download needs.
 *
 * A device initialised this way cannot advertise, be provisioned, or fall back to
 * BLE on WiFi loss — so it must reboot rather than continue running.
 *
 * OPTIONAL. IOT_CoreInit() now finds out for itself whether this boot is OTA-only
 * (IOT_CoreOtaIsPending()) and releases BLE's heap itself (IOT_CoreBleDeinit())
 * when it is — so the normal choice for a boot's platform init is plain
 * IOT_InitPlatform(), even on hardware that expects to take OTA requests. Call
 * this instead only if a partner wants the maximum possible headroom on that
 * path and is willing to accept the constraints above for the whole boot, not
 * just the part of it core releases BLE for.
 */
iot_err_t IOT_InitPlatformLean(void);

iot_err_t IOT_DeinitPlatform(void);