# Restore the last state at boot

Once `IOT_ContractStart()` has started core, a provisioned device's stored controls come back to
the hardware through `apply`, with `req->source == IOT_CONTRACT_SOURCE_RESTORE`. You drive the
hardware as for any command; the value `apply` returns is logged and ignored.

## What is restored

On/off, brightness, kelvin, colour, level, fan speed and swing, settings, and your own attributes
registered with `restore = true` ([custom-attributes.md](custom-attributes.md)).

Never a lock, a motor (curtain position or command), an air conditioner (AC, mode, temperature),
an event or a reading: those keep their own state through a power cut, so replaying them could
only move or switch something unasked. A motor that cannot hold or report its own position restores
it itself (read the Device store at boot, [read-state.md](read-state.md)).

Nothing is stored, recorded, reported or passed to automations for a restore; the cloud learns the
state from the full report the device sends when it connects.

## Choose the policy

Call `IOT_ContractRestoreConfigure(policy)` before `IOT_ContractStart()`:

| Policy | At boot |
|---|---|
| `IOT_CONTRACT_RESTORE_LAST` (default) | Every control above comes back to its last value |
| `IOT_CONTRACT_RESTORE_OFF` | Every element with on/off comes back off — stored as off and applied as off — and nothing else is applied. For a heater, a pump |
| `IOT_CONTRACT_RESTORE_NONE` | The contract does nothing; restore your hardware yourself |

It returns `IOT_ERR_INVALID_ARG` for a value outside the enum (the policy is unchanged).

## Rules

- **Have your hardware ready before `IOT_ContractStart()`.** The restore runs on the task that
  delivers core events, so it never interleaves with a command — and it can arrive from the moment
  core has started, possibly before `IOT_ContractStart()` returns.
- **Lights.** A light stored off still gets its brightness, colour and kelvin restored: set them
  without switching the light on, and let on/off decide.
- **Only this device, only when provisioned.** An unprovisioned device gets no `apply` call and
  keeps its hardware's power-on state. Sub-devices are not restored.
- **Update boots too.** The restore also runs on the boot that installs an update, at its start,
  alongside the download, so lights stay as they were.
- **Power cuts.** The stored state is written back once a second, so a power cut loses at most the
  last second of changes.
