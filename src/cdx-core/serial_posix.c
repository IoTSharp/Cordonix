#include "cdx/serial.h"
#include <string.h>

void cdx_serial_execute(const cdx_serial_request *request, cdx_serial_result *result) {
    (void)request;
    memset(result, 0, sizeof(*result));
    result->status = "not_supported";
    result->stop_reason = "platform";
    result->message = "Native serial communication currently uses the Windows COM API";
    result->cleanup_complete = 1;
}
void cdx_serial_result_free(cdx_serial_result *result) { memset(result, 0, sizeof(*result)); }
