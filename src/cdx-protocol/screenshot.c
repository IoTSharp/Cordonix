#include "cdx/operations.h"
#include "cdx/screenshot.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *text_field(const cdx_json *json, int object, const char *key, const char *fallback, int *valid) {
    int node = cdx_json_get(json, object, key);
    if (node < 0) return fallback;
    if (json->nodes[node].type != CDX_JSON_STRING) { *valid = 0; return fallback; }
    return json->nodes[node].string;
}

static int number_field(const cdx_json *json, int object, const char *key, int fallback, int minimum, int maximum, int *valid) {
    int node = cdx_json_get(json, object, key);
    double value;
    if (node < 0) return fallback;
    value = json->nodes[node].number;
    if (json->nodes[node].type != CDX_JSON_NUMBER || !isfinite(value) || value < minimum || value > maximum || floor(value) != value) {
        *valid = 0; return fallback;
    }
    return (int)value;
}

static uint64_t window_value(const char *value, int *valid) {
    uint64_t result = 0;
    unsigned base = 10, digit;
    size_t i = 0;
    if (value[0] == '0' && value[1] == 'x') { base = 16; i = 2; }
    if (!value[i]) *valid = 0;
    for (; value[i]; ++i) {
        char c = value[i];
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a') + 10u;
        else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A') + 10u;
        else { *valid = 0; return 0; }
        if (digit >= base || result > (UINTPTR_MAX - digit) / base) { *valid = 0; return 0; }
        result = result * base + digit;
    }
    if (!result) *valid = 0;
    return result;
}

static void signed_number(cdx_json_output *output, int value) {
    char text[24];
    (void)snprintf(text, sizeof(text), "%d", value); cdx_json_raw(output, text);
}

static void window_number(cdx_json_output *output, uint64_t value) {
    char text[32];
    (void)snprintf(text, sizeof(text), "%llu", (unsigned long long)value);
    cdx_json_quote(output, text, strlen(text));
}

void cdx_screenshot_call(const cdx_json *json, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output) {
    cdx_screenshot_request request;
    cdx_screenshot_result result;
    const char *action, *target, *method, *path, *window;
    cdx_path_char *native_path = NULL;
    int valid = 1;
    memset(&request, 0, sizeof(request));
    action = text_field(json, arguments, "action", "capture", &valid);
    target = text_field(json, arguments, "target", "desktop", &valid);
    path = text_field(json, arguments, "path", "", &valid);
    window = text_field(json, arguments, "window_handle", "", &valid);
    request.list_windows = !strcmp(action, "list_windows");
    if (strcmp(action, "capture") && !request.list_windows) valid = 0;
    if (!strcmp(target, "desktop")) request.target = CDX_CAPTURE_DESKTOP;
    else if (!strcmp(target, "region")) request.target = CDX_CAPTURE_REGION;
    else if (!strcmp(target, "window")) request.target = CDX_CAPTURE_WINDOW;
    else if (!strcmp(target, "active_window")) request.target = CDX_CAPTURE_ACTIVE;
    else valid = 0;
    method = text_field(json, arguments, "method", request.target >= CDX_CAPTURE_WINDOW ? "print_window" : "screen", &valid);
    request.print_window = !strcmp(method, "print_window");
    if (strcmp(method, "screen") && !request.print_window) valid = 0;
    request.timeout_ms = (unsigned)number_field(json, arguments, "timeout_ms", 10000, 1, 60000, &valid);
    request.max_entries = (size_t)number_field(json, arguments, "max_entries", 50, 1, CDX_SCREENSHOT_MAX_WINDOWS, &valid);
    request.x = number_field(json, arguments, "x", 0, -100000, 100000, &valid);
    request.y = number_field(json, arguments, "y", 0, -100000, 100000, &valid);
    request.width = number_field(json, arguments, "width", 0, 1, 32768, &valid);
    request.height = number_field(json, arguments, "height", 0, 1, 32768, &valid);
    if (*window) request.window = window_value(window, &valid);
    if (request.list_windows) {
        static const char *const capture_fields[] = {"path", "target", "method", "window_handle", "x", "y", "width", "height"};
        for (size_t i = 0; i < sizeof(capture_fields) / sizeof(capture_fields[0]); ++i)
            if (cdx_json_get(json, arguments, capture_fields[i]) >= 0) valid = 0;
    } else {
        if (!*path || (request.target == CDX_CAPTURE_WINDOW) != (*window != 0) ||
            (request.print_window && request.target < CDX_CAPTURE_WINDOW)) valid = 0;
        if (request.target == CDX_CAPTURE_REGION) {
            if (cdx_json_get(json, arguments, "x") < 0 || cdx_json_get(json, arguments, "y") < 0 ||
                !request.width || !request.height || (uint64_t)request.width * request.height > CDX_SCREENSHOT_MAX_PIXELS) valid = 0;
        } else if (cdx_json_get(json, arguments, "x") >= 0 || cdx_json_get(json, arguments, "y") >= 0 ||
                   cdx_json_get(json, arguments, "width") >= 0 || cdx_json_get(json, arguments, "height") >= 0) valid = 0;
    }
    if (!valid) {
        cdx_json_raw(output, "{\"status\":\"validation_error\",\"written_to_disk\":false,\"message\":\"Use list_windows alone, or capture with an absolute new PNG path and a matching desktop/region/window/active_window target. Window handles are decimal or 0x hexadecimal strings.\"}"); return;
    }
#ifdef _WIN32
    if (!request.list_windows) {
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
        if (length > 0 && length <= 32768) native_path = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
        if (!native_path || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, native_path, length)) valid = 0;
    }
#else
    if (!request.list_windows) {
        native_path = (char *)malloc(strlen(path) + 1u);
        if (native_path) memcpy(native_path, path, strlen(path) + 1u); else valid = 0;
    }
#endif
    if (!valid) { free(native_path); cdx_json_raw(output, "{\"status\":\"encoding_error\",\"written_to_disk\":false}"); return; }
    request.path = native_path; request.cancellation = cancellation;
    cdx_screenshot(&request, &result);
    free(native_path);
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, result.status, strlen(result.status));
    cdx_json_raw(output, ",\"written_to_disk\":"); cdx_json_raw(output, result.written_to_disk ? "true" : "false");
    cdx_json_raw(output, ",\"cleanup_complete\":"); cdx_json_raw(output, result.cleanup_complete ? "true" : "false");
    cdx_json_raw(output, ",\"native_error\":"); cdx_json_uint(output, result.native_error);
    cdx_json_raw(output, ",\"elapsed_ms\":"); cdx_json_uint(output, result.elapsed_ms);
    if (request.list_windows) {
        cdx_json_raw(output, ",\"truncated\":"); cdx_json_raw(output, result.truncated ? "true" : "false");
        cdx_json_raw(output, ",\"windows\":[");
        for (size_t i = 0; i < result.count; ++i) {
            const cdx_capture_window *entry = &result.windows[i];
            if (i) cdx_json_raw(output, ",");
            cdx_json_raw(output, "{\"window_handle\":"); window_number(output, entry->handle);
            cdx_json_raw(output, ",\"pid\":"); cdx_json_uint(output, entry->pid);
            cdx_json_raw(output, ",\"title\":"); cdx_json_quote(output, entry->title, strlen(entry->title));
            cdx_json_raw(output, ",\"minimized\":"); cdx_json_raw(output, entry->minimized ? "true" : "false");
            cdx_json_raw(output, ",\"x\":"); signed_number(output, entry->x);
            cdx_json_raw(output, ",\"y\":"); signed_number(output, entry->y);
            cdx_json_raw(output, ",\"width\":"); signed_number(output, entry->width);
            cdx_json_raw(output, ",\"height\":"); signed_number(output, entry->height); cdx_json_raw(output, "}");
        }
        cdx_json_raw(output, "]");
    } else {
        cdx_json_raw(output, ",\"path\":");
        if (result.written_to_disk) cdx_json_quote(output, path, strlen(path)); else cdx_json_raw(output, "null");
        cdx_json_raw(output, ",\"method\":"); cdx_json_quote(output, method, strlen(method));
        cdx_json_raw(output, ",\"window_handle\":"); window_number(output, result.window);
        cdx_json_raw(output, ",\"x\":"); signed_number(output, result.x);
        cdx_json_raw(output, ",\"y\":"); signed_number(output, result.y);
        cdx_json_raw(output, ",\"width\":"); signed_number(output, result.width);
        cdx_json_raw(output, ",\"height\":"); signed_number(output, result.height);
        cdx_json_raw(output, ",\"bytes\":"); cdx_json_uint(output, result.bytes);
        cdx_json_raw(output, ",\"content_verified\":false");
    }
    cdx_json_raw(output, "}"); free(result.windows);
}

const char *cdx_screenshot_tool_schema(void) {
    return "{\"name\":\"screenshot\",\"title\":\"原生窗口与屏幕截图\",\"description\":\"Native Windows PNG capture and visible top-level window listing. capture requires an absolute new .png path in an existing directory; never overwrites. target=desktop captures all monitors, region uses physical pixel x/y/width/height, window requires a string window_handle, active_window snapshots the foreground window. Windows default to print_window, desktop/region to screen. print_window requests target rendering and can capture occluded windows, but applications may return blank/incomplete content; success confirms PNG creation, not visual correctness. screen captures visible pixels, including overlapping windows, and rejects off-screen rectangles. No focus, move, restore or minimize actions and no silent method fallback. A bounded owned C child isolates synchronous capture; timeout/cancel writes no image. At most 16777216 pixels and 16 MiB PNG. list_windows returns bounded visible windows with handle/PID/title/physical bounds. Linux returns not_supported.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"capture\",\"list_windows\"],\"default\":\"capture\"},\"target\":{\"type\":\"string\",\"enum\":[\"desktop\",\"region\",\"window\",\"active_window\"],\"default\":\"desktop\"},\"path\":{\"type\":\"string\"},\"method\":{\"type\":\"string\",\"enum\":[\"screen\",\"print_window\"]},\"window_handle\":{\"type\":\"string\"},\"x\":{\"type\":\"integer\",\"minimum\":-100000,\"maximum\":100000},\"y\":{\"type\":\"integer\",\"minimum\":-100000,\"maximum\":100000},\"width\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":32768},\"height\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":32768},\"max_entries\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100,\"default\":50},\"timeout_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":60000,\"default\":10000}}},\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":false,\"idempotentHint\":false}}";
}
