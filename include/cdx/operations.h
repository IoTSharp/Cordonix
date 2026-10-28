#ifndef CDX_OPERATIONS_H
#define CDX_OPERATIONS_H
#include "cdx/json.h"
#include <windows.h>
void cdx_call(const char *operation, const cdx_json *request, int arguments, HANDLE cancellation, cdx_json_output *output);
void cdx_workspace_clear(void);
const char *cdx_tool_list(void);
#endif
