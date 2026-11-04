#include "cdx/ssh.h"
#include <string.h>

void cdx_ssh_execute(const cdx_ssh_request *request, cdx_ssh_result *result) {
    (void)request;
    memset(result, 0, sizeof(*result));
    result->status = "not_supported";
    result->stage = "platform";
    result->message = "Native SSH currently uses the Windows CNG backend";
    result->cleanup_complete = 1;
}
void cdx_ssh_result_free(cdx_ssh_result *result) { memset(result, 0, sizeof(*result)); }
