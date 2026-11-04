#ifndef CDX_HTTP_PROXY_H
#define CDX_HTTP_PROXY_H
#include "cdx/platform.h"
#include <stdint.h>

typedef struct cdx_http_proxy_bridge cdx_http_proxy_bridge;
/* Ownership of the connected socket transfers only after a successful start. */
int cdx_http_proxy_bridge_start(uintptr_t socket, const char *host, unsigned int port, int tls,
    uint64_t deadline_ms, cdx_cancel_handle cancellation, cdx_http_proxy_bridge **bridge, char proxy[64]);
int cdx_http_proxy_bridge_stop(cdx_http_proxy_bridge *bridge, uint32_t *error);
#endif
