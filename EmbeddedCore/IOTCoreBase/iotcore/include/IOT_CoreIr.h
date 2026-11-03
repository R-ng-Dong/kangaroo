#pragma once
#include <inttypes.h>

#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventHandler.h"

/*
 * IR sub-device support.
 *
 * Split out of IOT_CoreReporter.h on 2026-10-06. That header remains as an umbrella that
 * includes this one, so existing includes keep working.
 */

/* ============================
 *  IR Keyset Request (to cloud)
 * ============================ */

/**
 * @brief Ask the cloud to push a sub-device's IR keyset.
 *
 * Sent at IR sub-device join so the cloud responds with the appliance's code book
 * (which the SDK stores via the IR STORE handler). Selector is the cloud device id.
 *
 * @param eid        Sub-device (appliance) EID whose keyset is needed
 * @param deviceType Appliance device type
 * @param devId      Cloud-assigned device id (selector; may be NULL)
 * @param devIdLen   Length of devId (0 if NULL)
 * @return IOT_OK on success
 */

#ifdef __cplusplus
extern "C" {
#endif
iot_err_t IOT_CoreRequestIrData(uint16_t eid, uint16_t deviceType, const uint8_t *devId, uint16_t devIdLen);

/* ============================
 *  IR detect / learn result (SDK → cloud)
 * ============================ */

/**
 * @brief Largest raw IR capture we will put on the wire, in bytes.
 *
 * The wire format's raw-data length field could describe far more than a reply can
 * actually deliver. A capture above this is refused
 * outright rather than truncated, because a frame that claims a length it does
 * not carry is worse than no frame at all.
 *
 * 2048 bytes = 1024 timing entries, which covers the long multi-section AC frames
 * (a Daikin ARC runs to roughly 600 entries). The first value here was 1024 bytes
 * = 512 entries, which would have refused those outright. The cap exists so the
 * failure is explicit rather than a malformed frame.
 */
#define IOT_IR_RAW_REPORT_MAX 2048

/**
 * @brief One outcome of an IR detect/learn window, as the SDK observed it.
 *
 * `raw` selects the reply: false → a detected-protocol reply with the protocol info
 * (and the attribute list for an AC), true → a learned-data reply carrying the timings. The
 * SDK decides which, because the protocol → device-type mapping and the
 * IOT_PROTOCOL_IR_* ids are SDK-layer knowledge; core owns only the wire format.
 */
typedef struct
{
    bool raw;               ///< true → learned timings; false → detected protocol
    uint16_t protocol;      ///< decoded protocol, or protocol+model for an AC
    uint8_t rogoIrProtocol; ///< IOT_PROTOCOL_IR_PRTC / _AC, supplied by the SDK
    uint16_t deviceType;    ///< appliance device type
    uint16_t manufacturer;  ///< appliance manufacturer id
    const uint8_t *attrs;   ///< AC state attributes (AC only); NULL otherwise
    uint16_t attrsLen;
    /* The decoded value, big-endian, ceil(bits/8) bytes — sent as the captured code.
     * Without it the reply says WHICH REMOTE was seen but not which key, and the
     * cloud has no keyset entry to store. An AC carries state rather than a value,
     * so it leaves this empty. */
    const uint8_t *code;
    uint8_t codeLen;
    const uint8_t *rawData; ///< big-endian uint16 timing pairs (raw == true)
    uint16_t rawLen;        ///< bytes in rawData; must be <= IOT_IR_RAW_REPORT_MAX
} IOT_CoreIrDetectResult_t;

/**
 * @brief Report the outcome of an IR detect/learn window to whoever asked.
 *
 * Answers on the transport the original request arrived on, via `ctx`.
 *
 * @return IOT_OK; IOT_ERR_INVALID_ARG on a NULL argument or raw mode with no data;
 *         IOT_ERR_INVALID_SIZE if rawLen exceeds IOT_IR_RAW_REPORT_MAX
 */
iot_err_t IOT_CoreReportIrDetected(const IOT_CoreIrDetectResult_t *result,
                                   const IOT_RequestContext_t *ctx);

/**
 * @brief Report that a learn window ended without a usable capture.
 *
 * Sends a failure result for the learn request carrying `errorCode`. Without it, an app that opened a window and heard nothing
 * cannot tell a timeout from a device that stopped answering — the SET was
 * already acknowledged, so silence is indistinguishable from success.
 *
 * @return IOT_OK, or IOT_ERR_INVALID_ARG when ctx is NULL
 */
iot_err_t IOT_CoreReportIrLearnFailed(uint16_t errorCode, const IOT_RequestContext_t *ctx);
#ifdef __cplusplus
}
#endif
