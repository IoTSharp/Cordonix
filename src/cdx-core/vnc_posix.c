#include "cdx/vnc.h"
#include <string.h>

void cdx_vnc_screenshot(const cdx_vnc_request *request, cdx_vnc_result *result) {
    (void)request;
    memset(result, 0, sizeof(*result));
    result->image.status = "not_supported";
    result->image.cleanup_complete = 1;
    result->stage = "platform";
    result->credential_status = "not_used";
}
