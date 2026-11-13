# Build a water purifier

A water purifier reports one filter per element, the water it has treated and any current issues,
and takes a "filter replaced" command from the app. The attributes are standard, so there is
nothing to register. The complete, buildable version of this page is
[examples/water-purifier](../../examples/water-purifier/main/main.c): a simulated purifier that runs
on any board.

| Constant | Id | Size | Value (big-endian) |
|---|---|---|---|
| `IOT_ATTR_INFO_PRODUCT_USAGE_TIME` | 45568 | 24 B | product id (16 B, ASCII) · expiry (4 B, Unix seconds, UTC) · time used (4 B, seconds) |
| `IOT_ATTR_INFO_WATER_CONSUMED` | 45569 | 4 B | total treated, 32-bit |
| `IOT_ATTR_INFO_CURRENT_ISSUES_STATUS` | 46096 | 4 B | issue mask, 32-bit |
| `IOT_ATTR_SETTING_PRODUCT_REPLACE` | 62624 | 22 B | new model (2 B, `0001` = a different model) · expiry (4 B, Unix seconds, UTC) · product id (16 B, ASCII) |

Sizes are the value only. The attribute id travels in front of it on the wire, but the contract
hands it to you separately and never counts it. Expiry is an absolute time, not a duration: the app
sends "now + lifetime".

Each element is one filter: product replace on element 3 is about filter 3, and so is the usage
reported on element 3.

## Report every value through the contract

The contract answers the app's state reads and sends the full state after every cloud connect
from its own store. A value you send any other way never reaches that store, so those replies
carry the join default (zero) and the app shows an empty filter that expired in 1970. Report with
the contract's calls:

```c
#include <string.h>
#include "IOT_Contract.h"
#include "IOT_Core.h"

typedef struct
{
    uint8_t  pid[16];
    uint32_t expire; /* Unix seconds */
    uint32_t usedS;
} my_filter_t;

static my_filter_t s_filter[MY_ELEMENT_COUNT]; /* index = element - 1 */

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

/* 45568 is 12 fields, more than IOT_ContractReportFields() takes: send the raw bytes. */
static iot_err_t report_filter(uint16_t elemId)
{
    uint16_t eid = 0;
    iot_err_t err = IOT_CoreGetRootEid(&eid);
    if (err != IOT_OK)
        return err;

    const my_filter_t *f = &s_filter[elemId - 1];
    uint8_t v[24];
    memcpy(v, f->pid, 16);
    put_u32(&v[16], f->expire);
    put_u32(&v[20], f->usedS);
    return IOT_ContractLocalChange(eid, elemId, IOT_ATTR_INFO_PRODUCT_USAGE_TIME, v, sizeof(v));
}

/* A 32-bit value is two 16-bit fields, high first. */
static iot_err_t report_u32(uint16_t elemId, uint16_t attrId, uint32_t value)
{
    const uint16_t fields[2] = {(uint16_t) (value >> 16), (uint16_t) value};
    return IOT_ContractReportFields(elemId, attrId, fields, 2);
}

/* water:  report_u32(1, IOT_ATTR_INFO_WATER_CONSUMED, total);
 * issues: report_u32(1, IOT_ATTR_INFO_CURRENT_ISSUES_STATUS, mask); */
```

Every report sends the device's state, so report on change and on a slow cadence, not on a fast
timer.

### Before setup, and the first connect

`IOT_ContractReportFields()` holds a value reported before the device is set up and sends it with
the first full state, so water and issues need nothing more. Product usage does not fit it (12
fields, the most is `IOT_CONTRACT_REPORT_FIELDS_MAX`), and `IOT_ContractLocalChange()` needs the
device's id, which does not exist before setup: nothing is held. Send each filter when the device
comes online. Subscribe before `IOT_ContractStart()` and hand the work to your own task, because a
subscriber runs on core's event task:

```c
static void on_status(const IOT_ContractEvent_t *event, void *arg)
{
    if (event->type == IOT_CONTRACT_EVT_STATUS_CHANGED &&
        event->data.statusChanged.status == IOT_CONTRACT_STATUS_ONLINE)
        my_post_online(); /* your task: report_filter() for every element */
}

/* in app_main, before IOT_ContractStart(): */
IOT_ContractSubscribe(on_status, NULL);
```

Online is raised on every connect. The example reads the Device store first
([read-state.md](read-state.md)) and reports only a filter the store does not already hold, so a
reconnect costs nothing; the contract's own full state carries the rest.

## Take a product replace in `apply`

The app's filter-replace screen arrives in [`apply`](apply.md) like any other setting. So do its
on/off and temperature controls, whether the app sends them as a state command or as a device
setting: an `apply` that returns `IOT_OK` without acting acknowledges commands that then do
nothing. `apply` must not block, so copy the value and refit the filter on your own task:

```c
static iot_err_t app_apply(const IOT_ContractApply_t *req)
{
    if (req->elemId < 1 || req->elemId > MY_ELEMENT_COUNT)
        return IOT_ERR_NOT_FOUND;

    switch (req->attrId)
    {
    case IOT_ATTR_SETTING_PRODUCT_REPLACE:
        /* The length is not checked for you: refuse anything but the whole value. */
        if (req->value == NULL || req->valueLen != 22)
            return IOT_ERR_INVALID_SIZE;
        my_post_refit(req->elemId, req->value); /* copies the 22 bytes; req ends with this call */
        return IOT_OK;

    case IOT_ATTR_TEMP_SET:
    {
        uint16_t celsius = 0;
        if (IOT_ContractApplyField(req, 0, &celsius) != IOT_OK)
            return IOT_ERR_INVALID_ARG;
        return my_tank_set_temperature(req->elemId, celsius);
    }

    default:
        return IOT_ERR_NOT_FOUND; /* not driven here: stored, not an error */
    }
}

/* On your task, for a posted refit: */
static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static void on_refit(uint16_t elemId, const uint8_t value[22])
{
    my_filter_t *f = &s_filter[elemId - 1];
    bool newModel  = ((value[0] << 8) | value[1]) != 0;
    f->expire      = get_u32(&value[2]);
    memcpy(f->pid, &value[6], 16);
    f->usedS = 0;
    my_filter_save(elemId, f, newModel);
    report_filter(elemId); /* the app learns the new filter from 45568 */
}
```

A product replace is never restored at boot: replaying it would reset a filter's time used.

## Check

- After setup, each element's filter card shows your product id, time used and expiry, not zeros.
- A filter replace from the app shows the new product id with time used back to 0 and the expiry it
  asked for.
- A replace of any length other than 22 bytes is refused, and the filter is unchanged.
