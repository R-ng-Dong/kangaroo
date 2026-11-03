# Custom Attributes

Attribute ids you define yourself, for product features the published attribute list does not
cover.

[Attribute_Reference.md](Attribute_Reference.md) lists every attribute the Rogo cloud and the mobile app
already understand. This document covers the other case: your product does something none of
those ids describe, and you need an id of your own.

> **The partner id range is [`0xF700`–`0xFEFF`](#2-your-range-0xf7000xfeff)** and is
> **not subject to change** — an attribute id is written into persisted
> element state and into cloud automation data, so it is a one-way door once devices ship.

> **Working code:** **[examples/custom-attrs/](../../examples/custom-attrs/)** is this document as a buildable app — two custom
> attributes beside a Rogo one, with the five places an attribute id appears marked
> `[1]`..`[5]`.

---

## The problem

Say you are building an air purifier. Two of its features have no id in the reference:

- **Filter life remaining**, 0–100 %, reported by the device, read-only to the user.
- **Boost mode**, an enum (`off` / `30 min` / `60 min`), set by the user.

You could force these onto existing ids — `IOT_ATTR_LEVEL` for one, `IOT_ATTR_MODE` for the
other — and it will appear to work in your own testing. It will then break in ways that are hard to
trace: `IOT_ATTR_MODE` is the AC mode attribute, so an automation built against "mode" on a
different product will fire on your purifier, and the app will render an AC mode picker.

Attribute ids are one flat namespace. Reusing an id does not conflict loudly; it silently
means something else.

So you declare your own.

---

## 1. Core does not know what an attribute means

Nothing in `iotcore.a` includes an attribute header, and no core API takes an attribute
constant as a type. Core reads the id and the raw value bytes out of the message, hands them
to your event handler, and writes back whatever bytes you give it.

| Layer | Knows about your custom attribute |
|---|---|
| `iotcore.a` | **Nothing.** Moves `attrId` + bytes through, unchanged, in both directions. |
| Your SDK | Everything — the id, its size, its default, what it does to the hardware. |
| Cloud + app | Only what is defined for your product. See [§5](#5-the-cloud-half). |

The practical consequence: **you can add a custom attribute without any change to core, and
without waiting for a new `iotcore.a`.** It is entirely a change in your own SDK, plus a
model-record entry on our side.

---

## 2. Your range: `0xF700`–`0xFEFF`

Attribute ids are `uint16_t`, one flat namespace shared by device firmware, the cloud, and the
mobile app.

**`0xF700`–`0xFEFF` (63232–65279) is yours — 2048 ids.** Allocate freely inside it. Nothing
Rogo ships uses an id in that block, and the nearest allocation below it is `0xF222`, 1246
ids away, so the settings page has room to grow without ever reaching you.

**The whole `0xFF..` page above it is reserved.** That is where the wildcard `0xFFFF` lives,
along with ids whose status we are still confirming. Your range stopping at `0xFEFF` is what
keeps the rule a single sentence with no exceptions in it.

> **This range is settled and will not change.** An attribute id is written into persisted
> element state and into cloud automation data, so moving one after devices ship is a
> migration, not a recompile — which is exactly why it is fixed now rather than later.
>
> Keep your ids behind named constants in one header anyway (§3.1). That is good practice
> for reading the code, not insurance against us.

Everything outside that block is Rogo-allocated — including ids that look free:

| Range | Allocated to |
|---|---|
| `0`–`4`, `9`, `11`, `17`–`24` | Action and control attributes |
| `28`–`36` | Light attributes |
| `48`–`62`, `70`–`71` | Sensor and event attributes |
| `81`–`83`, `257`–`259` | IR and AC composite attributes |
| `90`–`92`, `462`, `800`–`801` | **Deprecated, but still reserved** — old automation data parses through these ids |
| `225`–`227` | Vendor and motor info |
| `310`–`315` | Reverse / sync-on-off |
| `320`–`332` | Step control, push notification, trigger config |
| `1024` | Smart automation temperature |
| `4096`–`4097` | Enable/disable attribute, touch setting |
| `61952`–`61986` (`0xF200`–`0xF222`) | Settings and working params |
| `65280`–`65535` (`0xFF00`–`0xFFFF`) | **Reserved page.** Holds the `0xFFFF` wildcard and ids under review. Never allocate here |

Gaps in that table are unallocated, not free. Treat anything outside `0xF700`–`0xFEFF` as
ours.

**`0xFFFF` is never an attribute**, whatever else changes. It is the all-ones value, which is
what an unset id, a "not found" result and erased flash all read as — so allocating it makes
empty storage look like a real attribute holding a real value. `0` is reserved at the other
end for the same reason.

> **Register each id with us.** Picking an id from the range is not the same as the cloud or
> the app understanding it — that needs a model-record entry, which is
> [§5](#5-the-cloud-half). Send us the id, its size, and what it means, and we will add it.
> Until then the attribute works device-locally and is invisible to the app. Registering also
> tells us which ids are actually in use, which is what lets us confirm the range.

---

## 3. Declaring one

Three edits, all in your own copy of `reference/defines/IOT_AttrDefines.{h,c}`. Core is not
touched.

### 3.1 The id

```c
// MySdkAttrDefines.h — your own attributes, in the partner range 0xF700-0xFEFF.

#define MYSDK_ATTR_BOOST_MODE       0xF701  // action, enum: 0=off, 1=30min, 2=60min
#define MYSDK_ATTR_FILTER_LIFE_PCT  0xF702  // event, 0-100 %

typedef enum
{
    MYSDK_BOOST_OFF = 0,
    MYSDK_BOOST_30_MIN,
    MYSDK_BOOST_60_MIN,
} mysdk_attr_boost_t;
```

Keep the comment that says which kind of attribute it is. An id that the user *sets* and one
that the device *reports* travel the same path but mean different things to the app, and six
months later the enum is the only record of which you intended.

### 3.2 The size

**This is the step that goes wrong.** The attribute id does not carry a length — the reader
has to know it. Get it wrong and the value is misparsed by whoever reads it.

The unit is a **16-bit field, not a byte.** Every size is `2 * N`, where N is the number of
16-bit fields in the value. A single-field attribute is **2 bytes**, not 1. Values are
**big-endian**, so a one-field value of `1` is `{0x00, 0x01}`.

```c
uint16_t IOT_AttrGetSize(uint16_t attrId)
{
    switch (attrId)
    {
    /* ... the Rogo ids your product exposes ... */

    case MYSDK_ATTR_BOOST_MODE:      return 2 * 1;  /* one enum field  */
    case MYSDK_ATTR_FILTER_LIFE_PCT: return 2 * 1;  /* one 0-100 field */

    default:                         return 2 * 1;
    }
}
```

Note the default: **`2 * 1`, two bytes.** Any attribute you do not list falls through to it,
which is correct for every single-field attribute — so you only need an explicit entry for
multi-field values.

On path B you can equally keep your custom ids in a small function of your own that handles
them and delegates everything else to the reference implementation, rather than forking its
~100-id table. The effect is identical, and it leaves one copy of the Rogo sizes to keep correct
instead of two.

**On `sdkcontract/` (path A) the custom ids must be in the table itself.** The contract is built
against the attribute table that `IOT_ATTR_DEFINES_DIR` points at, and it takes sizes and
defaults from that table when it seeds element state at join and when it stores a change. A
custom id missing from that table is stored as zero at join, however your own code handles it.
That is why `examples/custom-attrs` carries its **own copy** of the table
(`components/sdk/defines/`, its two attributes marked `THIS EXAMPLE`) and points
`IOT_ATTR_DEFINES_DIR` at it, instead of using the shared `reference/defines/`.

<a id="the-gate-crash"></a>
> **Why this matters more than it looks.** A size table whose default returns `1`, or whose
> `OPEN_CLOSE_CTL` entry returns 2 bytes instead of 4, passes every test written from that
> table, because its encoder and its decoder agree with each other. The failure appears only
> in another reader: the app reads a 4-byte value where 2 bytes were sent, and when that value
> is the last one in the message it reads past the end of the buffer and crashes.
>
> A test written from your own size table proves only that you agree with yourself. Write the
> expected sizes from `Attribute_Reference.md`.

Sizes are always even. If you find yourself wanting an odd size, you are counting bytes
instead of 16-bit fields.

**Never return 0** for an attribute that has a value. A zero size is a legal "placeholder"
all the way down — element state will store it, and core's report builder then **skips the
attribute without a word**. The device provisions, registers, and runs; the attribute simply
never appears in any report. This is the most common way a new custom attribute goes missing.

For a genuinely variable-length value, leave the id out of `IOT_AttrGetSize()` entirely and
take the length from the value itself — that is what `IOT_ATTR_SYNC_ONOFF_ELEMENTS` (315)
does.

### 3.3 The default value

What the attribute reads before anything has set it. Called when an element is first
initialised.

```c
iot_err_t IOT_AttrFillDefault(uint16_t attrId, uint8_t *buffer)
{
    switch (attrId)
    {
    /* ... */

    case MYSDK_ATTR_FILTER_LIFE_PCT:
        buffer[0] = 0;
        buffer[1] = 100;            /* big-endian: a new filter is at 100 % */
        break;

    default:
        memset(buffer, 0, IOT_AttrGetSize(attrId));  /* MYSDK_BOOST_OFF == 0 */
        break;
    }
    return IOT_OK;
}
```

The `default` branch must use `IOT_AttrGetSize()`, not a literal. A new multi-field attribute
whose default is zero then needs no entry here at all.

---

## 4. Using it

A custom attribute travels exactly the same path as a Rogo one. There is no separate API.

> **On `sdkcontract/` (path A), §4.1–§4.5 come down to two calls.** The value reaches your
> `apply` (`req->attrId` is your custom id, `req->value` / `req->valueLen` its bytes), and a
> change your hardware made by itself goes to `IOT_ContractLocalChange()`. The contract
> stores it, reports it, answers GET_STATE and feeds it to history and automations, sizing it
> from your attribute table. Range-check the length in `apply` all the same (§4.1). This is
> how `examples/custom-attrs` is built. The handlers below are what an SDK of your own
> (path B) writes; see [../../sdkcontract/README.md](../../sdkcontract/README.md) for the two
> paths.

### 4.1 Inbound — the user sets it

Your `IOT_EVENT_TYPE_STATE_SET` handler already receives it. Add a case:

```c
static iot_err_t handle_state_set(const IOT_EventStateSetRequest_t *req)
{
    const dev_set_info_t *set = &req->devSetInfo;

    /* Resolve req->targetType to EID(s) first — see the Device Control guide. */

    switch (set->attrId)
    {
    case MYSDK_ATTR_BOOST_MODE:
    {
        if (set->attrSize < 2)
        {
            return IOT_ERR_INVALID_SIZE;
        }
        uint16_t mode = (set->attrValue[0] << 8) | set->attrValue[1];
        purifier_set_boost((mysdk_attr_boost_t) mode);
        break;
    }

    case MYSDK_ATTR_FILTER_LIFE_PCT:
        return IOT_ERR_NOT_SUPPORTED;   /* report-only; the user cannot set it */

    default:
        break;
    }
    /* persist + report — 4.2, 4.3 */
}
```

**Where the inbound size comes from.** Core takes the attribute id from the first two bytes of
the value block and gives you everything after it as `attrValue` / `attrSize`. It consults no
table to do that. So an inbound value arrives correctly sized even before you have written a
size entry for it — **your table governs the outbound direction.** That asymmetry is why a
wrong size passes every round trip you can run against your own device and only fails against
the app.

Always range-check `attrSize` before reading, as above. It is wire-derived, so a malformed or
mismatched sender can make it anything.

### 4.2 Persist it

Nothing special: it is an `attrId` and bytes in your element state, stored through your device
DAO like any other attribute; see [../sdk/4_DAO_Guide.md](../sdk/4_DAO_Guide.md). On `sdkcontract/` the
contract stores it in its Device store, and there is nothing to write.

### 4.3 Report the change

Include the attribute in the state change report. `changedAttrId` carries your custom id
unchanged:

```c
IOT_CoreStateChangeDeviceData_t dev = {
    .eid              = eid,
    .mac              = mac,
    .macLen           = macLen,
    .devId            = devId,
    .devIdLen         = devIdLen,
    .fullState        = &fullState,          /* must contain the custom attribute too */
    .numChangedElems  = 1,
    .changedElemIds   = &elemId,
    .changedAttrId    = MYSDK_ATTR_BOOST_MODE,
    .pushNotify       = false,
};
IOT_CoreStateChangeReport_t report = { .devices = &dev, .numDevices = 1, .ctx = req->ctx };
IOT_CoreRequestReportStateChanged(&report);
```

`fullState` is a `devStateElemsInfo_t`, whose per-attribute entry is `elemStateAttr_t
{ attrId, attrSize, attrValue }`. **This is where your size table is used** — put
`IOT_AttrGetSize(MYSDK_ATTR_BOOST_MODE)` in `attrSize`, never a literal.

### 4.4 GET_STATE

Your `IOT_EVENT_TYPE_STATE_GET` handler returns the element's full state through
`IOT_CoreRequestReportState()`. If the custom attribute is in your stored element state, it is
included automatically — no extra case needed. If you special-case which attributes go into
the response, add yours.

### 4.5 A locally-originated change

A change the cloud did not ask for — a button press, or the filter wearing down — reports the
same way, through `IOT_CoreRequestReportStateChanged()`, with `ctx.senderFrom = 0` and
`ctx.senderId = NULL`. Set `pushNotify = true` only for something the user needs to be told
about on their phone; "filter life dropped to 5 %" qualifies, "boost mode ended" does not.

On `sdkcontract/` a local change is `IOT_ContractLocalChange()`, which reports it for you.
**Known limit:** it never raises a push notification, so a reading of the device's own (the
filter wearing down) cannot notify the user's phone through it yet. A hub's sub-device report,
`IOT_ContractSubDeviceReport()`, does take a `pushNotify` flag.

---

## 5. The cloud half

**The cloud half is staged, not all-or-nothing.** You can use a custom attribute before it
is defined on our side — what the definition adds is explained below. That matters for how
you sequence the work: you are never blocked waiting on us.

| What you want | Needs the id defined on the cloud side? |
|---|---|
| Report the attribute, and read it in **your own** app | **No** — report it and the value reaches you |
| Drive it locally: your buttons, your automations, your stored state | **No** — it never leaves the device |
| The id arriving in your device's **join**, so your element persists it | **Yes** |
| **Rogo's** app rendering a control or readout for it | **Yes** |

A device does not declare which attributes its elements have. That list comes *from the
cloud* and arrives during join as `IOT_DevJoinElmInfo_t.attrIdList[]`. Your firmware supplies
the element **ids**; the cloud supplies each element's **attributes** — and once defined it
carries custom ids there exactly as it carries ours, so no firmware change is needed to
receive one.

So while `0xF701` is undefined:

- it does not arrive in `attrIdList[]` at join, so **your seeding code never sees it**;
- the cloud does not send a SET for it, so that handler path stays untested;
- Rogo's app shows no control for it;
- **nothing logs an error** — and that is the trap. The device looks healthy either way.

> ### ⚠️ Do not make your defaults depend on the join
>
> This follows directly from the table above, and it is easy to get wrong. If your startup
> code applies an attribute's default only for ids that arrived in `attrIdList[]`, then an
> undefined attribute holds **zero** instead of its default — a brand new filter reporting
> 0 % life, a setpoint reading 0 °C — while still being reported, because your firmware
> knows it exists even though the cloud does not.
>
> Apply defaults for **every attribute your firmware exposes**, at startup, unconditionally.
> Let the join and your stored state overlay on top. `examples/custom-attrs` does this in
> `app_apply_defaults()`, called before anything else.

That symptom — new attribute, correct firmware, nothing visible in **our** app, zero errors —
is almost always a missing definition, not a firmware bug. Check that before debugging.

To get an id defined, send us:

| | |
|---|---|
| **Id** | e.g. `0xF701`, from your range |
| **Name** | `BOOST_MODE` |
| **Size** | in 16-bit fields — `2 * 1` |
| **Kind** | action (user sets it) or event (device reports it) |
| **Semantics** | the value encoding: enum members and their meanings, or the unit and range |
| **Elements** | which of your model's elements carry it |
| **Presentation** | what control or readout should be shown, if any |

Once defined, the id is carried in your device's join and in the cloud's control and state
paths like any other attribute.

**If you are building your own app** against the Rogo API rather than using ours, say so when
you send the above — reading a custom attribute back and rendering it yourself is a different
requirement from us drawing a control for it, and the two are arranged differently.

**Recommended sequence**, since nothing here blocks you: build the attribute, report it, and
read it in your own app straight away. Register the id when you want it persisted from the
join, or want it in Rogo's app. Just get your defaults right from day one — see the warning
above.

---

## 6. Checklist

- [ ] Id is inside `0xF700`–`0xFEFF`, and never `0xFFFF` or `0`.
- [ ] Every use of it goes through one named constant, so the id appears in exactly one place.
- [ ] Id is registered with Rogo, so it is defined on the cloud side.
- [ ] Size is in `IOT_AttrGetSize()` as `2 * N` — or deliberately absent, for a
      variable-length value.
- [ ] `IOT_AttrGetSize()`'s `default` returns `2 * 1` — **not 0**, which silently drops the
      attribute from every report.
- [ ] Default value is in `IOT_AttrFillDefault()`, and its `default` branch uses
      `IOT_AttrGetSize()`.
- [ ] Multi-byte values are written big-endian.
- [ ] Inbound handler range-checks `attrSize` before reading `attrValue`.
- [ ] Outbound `elemStateAttr_t.attrSize` comes from `IOT_AttrGetSize()`, not a literal.
- [ ] The attribute is in your persisted element state, so it survives a reboot and appears in
      GET_STATE.
- [ ] Sizes in your tests are written from `Attribute_Reference.md`, not copied from your own
      table.

---

## 7. When it does not work

| Symptom | Cause |
|---|---|
| Nothing visible in **Rogo's** app; no errors anywhere | Id not defined on the cloud side. Your own app can still read it ([§5](#5-the-cloud-half)) |
| Your handler is never called | Same — the cloud will not send a SET for an attribute it does not know |
| App crashes or renders garbage opening the device | Size mismatch: you reported `2 * 1` for a value the reader expects as `2 * 2`, or vice versa ([§3.2](#32-the-size)) |
| Value is right on the device, wrong in the app | Byte order. Values are big-endian; a single-field `1` is `{0x00, 0x01}` |
| Another product's automation fires on your device | You reused a Rogo id instead of allocating from your range |
| Attribute never appears in any report, although your handler clearly ran | `IOT_AttrGetSize()` returned 0 for it. Core skips zero-length attributes silently ([§3.2](#32-the-size)) |
| Attribute resets after a reboot | Not in your persisted element state ([§4.2](#42-persist-it)) |
| Value reads as 0 on a provisioned device at startup | Either the default was applied only for ids from the join ([§5](#5-the-cloud-half)), or element state was loaded before `IOT_CoreInit()` — the root EID does not exist until then |

---

## See also

- **[Attribute_Reference.md](Attribute_Reference.md)** — every Rogo-allocated attribute. Definitive.
- **[../sdk/1_Device_Control.md](../sdk/1_Device_Control.md)** — the STATE_SET / STATE_GET flow this plugs into (path B).
- **[../sdk/4_DAO_Guide.md](../sdk/4_DAO_Guide.md)** — persisting element state (path B).
- **[../../sdkcontract/README.md](../../sdkcontract/README.md)** — `apply` and `IOT_ContractLocalChange()` (path A).
- `reference/defines/IOT_AttrDefines.{h,c}` — the three helpers, as reference material to copy.
