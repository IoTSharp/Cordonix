#ifndef CDX_NETWORK_H
#define CDX_NETWORK_H
#include "cdx/platform.h"
#include "cdx/proxy.h"
#include <stddef.h>
#include <stdint.h>

#define CDX_NETWORK_MAX_BYTES 65536u
typedef struct cdx_network_request {
    const char *protocol, *action, *host, *client_id, *topic, *proxy;
    unsigned int port;
    const unsigned char *payload;
    size_t payload_size, read_limit_bytes;
    uint32_t timeout_ms, idle_timeout_ms;
    int allow_send, retain, tls;
    cdx_cancel_handle cancellation;
} cdx_network_request;
typedef struct cdx_network_result {
    const char *status, *stage, *completion;
    unsigned char *data;
    size_t size, payload_bytes_sent, wire_bytes_sent;
    uint64_t elapsed_ms;
    int connected, mqtt_connack, application_write_attempted, cleanup_complete;
    cdx_proxy_result proxy;
} cdx_network_result;
void cdx_network_execute(const cdx_network_request *, cdx_network_result *);
void cdx_network_result_free(cdx_network_result *);
#endif
