#pragma once

/**
 * @brief Provisioning mode selection.
 *
 * Controls how the device enters provisioning when not yet provisioned.
 * Default is IOT_PROVISION_BLE_LAN (BLE config + LAN discovery).
 * Call IOT_CoreSetProvisionMode() before IOT_CoreStartProvision() to override.
 */
typedef enum
{
    IOT_PROVISION_BLE_LAN = 0, ///< BLE config + LAN discovery (default)
    IOT_PROVISION_LAN_ONLY,    ///< LAN config only, no BLE (not yet implemented)
    IOT_PROVISION_AP_MODE,     ///< Device as WiFi AP (not yet implemented)
} IOT_ProvisionMode_t;
