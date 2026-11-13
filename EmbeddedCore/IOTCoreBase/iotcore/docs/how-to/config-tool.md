# Give your users a configuration tool

When building your own SDK, you should give your users (app developers) an interactive tool that modifies the ESP-IDF `sdkconfig` file. Here's how such a tool works so you can write one.

## How It Works

The config tool reads the `sdkconfig` file, applies regex-based replacements for user-selected options, and writes it back. It does **not** regenerate from scratch — only the changed keys are modified, leaving all other settings intact.

## Core Pattern: `set_config_key(content, key, value)`

Every config change boils down to one operation — set a key to a value, or unset it:

```python
import re

def set_config_key(content, key, value):
    """Set a config key in sdkconfig text. value=None means 'is not set'."""
    if value is not None:
        new_line = f"{key}={value}"
        # Replace existing KEY=... line
        pattern = re.compile(rf"^{re.escape(key)}=.*$", re.MULTILINE)
        if pattern.search(content):
            return pattern.sub(new_line, content)
        # Replace existing "# KEY is not set" line
        pattern = re.compile(rf"^# {re.escape(key)} is not set$", re.MULTILINE)
        if pattern.search(content):
            return pattern.sub(new_line, content)
        # Key doesn't exist — append
        return content.rstrip("\n") + f"\n{new_line}\n"
    else:
        new_line = f"# {key} is not set"
        pattern = re.compile(rf"^{re.escape(key)}=.*$", re.MULTILINE)
        if pattern.search(content):
            return pattern.sub(new_line, content)
        return content
```

## BLE Stack Switching

When switching between NimBLE and Bluedroid, you need to set the chosen stack and unset the other:

```python
# Switch to Bluedroid with BLE 4.2 enforcement
content = set_config_key(content, "CONFIG_BT_BLUEDROID_ENABLED", "y")
content = set_config_key(content, "CONFIG_BT_NIMBLE_ENABLED", None)
content = set_config_key(content, "CONFIG_BT_BLE_42_FEATURES_SUPPORTED", "y")
content = set_config_key(content, "CONFIG_BT_BLE_50_FEATURES_SUPPORTED", None)

# Switch to NimBLE
content = set_config_key(content, "CONFIG_BT_NIMBLE_ENABLED", "y")
content = set_config_key(content, "CONFIG_BT_BLUEDROID_ENABLED", None)
```

**Important:** Bluedroid must always use BLE 4.2. BLE 5.0 with Bluedroid has GATT issues (connection works but read/write fails). If your SDK has a build script, have it enforce this as a safety net too, but your config tool should set it correctly upfront.

## Board Change Handling

When the target board changes (e.g., ESP32 → ESP32-C3), you **cannot** patch `CONFIG_IDF_TARGET` in-place. The sdkconfig must be regenerated from defaults:

```python
# Concatenate base defaults + chip-specific defaults
content = read_file("sdkconfig.defaults")
content += "\n" + read_file(f"sdkconfig.defaults.{board}")
# Then apply user choices on top
```

The next `idf.py build` will resolve all remaining Kconfig defaults automatically.

## Options Your Tool Should Support

| Option | Config Keys | Notes |
|--------|------------|-------|
| BLE stack | `CONFIG_BT_NIMBLE_ENABLED`, `CONFIG_BT_BLUEDROID_ENABLED` | Bluedroid → force BLE 4.2 |
| BLE 5.0 features | `CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT`, `CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_2M_PHY`, etc. | Only for NimBLE on S3/C3/C6 |
| Log level | `CONFIG_LOG_DEFAULT_LEVEL_INFO=y` + `CONFIG_LOG_DEFAULT_LEVEL=3` | Both bool selector and int value |
| Flash size | `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` + `CONFIG_ESPTOOLPY_FLASHSIZE="4MB"` | Both bool selector and string value |
| WiFi | `CONFIG_ESP_WIFI_ENABLED` | Set to `None` to disable |

The settings such a tool must never get wrong are in the platform's
[sdkconfig reference](../../../iotPlatform/docs/sdkconfig-reference.md).
