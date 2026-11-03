#ifndef CDX_PRESENTATION_H
#define CDX_PRESENTATION_H
#include "cdx/json.h"

#define CDX_CALL_SUMMARY_BYTES 512u
#define CDX_CALL_SUMMARY_META "cordonix/summary"

void cdx_summarize_call(const char *operation, const cdx_json *json, int arguments,
                        char summary[CDX_CALL_SUMMARY_BYTES]);
int cdx_summary_progress(const cdx_json *json, int params, const char *summary, cdx_json_output *output);
void cdx_summary_metadata(const char *summary, cdx_json_output *output);
#endif
