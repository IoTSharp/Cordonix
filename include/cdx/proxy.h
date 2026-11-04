#ifndef CDX_PROXY_H
#define CDX_PROXY_H
#include "cdx/platform.h"
#include <stddef.h>
#include <stdint.h>

typedef struct cdx_proxy_request {
    const char *host;
    unsigned int port;
    const char *proxy;
    uint64_t deadline_ms;
    cdx_cancel_handle cancellation;
    int skip_direct;
    const unsigned short *candidate_ports;
    size_t candidate_count;
} cdx_proxy_request;

typedef struct cdx_proxy_result {
    uintptr_t socket;
    const char *status;
    uint32_t native_error, direct_error;
    char selected_proxy[64];
    int direct_attempted;
    unsigned short probed_ports[2];
    int probe_open[2];
    size_t probe_count;
    unsigned int tunnel_attempts;
    int cleanup_complete;
} cdx_proxy_result;

/* The caller owns Winsock initialization and the returned connected socket. */
void cdx_proxy_connect(const cdx_proxy_request *, cdx_proxy_result *);
void cdx_proxy_close(uintptr_t socket_value);
#endif
