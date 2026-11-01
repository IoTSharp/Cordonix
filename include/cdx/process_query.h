#ifndef CDX_PROCESS_QUERY_H
#define CDX_PROCESS_QUERY_H

#include "cdx/json.h"
#include "cdx/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

void cdx_query_processes_call(const cdx_json *request, int arguments,
                              cdx_cancel_handle cancellation, cdx_json_output *output);

#ifdef __cplusplus
}
#endif

#endif
