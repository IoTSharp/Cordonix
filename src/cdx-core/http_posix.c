#include "cdx/http.h"
#include <string.h>

void cdx_http_execute(const cdx_http_request *request, cdx_http_result *result) {
    (void)request;
    memset(result, 0, sizeof(*result));
    result->status = "not_supported"; result->stage = "platform";
    result->message = "Native HTTP currently uses Windows WinHTTP";
    result->cleanup_complete = 1;
    result->proxy.socket = UINTPTR_MAX; result->proxy.status = "not_run"; result->proxy.cleanup_complete = 1;
}
void cdx_http_result_free(cdx_http_result *result) { memset(result, 0, sizeof(*result)); }
