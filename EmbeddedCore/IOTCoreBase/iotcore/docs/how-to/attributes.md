# Use the attribute table

The attribute table ships in `sdkcontract`: `IOT_ContractAttrs.h` declares every standard
attribute id, device type and value constant, and the contract holds their sizes and defaults.
Do not write your own copy — a second definition of the table fails to link.

- **Path A:** the contract sizes and seeds every attribute for you. Register your own ids
  (`0xF700`–`0xFEFF`) with `IOT_ContractRegisterAttrs()` before `IOT_ContractStart()`.
- **Path B:** compile `sdkcontract/src/IOT_ContractAttrs.c` on its own (it needs only
  `iotcommon`, with `sdkcontract/include` and `sdkcontract/internal_include` on the include
  path), register your own ids the same way **before `IOT_CoreInit()`** (nothing closes
  registration for you on this path), and size every value with
  `IOT_ContractAttrSize()`. Defaults for the attributes your device exposes are your own helper:

```c
/* Sizes count 16-BIT FIELDS, not bytes: IOT_ContractAttrSize() returns 2*N, so a
 * single-field attribute is 2 BYTES. Write the WHOLE value as big-endian 16-bit fields. */
void fillAttrDefaultValue(uint16_t attrId, uint8_t *outValue) {
    uint16_t size = IOT_ContractAttrSize(attrId);
    memset(outValue, 0, size);
    switch (attrId) {
        case IOT_ATTR_BRIGHTNESS:
            outValue[0] = 100 >> 8;
            outValue[1] = 100 & 0xFF;
            break;
        // ... add cases whose default is not all-zero
        default:
            break;      // all-zero default already written
    }
}
```

The complete list of attribute ids, their sizes and default values is
[sdkcontract's attribute reference](../../../sdkcontract/docs/attributes.md); attributes of your own
are [custom-attributes.md](../../../sdkcontract/docs/custom-attributes.md). Core never interprets an
attribute: it hands you the id and the raw value bytes and reports back whatever bytes you give it.
