#pragma once

/* Per-driver registration for ESP32 platform drivers.
 *
 * The rule: NO weak stubs, ever. Weak symbols in a static library are NOT
 * overridden by a strong definition in another archive member — the weak
 * definition satisfies the call site, so the linker has no undefined reference
 * left to make it extract the object holding the real registration. The driver
 * silently stays NULL and every call returns IOT_ERR_NOT_SUPPORTED. This bit
 * mDNS once and mesh for far longer.
 *
 * Two valid patterns, chosen by where the driver is compiled:
 *
 *   Same component as this file (mDNS)
 *     Register it from IOT_Esp32DriversInit() below. That call is the undefined
 *     reference that force-links the driver object out of libiotplatform.a.
 *
 *   Its own component (BLE/iotble, mesh/iotmesh, IR/iotir)
 *     These live apart so they can declare REQUIRES bt without pulling the BT
 *     stack into the foundational iotplatform component. iotplatform is their
 *     dependency, so a call from here would be circular. They self-register via
 *     __attribute__((constructor)) and their own CMakeLists carries a
 *     `-u <symbol>` flag to force the object out of the archive.
 */

void IOT_Esp32MdnsMgmtRegister(void);

void IOT_Esp32DriversInit(void);
