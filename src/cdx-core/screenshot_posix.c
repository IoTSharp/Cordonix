#include "cdx/screenshot.h"
#include <string.h>

void cdx_screenshot(const cdx_screenshot_request *request, cdx_screenshot_result *result) {
    (void)request;
    memset(result, 0, sizeof(*result));
    result->status = "not_supported";
    result->cleanup_complete = 1;
}
