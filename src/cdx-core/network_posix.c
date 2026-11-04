#include "cdx/network.h"
#include <string.h>
void cdx_network_execute(const cdx_network_request *request, cdx_network_result *result) {
    (void)request;
    memset(result, 0, sizeof(*result));
    result->proxy.socket = UINTPTR_MAX; result->proxy.status = "not_run"; result->proxy.cleanup_complete = 1;
    result->status = "not_supported"; result->stage = "platform";
    result->mqtt_connack = -1;
    result->completion = "Windows native Winsock backend required"; result->cleanup_complete = 1;
}
void cdx_network_result_free(cdx_network_result *result) { memset(result, 0, sizeof(*result)); }
