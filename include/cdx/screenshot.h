#ifndef CDX_SCREENSHOT_H
#define CDX_SCREENSHOT_H

#include "cdx/platform.h"
#include <stddef.h>
#include <stdint.h>

#define CDX_SCREENSHOT_MAX_PIXELS 16777216u
#define CDX_SCREENSHOT_MAX_PNG (16u * 1024u * 1024u - 64u)
#define CDX_SCREENSHOT_MAX_WINDOWS 100u

typedef enum cdx_capture_target {
    CDX_CAPTURE_DESKTOP, CDX_CAPTURE_REGION, CDX_CAPTURE_WINDOW, CDX_CAPTURE_ACTIVE
} cdx_capture_target;

typedef struct cdx_screenshot_request {
    int list_windows;
    cdx_capture_target target;
    int print_window;
    uint64_t window;
    int x, y, width, height;
    const cdx_path_char *path;
    unsigned timeout_ms;
    size_t max_entries;
    cdx_cancel_handle cancellation;
} cdx_screenshot_request;

typedef struct cdx_capture_window {
    uint64_t handle;
    uint32_t pid;
    int x, y, width, height, minimized;
    char title[2048];
} cdx_capture_window;

typedef struct cdx_screenshot_result {
    const char *status;
    uint32_t native_error;
    int written_to_disk, cleanup_complete, truncated;
    int x, y, width, height;
    uint64_t window, elapsed_ms;
    size_t bytes, count;
    cdx_capture_window *windows;
} cdx_screenshot_result;

void cdx_screenshot(const cdx_screenshot_request *request, cdx_screenshot_result *result);
#ifdef _WIN32
struct IStream;
HRESULT cdx_screenshot_encode_png(HBITMAP bitmap, struct IStream **output);
void cdx_screenshot_publish_png(const wchar_t *path, const unsigned char *bytes, size_t size,
    uint64_t deadline, cdx_cancel_handle cancellation, cdx_screenshot_result *result);
int cdx_screenshot_worker(int argc, wchar_t **argv);
#endif
#endif
