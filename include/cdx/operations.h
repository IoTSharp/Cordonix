#ifndef CDX_OPERATIONS_H
#define CDX_OPERATIONS_H
#include "cdx/json.h"
#include "cdx/platform.h"
void cdx_call(const char *operation, const cdx_json *request, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output);
void cdx_workspace_clear(void);
void cdx_tool_list(cdx_json_output *output);
#endif
