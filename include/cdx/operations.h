#ifndef CDX_OPERATIONS_H
#define CDX_OPERATIONS_H
#include "cdx/json.h"
#include "cdx/platform.h"
void cdx_call(const char *operation, const cdx_json *request, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output);
void cdx_adapter_call(const char *operation, const cdx_json *request, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output);
void cdx_process_query_call(const cdx_json *request, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output);
void cdx_ssh_call(const cdx_json *request, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output);
void cdx_sftp_call(const cdx_json *request, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output);
const char *cdx_sftp_tool_schema(void);
void cdx_http_call(const cdx_json *request, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output);
const char *cdx_http_tool_schema(void);
void cdx_serial_call(const cdx_json *request, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output);
const char *cdx_serial_tool_schema(void);
void cdx_network_call(const cdx_json *request, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output);
const char *cdx_network_tool_schema(void);
void cdx_screenshot_call(const cdx_json *request, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output);
const char *cdx_screenshot_tool_schema(void);
void cdx_data_query_call(const cdx_json *request, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output);
const char *cdx_data_query_tool_schema(void);
void cdx_file_ops_call(const char *operation, const cdx_json *request, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output);
const char *cdx_file_ops_tool_schema(void);
void cdx_discovery_call(const cdx_json *request, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output);
const char *cdx_discovery_tool_schema(void);
void cdx_program_discovery_call(const cdx_json *request, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output);
const char *cdx_program_discovery_tool_schema(void);
void cdx_workspace_clear(void);
void cdx_tool_list(cdx_json_output *output);
#endif
