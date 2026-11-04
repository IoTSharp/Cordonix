#include "cdx/proxy.h"
#include <string.h>
void cdx_proxy_connect(const cdx_proxy_request *request, cdx_proxy_result *result) {
    (void)request;
    memset(result, 0, sizeof(*result));
    result->socket = UINTPTR_MAX;
    result->status = "not_supported";
    result->cleanup_complete = 1;
}
void cdx_proxy_close(uintptr_t socket_value) { (void)socket_value; }
