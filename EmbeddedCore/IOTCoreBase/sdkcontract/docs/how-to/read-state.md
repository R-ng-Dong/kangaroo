# Read the stores

The public stores are read-only; every write is the contract's. The Device store
(`include/dao/IOT_DaoDeviceMgmt.h`) holds every device's identity, common info, elements and their
state, and a sub-device's mesh key. The IR store is described in [ir.md](ir.md).

## Find your own element

The Device store lists a device's elements under the ids the cloud assigned, not your element
numbers. Look yours up by `IOT_ContractElementStoreId(eid, elemId)`: the contract joins the two by
position, exactly as it does for `apply` and `IOT_ContractReport()`, so a read and a write of one
element always meet.

```c
uint16_t rootEid = 0;
IOT_DaoDeviceElemsAttrsInfo_t state = {0};
if (IOT_CoreGetRootEid(&rootEid) == IOT_OK &&
    IOT_DaoDeviceGetDeviceElemsState(rootEid, &state) == IOT_OK)
{
    uint16_t storeId = IOT_ContractElementStoreId(rootEid, MY_ELEM_RELAY);
    for (uint16_t e = 0; e < state.numOfElem; e++)
    {
        const IOT_DaoDeviceStateElemState_t *elem = &state.elemList[e];
        for (uint8_t a = 0; elem->elemId == storeId && a < elem->numOfAttr; a++)
        {
            /* elem->attrList[a].attrId, .attrSize, .attrValue (big-endian fields) */
        }
    }
    IOT_DaoDeviceFreeElemsInfo(&state);
}
```

The id comes back **unchanged** for a sub-device (its ids are its hub's), before provisioning,
with no declared elements, for an id you did not declare, and when you declared more elements than
the cloud's model has. An unchanged id is not checked against the store and may be another
element's, so trust the result only for a declared element of a provisioned device.

## The calls

| Call | Returns | Free with |
|---|---|---|
| `IOT_DaoDeviceGetAllEids(&eids, &count)` | Every device's EID (`IOT_ERR_NOT_FOUND`: none) | free the array |
| `IOT_DaoDeviceGetIdentityInfo(eid, &info)` | MAC type, MAC, device id (`IOT_DAO_DEVICE_ID_LEN` bytes) | `IOT_DaoDeviceFreeIdentityInfo()` |
| `IOT_DaoDeviceGetCommonInfo(eid, &info)` | Root EID, protocol, network address, device type | — |
| `IOT_DaoDeviceGetDeviceElemsState(eid, &info)` | Every element with its type and attributes (cached) | `IOT_DaoDeviceFreeElemsInfo()` |
| `IOT_DaoDeviceGetElementType(eid, elemId, &type)` | One element's type (cached; `elemId` is the store's id) | — |
| `IOT_DaoDeviceGetMeshKey(eid, buf, cap, &len)` | The stored mesh device key blob, opaque; size `buf` with `IOT_DAO_DEVICE_MESH_KEY_MAX_LEN` (a smaller one gets `IOT_ERR_NO_MEM`) | — |

A device that is not stored returns `IOT_ERR_DATA_NOT_FOUND`. Every read blocks on storage (the
element state on a lock, then storage on a cache miss): call them from any task, including your
core event callback, but never from a timer callback.

To find a sub-device by MAC or device id, loop over `IOT_DaoDeviceGetAllEids()` and read each with
`IOT_DaoDeviceGetIdentityInfo()`. There is no public group read: the contract resolves groups
itself.
