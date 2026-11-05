#define COBJMACROS
#include "cdx/screenshot.h"
#include "cdx/filesystem.h"
#include "cdx/process.h"
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CAPTURE_MAGIC 0x53445843u
typedef struct capture_packet {
    uint32_t magic, code, error, width, height;
    int32_t x, y;
    uint64_t window;
} capture_packet;

static const char *const capture_status[] = {
    "ok", "window_not_found", "window_minimized", "invalid_region", "too_large", "capture_failed", "encode_failed", "io_error"
};

static const char *checkpoint(const cdx_screenshot_request *request, ULONGLONG deadline);
static int valid_request(const cdx_screenshot_request *request);
static void capture(const cdx_screenshot_request *request, ULONGLONG deadline, cdx_screenshot_result *result);
static void publish_png(const cdx_screenshot_request *request, const wchar_t *path, const unsigned char *bytes,
                        size_t size, ULONGLONG deadline, cdx_screenshot_result *result);
static HRESULT encode_png(HBITMAP bitmap, IStream **output);

typedef struct window_scan {
    const cdx_screenshot_request *request;
    cdx_screenshot_result *result;
    ULONGLONG deadline;
    size_t examined;
} window_scan;

static BOOL CALLBACK enumerate_window(HWND window, LPARAM parameter) {
    window_scan *scan = (window_scan *)parameter;
    cdx_screenshot_result *result = scan->result;
    cdx_capture_window *entry;
    wchar_t title[512];
    RECT rect;
    DWORD pid = 0;
    const char *stop = checkpoint(scan->request, scan->deadline);
    if (stop) { result->status = stop; result->truncated = 1; return FALSE; }
    if (++scan->examined > 8192u) { result->status = "output_limit"; result->truncated = 1; return FALSE; }
    if (!IsWindowVisible(window) || !GetWindowRect(window, &rect)) return TRUE;
    if (result->count == scan->request->max_entries) { result->truncated = 1; return FALSE; }
    entry = &result->windows[result->count++];
    entry->handle = (uint64_t)(uintptr_t)window;
    (void)GetWindowThreadProcessId(window, &pid); entry->pid = pid;
    entry->minimized = IsIconic(window) != FALSE;
    entry->x = rect.left; entry->y = rect.top;
    entry->width = rect.right - rect.left; entry->height = rect.bottom - rect.top;
    title[0] = 0; (void)GetWindowTextW(window, title, 512);
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, title, -1, entry->title, sizeof(entry->title), NULL, NULL)) {
        result->status = "encoding_error"; result->native_error = GetLastError(); result->truncated = 1; return FALSE;
    }
    return TRUE;
}

void cdx_screenshot(const cdx_screenshot_request *request, cdx_screenshot_result *result) {
    ULONGLONG started = GetTickCount64(), deadline;
    DPI_AWARENESS_CONTEXT previous;
    memset(result, 0, sizeof(*result)); result->status = "ok"; result->cleanup_complete = 1;
    if (!valid_request(request)) { result->status = "validation_error"; return; }
    deadline = started + request->timeout_ms;
    result->status = checkpoint(request, deadline);
    if (result->status) return;
    result->status = "ok";
    previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (request->list_windows) {
        window_scan scan = {request, result, deadline, 0};
        result->windows = (cdx_capture_window *)calloc(request->max_entries, sizeof(*result->windows));
        if (!result->windows) result->status = "io_error";
        else if (!EnumWindows(enumerate_window, (LPARAM)&scan) && !result->truncated) {
            result->status = "io_error"; result->native_error = GetLastError();
        }
    } else capture(request, deadline, result);
    if (previous) (void)SetThreadDpiAwarenessContext(previous);
    result->elapsed_ms = GetTickCount64() - started;
}

static const char *checkpoint(const cdx_screenshot_request *request, ULONGLONG deadline) {
    if (request->cancellation && WaitForSingleObject(request->cancellation, 0) == WAIT_OBJECT_0) return "cancelled";
    return GetTickCount64() >= deadline ? "timed_out" : NULL;
}

static int valid_request(const cdx_screenshot_request *request) {
    if (!request || !request->timeout_ms || request->timeout_ms > 60000u || !request->max_entries ||
        request->max_entries > CDX_SCREENSHOT_MAX_WINDOWS || request->target < CDX_CAPTURE_DESKTOP ||
        request->target > CDX_CAPTURE_ACTIVE || (request->print_window != 0 && request->print_window != 1)) return 0;
    if (request->list_windows) return 1;
    if (!request->path || (request->print_window && request->target < CDX_CAPTURE_WINDOW) ||
        (request->target == CDX_CAPTURE_WINDOW && (!request->window || request->window > UINTPTR_MAX))) return 0;
    if (request->target == CDX_CAPTURE_REGION && (request->x < -100000 || request->x > 100000 ||
        request->y < -100000 || request->y > 100000 || request->width < 1 || request->height < 1 ||
        request->width > 32768 || request->height > 32768 ||
        (uint64_t)request->width * request->height > CDX_SCREENSHOT_MAX_PIXELS)) return 0;
    return 1;
}

static void capture(const cdx_screenshot_request *request, ULONGLONG deadline, cdx_screenshot_result *result) {
    wchar_t *program = NULL, *cwd, numbers[6][32];
    const wchar_t *args[8];
    wchar_t *path = NULL, *separator;
    cdx_exec_request execution;
    cdx_exec_result process;
    capture_packet packet;
    DWORD length;
    unsigned long path_error = 0;
    uint64_t window = request->target == CDX_CAPTURE_ACTIVE ? (uint64_t)(uintptr_t)GetForegroundWindow() : request->window;
    cdx_fs_status fs = cdx_fs_normalize_path(request->path, request->cancellation, &path, &path_error);
    result->native_error = (uint32_t)path_error;
    if (fs != CDX_FS_OK) { result->status = cdx_fs_status_name(fs); return; }
    length = (DWORD)wcslen(path);
    if (length < 4 || _wcsicmp(path + length - 4, L".png")) { result->status = "validation_error"; goto done; }
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) { result->status = "already_exists"; goto done; }
    program = (wchar_t *)calloc(2u * 32768u, sizeof(wchar_t));
    if (!program) { result->status = "io_error"; result->native_error = ERROR_NOT_ENOUGH_MEMORY; goto done; }
    cwd = program + 32768u;
    length = GetModuleFileNameW(NULL, program, 32768);
    if (!length || length >= 32768) { result->status = "io_error"; result->native_error = GetLastError(); goto done; }
    (void)wcscpy_s(cwd, 32768, program); separator = wcsrchr(cwd, L'\\');
    if (!separator) { result->status = "io_error"; goto done; }
    separator[1] = 0;
    (void)swprintf_s(numbers[0], 32, L"%u", request->target == CDX_CAPTURE_ACTIVE ? CDX_CAPTURE_WINDOW : (unsigned)request->target);
    (void)swprintf_s(numbers[1], 32, L"%llu", (unsigned long long)window);
    (void)swprintf_s(numbers[2], 32, L"%d", request->x); (void)swprintf_s(numbers[3], 32, L"%d", request->y);
    (void)swprintf_s(numbers[4], 32, L"%d", request->width); (void)swprintf_s(numbers[5], 32, L"%d", request->height);
    args[0] = L"--screenshot-worker"; args[1] = numbers[0]; args[2] = request->print_window ? L"print_window" : L"screen";
    args[3] = numbers[1]; args[4] = numbers[2]; args[5] = numbers[3]; args[6] = numbers[4]; args[7] = numbers[5];
    result->status = checkpoint(request, deadline);
    if (result->status) goto done;
    memset(&execution, 0, sizeof(execution));
    execution.program = program; execution.cwd = cwd; execution.args = args; execution.arg_count = 8;
    {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline) { result->status = "timed_out"; goto done; }
        execution.timeout_ms = (uint32_t)(deadline - now);
    }
    execution.output_limit_bytes = CDX_SCREENSHOT_MAX_PNG + sizeof(packet);
    execution.cancel_event = request->cancellation;
    cdx_execute(&execution, &process);
    result->cleanup_complete = process.cleanup_complete;
    result->status = cdx_status_name(process.status); result->native_error = process.win32_error;
    if (process.status == CDX_COMPLETED && process.stdout_bytes.size >= sizeof(packet)) {
        memcpy(&packet, process.stdout_bytes.data, sizeof(packet));
        if (packet.magic != CAPTURE_MAGIC || packet.code >= sizeof(capture_status) / sizeof(capture_status[0])) result->status = "capture_failed";
        else {
            result->status = capture_status[packet.code]; result->native_error = packet.error;
            result->x = packet.x; result->y = packet.y; result->width = (int)packet.width; result->height = (int)packet.height; result->window = packet.window;
            if (!packet.code) {
                size_t size = process.stdout_bytes.size - sizeof(packet);
                if (size < 24 || memcmp(process.stdout_bytes.data + sizeof(packet), "\x89PNG\r\n\x1a\n", 8)) result->status = "encode_failed";
                else publish_png(request, path, process.stdout_bytes.data + sizeof(packet), size, deadline, result);
            }
        }
    } else if (process.status == CDX_COMPLETED) result->status = "capture_failed";
    cdx_result_free(&process);
done:
    free(program); free(path);
}

static void publish_png(const cdx_screenshot_request *request, const wchar_t *path, const unsigned char *bytes,
                        size_t size, ULONGLONG deadline, cdx_screenshot_result *result) {
    wchar_t *temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD written;
    size_t capacity = wcslen(path) + 80u;
    const char *stop = checkpoint(request, deadline);
    if (stop) { result->status = stop; return; }
    temporary = (wchar_t *)calloc(capacity, sizeof(wchar_t));
    if (!temporary) { result->status = "io_error"; return; }
    (void)swprintf_s(temporary, capacity, L"%s.cdx-shot-%lu-%llu.tmp", path, GetCurrentProcessId(), GetTickCount64());
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) { result->status = "io_error"; result->native_error = GetLastError(); goto done; }
    if (!WriteFile(file, bytes, (DWORD)size, &written, NULL) || written != size || !FlushFileBuffers(file)) {
        result->status = "io_error"; result->native_error = GetLastError(); goto cleanup;
    }
    if (!CloseHandle(file)) { result->status = "io_error"; result->native_error = GetLastError(); file = INVALID_HANDLE_VALUE; goto cleanup; }
    file = INVALID_HANDLE_VALUE;
    stop = checkpoint(request, deadline);
    if (stop) { result->status = stop; goto cleanup; }
    if (!MoveFileExW(temporary, path, MOVEFILE_WRITE_THROUGH)) {
        result->native_error = GetLastError();
        result->status = result->native_error == ERROR_ALREADY_EXISTS || result->native_error == ERROR_FILE_EXISTS ? "already_exists" : "io_error";
        goto cleanup;
    }
    result->status = "ok"; result->written_to_disk = 1; result->bytes = size; goto done;
cleanup:
    if (file != INVALID_HANDLE_VALUE) (void)CloseHandle(file);
    if (!DeleteFileW(temporary)) result->cleanup_complete = 0;
done:
    free(temporary);
}

static HRESULT encode_png(HBITMAP bitmap, IStream **output) {
    IWICImagingFactory *factory = NULL;
    IWICBitmap *source = NULL;
    IWICBitmapSource *converted = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, output);
    if (SUCCEEDED(hr)) hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&factory);
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateBitmapFromHBITMAP(factory, bitmap, NULL, WICBitmapIgnoreAlpha, &source);
    if (SUCCEEDED(hr)) hr = WICConvertBitmapSource(&GUID_WICPixelFormat24bppBGR, (IWICBitmapSource *)source, &converted);
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatPng, NULL, &encoder);
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Initialize(encoder, *output, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_CreateNewFrame(encoder, &frame, NULL);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Initialize(frame, NULL);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_SetPixelFormat(frame, &format);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_WriteSource(frame, converted, NULL);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Commit(frame);
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Commit(encoder);
    if (frame) IWICBitmapFrameEncode_Release(frame);
    if (encoder) IWICBitmapEncoder_Release(encoder);
    if (converted) IWICBitmapSource_Release(converted);
    if (source) IWICBitmap_Release(source);
    if (factory) IWICImagingFactory_Release(factory);
    return hr;
}

int cdx_screenshot_worker(int argc, wchar_t **argv) {
    capture_packet packet = {0};
    RECT rect, desktop;
    HDC screen = NULL, memory = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ previous = NULL;
    BITMAPINFO info = {0};
    void *pixels;
    IStream *stream = NULL;
    HGLOBAL global = NULL;
    STATSTG stat = {0};
    HRESULT hr;
    int initialized = 0, target, printing;
    HWND window;
    DWORD written;
    if (argc != 9) return 2;
    target = _wtoi(argv[2]); printing = !wcscmp(argv[3], L"print_window");
    window = (HWND)(uintptr_t)_wcstoui64(argv[4], NULL, 10);
    if (target < CDX_CAPTURE_DESKTOP || target > CDX_CAPTURE_WINDOW ||
        (wcscmp(argv[3], L"print_window") && wcscmp(argv[3], L"screen")) || (printing && target != CDX_CAPTURE_WINDOW)) return 2;
    packet.magic = CAPTURE_MAGIC; packet.window = (uint64_t)(uintptr_t)window;
    (void)SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    desktop.left = GetSystemMetrics(SM_XVIRTUALSCREEN); desktop.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    desktop.right = desktop.left + GetSystemMetrics(SM_CXVIRTUALSCREEN); desktop.bottom = desktop.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    rect = desktop;
    if (target == CDX_CAPTURE_WINDOW) {
        if (!IsWindow(window) || !GetWindowRect(window, &rect)) { packet.code = 1; goto done; }
        if (IsIconic(window)) { packet.code = 2; goto done; }
    } else if (target == CDX_CAPTURE_REGION) {
        rect.left = _wtoi(argv[5]); rect.top = _wtoi(argv[6]);
        rect.right = rect.left + _wtoi(argv[7]); rect.bottom = rect.top + _wtoi(argv[8]);
    }
    if (rect.right <= rect.left || rect.bottom <= rect.top ||
        (!printing && (rect.left < desktop.left || rect.top < desktop.top || rect.right > desktop.right || rect.bottom > desktop.bottom))) {
        packet.code = 3; goto done;
    }
    packet.x = rect.left; packet.y = rect.top; packet.width = (uint32_t)(rect.right - rect.left); packet.height = (uint32_t)(rect.bottom - rect.top);
    if ((uint64_t)packet.width * packet.height > CDX_SCREENSHOT_MAX_PIXELS) { packet.code = 4; goto done; }
    screen = GetDC(NULL); if (!screen) { packet.code = 5; packet.error = GetLastError(); goto done; }
    memory = CreateCompatibleDC(screen); if (!memory) { packet.code = 5; packet.error = GetLastError(); goto done; }
    info.bmiHeader.biSize = sizeof(info.bmiHeader); info.bmiHeader.biWidth = (LONG)packet.width;
    info.bmiHeader.biHeight = -(LONG)packet.height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    if (!bitmap) { packet.code = 5; packet.error = GetLastError(); goto done; }
    previous = SelectObject(memory, bitmap);
    if (!previous || previous == HGDI_ERROR) { previous = NULL; packet.code = 5; packet.error = GetLastError(); goto done; }
    memset(pixels, 0, (size_t)packet.width * packet.height * 4u);
    if (!(printing ? PrintWindow(window, memory, 2u) : BitBlt(memory, 0, 0, (int)packet.width, (int)packet.height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT))) {
        packet.code = 5; packet.error = GetLastError(); goto done;
    }
    (void)GdiFlush();
    (void)SelectObject(memory, previous); previous = NULL;
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) { packet.code = 6; packet.error = (uint32_t)hr; goto done; }
    initialized = 1;
    hr = encode_png(bitmap, &stream);
    if (SUCCEEDED(hr)) hr = IStream_Stat(stream, &stat, STATFLAG_NONAME);
    if (SUCCEEDED(hr)) hr = GetHGlobalFromStream(stream, &global);
    if (FAILED(hr)) { packet.code = 6; packet.error = (uint32_t)hr; goto done; }
    if (stat.cbSize.QuadPart > CDX_SCREENSHOT_MAX_PNG) { packet.code = 4; goto done; }
done:
    if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), &packet, sizeof(packet), &written, NULL) || written != sizeof(packet)) packet.code = 7;
    if (!packet.code) {
        void *data = GlobalLock(global);
        if (!data || !WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), data, (DWORD)stat.cbSize.QuadPart, &written, NULL) || written != stat.cbSize.QuadPart) packet.code = 7;
        if (data) (void)GlobalUnlock(global);
    }
    if (stream) IStream_Release(stream);
    if (initialized) CoUninitialize();
    if (previous) (void)SelectObject(memory, previous);
    if (bitmap) (void)DeleteObject(bitmap);
    if (memory) (void)DeleteDC(memory);
    if (screen) (void)ReleaseDC(NULL, screen);
    return packet.code == 7 ? 1 : 0;
}
