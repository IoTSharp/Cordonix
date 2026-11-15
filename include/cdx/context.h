#ifndef CDX_CONTEXT_H
#define CDX_CONTEXT_H

#include "cdx/json.h"
#include "cdx/platform.h"

/* Deterministic host-side context helpers exposed through three small tools. */
void cdx_project_facts_call(const cdx_json *request, int arguments,
                            cdx_cancel_handle cancellation, cdx_json_output *output);
const char *cdx_project_facts_tool_schema(void);
void cdx_failure_loop_call(const cdx_json *request, int arguments,
                           cdx_cancel_handle cancellation, cdx_json_output *output);
const char *cdx_failure_loop_tool_schema(void);
void cdx_diagnostic_view_call(const cdx_json *request, int arguments,
                              cdx_cancel_handle cancellation, cdx_json_output *output);
const char *cdx_diagnostic_view_tool_schema(void);

#endif
