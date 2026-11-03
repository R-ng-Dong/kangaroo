#pragma once

#include <stddef.h>
#include "IOT_ErrorManager.h"

/*
 * Partner business messages -- your own channels to your backend.
 *
 * One publish call for data this protocol does not define: a periodic report, an alert
 * list, a usage counter, anything a partner's own backend wants and ours has no
 * attribute for. The payload is opaque to core. It is never framed, stamped, encrypted
 * or inspected.
 *
 * Each publish names a channel ("report", "alerts", ...), so one device can feed several
 * streams of your backend. Core supplies the rest of the routing from its own
 * configuration once the device is provisioned; you cannot get it wrong.
 *
 * Put an idempotency key in the payload. Not because the transport redelivers -- it does
 * not, see below -- but because YOUR retries after a failed publish are what create
 * duplicates. Something downstream has to be able to tell a retry from a new event.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Largest payload a single publish accepts, in bytes. */
#define IOT_SERVICE_MSG_PAYLOAD_MAX 1024

/** Longest channel name accepted, excluding the terminating NUL. */
#define IOT_SERVICE_MSG_CHANNEL_MAX_LEN 32

/**
 * @brief Publish one partner-defined payload on one of your service channels.
 *
 * The channel is used VERBATIM -- no case change, no truncation -- and validated on every
 * call, because MQTT does not validate it: an empty or malformed channel would otherwise
 * publish successfully to a destination nobody reads. Accepted characters are printable
 * ASCII excluding '/', '+' and '#'.
 *
 * Sent at QoS 0, retain 0, on the device's existing MQTT session. The payload is
 * passed through byte for byte, so binary content and embedded NULs are fine as long
 * as len is right.
 *
 * IOT_OK means the MQTT client wrote it to the socket. It does NOT mean the broker
 * received it: at QoS 0 there is no acknowledgement, and nothing will tell you later
 * that a message was lost. This return value is the only signal you get.
 *
 * When the device is offline the message is DROPPED, not queued -- core cannot decide
 * on your behalf whether a stale reading is still worth sending. Retry from your own
 * code: IOT_CoreIsCloudConnected() polls, and DEV_EVENT_CLOUD_CONNECTED /
 * DEV_EVENT_CLOUD_DISCONNECTED give you the edges.
 *
 * Publishing shares the TLS session the cloud and OTA depend on. Core does not
 * rate-limit and will not silently drop to protect the link, so pace yourself.
 *
 * BLOCKING, for up to about ten seconds: it waits for the MQTT client lock, then
 * writes TLS, and the underlying client has its own network timeout. NEVER call this
 * from a FreeRTOS timer callback -- it would stall the timer task for seconds.
 *
 * @param channel NUL-terminated channel name, 1..IOT_SERVICE_MSG_CHANNEL_MAX_LEN characters
 * @param payload partner-defined bytes, not inspected
 * @param len     length of payload, 1..IOT_SERVICE_MSG_PAYLOAD_MAX
 * @return IOT_OK handed to the MQTT client (see above for what this does not promise)
 *         IOT_ERR_INVALID_ARG channel NULL, empty or containing a rejected character;
 *                             payload NULL; or len 0
 *         IOT_ERR_INVALID_SIZE channel longer than IOT_SERVICE_MSG_CHANNEL_MAX_LEN, or len
 *                             exceeds IOT_SERVICE_MSG_PAYLOAD_MAX
 *         IOT_ERR_NOT_INITIALIZED IOT_CoreInit() never ran
 *         IOT_ERR_NOT_PROVISIONED device has not been onboarded yet; may be a long wait
 *         IOT_ERR_DATA_NOT_FOUND provisioned, but the routing configuration could not be read
 *         IOT_ERR_NO_NETWORK_CONNECT no usable session right now; retry on reconnect
 *         IOT_ERR_NO_MEM transient allocation failure resolving the topic; retry
 *         IOT_ERR_TIMEOUT another task held the topic lock too long; retry
 *         IOT_ERR_INVALID_SIZE (also) the device's routing configuration does not fit a topic
 *
 * @note IOT_CoreGetRootEid() reports "not provisioned" as IOT_ERR_DATA_NOT_FOUND, which
 *       predates IOT_ERR_NOT_PROVISIONED. Here the two are distinct: NOT_PROVISIONED
 *       means never onboarded, DATA_NOT_FOUND means onboarded but the value is missing.
 */
iot_err_t IOT_CoreServiceMsgPublish(const char *channel, const void *payload, size_t len);

#ifdef __cplusplus
}
#endif
