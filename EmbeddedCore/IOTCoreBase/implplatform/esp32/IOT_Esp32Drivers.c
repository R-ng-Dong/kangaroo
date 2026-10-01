#include "sdkconfig.h"
#include "IOT_Esp32Drivers.h"

// No weak stubs here. A weak definition in this object satisfies the call below
// locally, so the linker never extracts the object holding the real (strong)
// registration and the driver stays unregistered — that is how mesh was silently
// disabled on every build until 08_2026. Each call below must be a genuine
// undefined reference that force-links the real driver object.
//
// Only drivers compiled INTO iotplatform can be registered from here. Drivers in
// their own component (iotble, iotmesh, iotir) cannot: iotplatform is their
// dependency, so calling into them would be a circular dep. Those self-register
// via __attribute__((constructor)) and are force-linked by -u flags in their own
// CMakeLists.

void IOT_Esp32DriversInit(void)
{
    IOT_Esp32MdnsMgmtRegister();
}
