/**
 * @file ble_ext_adv_policy.c
 * @brief See ble_ext_adv_policy.h.
 */

#include "ble_ext_adv_policy.h"

void IOT_BleExtAdvShapeFromConnectable(bool connectable, IOT_BleExtAdvShape_t *out)
{
    if (out == NULL)
    {
        return;
    }

    /* Always a legacy PDU. The point of moving to the extended API is that the legacy
     * one is compiled out when BLE Mesh asks for BLE 5.0 — not to change what goes on
     * air. Extended PDUs would be invisible to any scanner that only does 1M legacy. */
    out->legacyPdu = 1u;
    out->connectable = connectable ? 1u : 0u;
    out->scannable = connectable ? 1u : 0u;
}

bool IOT_BleExtAdvShapeIsValid(const IOT_BleExtAdvShape_t *shape)
{
    if (shape == NULL)
    {
        return false;
    }

    if (!shape->legacyPdu)
    {
        /* Extended PDUs: this driver never asks for them, so nothing here vouches
         * for their combinations. */
        return false;
    }

    if (shape->connectable && !shape->scannable)
    {
        return false; /* no undirected legacy PDU carries this */
    }

    return true;
}

/* One bit per advertising instance. Deliberately not static-initialised anywhere
 * else: IOT_BleExtAdvForgetAll() is the only reset, so the deinit path owns it. */
static uint8_t s_configuredInstances;

void IOT_BleExtAdvMarkConfigured(uint8_t instance)
{
    if (instance > IOT_BLE_ADV_INST_MAX)
    {
        return;
    }
    s_configuredInstances |= (uint8_t) (1u << instance);
}

bool IOT_BleExtAdvIsConfigured(uint8_t instance)
{
    if (instance > IOT_BLE_ADV_INST_MAX)
    {
        return false;
    }
    return (s_configuredInstances & (uint8_t) (1u << instance)) != 0u;
}

void IOT_BleExtAdvForgetAll(void)
{
    s_configuredInstances = 0u;
}
