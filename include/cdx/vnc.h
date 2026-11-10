#ifndef CDX_VNC_H
#define CDX_VNC_H

#include "cdx/screenshot.h"

typedef struct cdx_vnc_request {
    const char *host, *password, *credential, *proxy;
    const cdx_path_char *path;
    unsigned port, timeout_ms;
    int prompt_for_password;
    cdx_cancel_handle cancellation;
} cdx_vnc_request;

typedef struct cdx_vnc_result {
    cdx_screenshot_result image;
    const char *stage, *credential_status;
    char credential[320];
    unsigned security_type;
    uint32_t credential_error;
} cdx_vnc_result;

void cdx_vnc_screenshot(const cdx_vnc_request *request, cdx_vnc_result *result);
#ifdef _WIN32
int cdx_vnc_password_prompt(int argc, wchar_t **argv);
#endif
#endif
