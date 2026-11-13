# Add attributes of your own

The standard attribute table ships with the contract (`IOT_ContractAttrs.h`: the `IOT_ATTR_*`
ids, `IOT_DEVTYPE_*` element types and value constants) and is not edited. A product with
attributes of its own registers them, in `IOT_CONTRACT_ATTR_ID_MIN`..`IOT_CONTRACT_ATTR_ID_MAX`
(`0xF700`-`0xFEFF`) — the same ids the cloud has for your product.

```c
static const uint8_t MY_MODE_DEFAULT[2] = {0x00, 0x01}; /* big-endian, fieldCount * 2 bytes */

static const IOT_ContractAttrDef_t MY_ATTRS[] = {
    {.id = 0xF701, .fieldCount = 1, .defaultValue = MY_MODE_DEFAULT, .restore = true},
    {.id = 0xF702, .fieldCount = 2},                     /* a reading: default all zeros */
};

/* in app_main, before IOT_ContractStart() */
iot_err_t err = IOT_ContractRegisterAttrs(MY_ATTRS, sizeof(MY_ATTRS) / sizeof(MY_ATTRS[0]));
```

## What a registered attribute gets

- Its size is `fieldCount * 2` bytes (`IOT_ContractAttrSize()` answers it); at most
  `IOT_CONTRACT_ATTR_FIELDS_MAX` fields.
- It starts at `defaultValue` (NULL: all zeros).
- It is never a push-notification trigger.
- History keeps up to two fields per entry.
- `restore = true` marks a control the user sets: it is replayed through `apply` at boot
  ([restore-at-boot.md](restore-at-boot.md)). The default, `false`, never is.

It then works like a standard one: `apply` receives it, `IOT_ContractReport()` /
`IOT_ContractReportFields()` report it, Record rules can match it.

## Rules

- Call it from the boot task, before `IOT_ContractStart()`; each call adds to earlier ones. The
  array is copied, so it may live on the stack.
- All or nothing: if any entry is refused, nothing in the call is registered and an error naming
  the id is logged.

| Return | Meaning |
|---|---|
| `IOT_OK` | Registered (`count` 0 does nothing) |
| `IOT_ERR_INVALID_ARG` | `defs` NULL with `count` > 0, an id outside the range, or `fieldCount` 0 or above the maximum |
| `IOT_ERR_ALREADY_EXISTS` | An id given twice, or registered by an earlier call |
| `IOT_ERR_INVALID_STATE` | Called after `IOT_ContractStart()` |
| `IOT_ERR_NO_MEM` | No memory for the copy |

- An id that is neither standard nor registered is sized as one field, with a warning logged once;
  `IOT_ContractAttrIsKnown()` tells you which ids are known.
- Do not link a copy of an attribute table of your own beside the contract's: the duplicate
  symbols fail the link.

The full guide, including choosing an id, the sizes that must agree and getting the id defined on
the cloud side, is [custom-attributes.md](../custom-attributes.md); every standard attribute is in
[attributes.md](../attributes.md).
