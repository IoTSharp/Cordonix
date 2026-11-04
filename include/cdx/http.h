#ifndef CDX_HTTP_H
#define CDX_HTTP_H

#include "cdx/platform.h"
#include "cdx/proxy.h"
#include <stddef.h>
#include <stdint.h>

#define CDX_HTTP_MAX_HEADERS 32u
#define CDX_HTTP_MAX_BODY 65536u
#define CDX_HTTP_MAX_RESPONSE 262144u

typedef struct cdx_http_header { const char *name, *value; } cdx_http_header;
typedef struct cdx_http_request {
    const char *url, *method, *proxy;
    const cdx_http_header *headers;
    size_t header_count;
    const unsigned char *body;
    size_t body_size, response_limit_bytes;
    uint32_t timeout_ms;
    cdx_cancel_handle cancellation;
    const unsigned short *proxy_ports;
    size_t proxy_port_count;
} cdx_http_request;

typedef struct cdx_http_result {
    const char *status, *stage, *message;
    uint32_t status_code, native_error;
    char *headers;
    size_t headers_size;
    unsigned char *body;
    size_t body_size;
    uint64_t elapsed_ms;
    int request_started, response_complete, cleanup_complete;
    int request_send_started, proxy_bridge_used;
    uint32_t proxy_bridge_error;
    cdx_proxy_result proxy;
} cdx_http_result;

void cdx_http_execute(const cdx_http_request *, cdx_http_result *);
void cdx_http_result_free(cdx_http_result *);
#endif
