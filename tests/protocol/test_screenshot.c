#define COBJMACROS
#include "cdx/screenshot.h"
#include "cdx/operations.h"
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static HANDLE ready, worker_release, worker_entered;
static HWND target_window, overlay_window;
static int checks, failures;

static void check(int passed, const char *name) {
    ++checks;
    if (!passed) { ++failures; (void)fprintf(stderr, "FAIL: %s\n", name); }
}

static void paint(HWND window, HDC dc) {
    RECT rect;
    HBRUSH brush = CreateSolidBrush(window == target_window ? RGB(220, 30, 40) : RGB(20, 40, 220));
    (void)GetClientRect(window, &rect); (void)FillRect(dc, &rect, brush); (void)DeleteObject(brush);
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_PRINT || message == WM_PRINTCLIENT) {
        paint(window, (HDC)wparam); return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(window, &ps); paint(window, dc); (void)EndPaint(window, &ps); return 0;
    }
    if (message == WM_CLOSE) { (void)DestroyWindow(window); return 0; }
    if (message == WM_DESTROY && window == target_window) { (void)DestroyWindow(overlay_window); PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}

static DWORD WINAPI fixture_thread(void *unused) {
    WNDCLASSW cls = {0};
    MSG message;
    (void)unused;
    (void)SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    cls.lpfnWndProc = window_proc; cls.hInstance = GetModuleHandleW(NULL); cls.lpszClassName = L"CordonixScreenshotFixture";
    if (!RegisterClassW(&cls)) { SetEvent(ready); return 1; }
    target_window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName, L"Cordonix 截图 fixture", WS_POPUP, 60, 60, 200, 120, NULL, NULL, cls.hInstance, NULL);
    overlay_window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName, L"Cordonix screenshot overlay", WS_POPUP, 60, 60, 200, 120, NULL, NULL, cls.hInstance, NULL);
    if (target_window && overlay_window) {
        ShowWindow(target_window, SW_SHOWNOACTIVATE); ShowWindow(overlay_window, SW_SHOWNOACTIVATE);
        UpdateWindow(target_window); UpdateWindow(overlay_window);
    }
    SetEvent(ready);
    for (unsigned i = 0; i < 10000u && GetMessageW(&message, NULL, 0, 0) > 0; ++i) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
}

static int png_pixel(const wchar_t *path, unsigned width, unsigned height, int red, int green, int blue) {
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapSource *converted = NULL;
    WICRect rect = {(INT)(width / 2u), (INT)(height / 2u), 1, 1};
    BYTE pixel[4] = {0}; UINT actual_width = 0, actual_height = 0;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&factory);
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateDecoderFromFilename(factory, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameDecode_GetSize(frame, &actual_width, &actual_height);
    if (SUCCEEDED(hr)) hr = WICConvertBitmapSource(&GUID_WICPixelFormat32bppBGRA, (IWICBitmapSource *)frame, &converted);
    if (SUCCEEDED(hr)) hr = IWICBitmapSource_CopyPixels(converted, &rect, 4, 4, pixel);
    if (converted) IWICBitmapSource_Release(converted);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    if (factory) IWICImagingFactory_Release(factory);
    return SUCCEEDED(hr) && actual_width == width && actual_height == height &&
        (red < 0 || (pixel[2] == red && pixel[1] == green && pixel[0] == blue));
}

static void protocol_validation(void) {
    static const char *const invalid[] = {
        "{}", "{\"action\":\"bad\"}", "{\"action\":\"list_windows\",\"path\":\"C:\\\\bad.png\"}",
        "{\"path\":\"C:\\\\bad.png\",\"target\":\"window\",\"window_handle\":123}",
        "{\"path\":\"C:\\\\bad.png\",\"target\":\"window\",\"window_handle\":\"18446744073709551616\"}",
        "{\"path\":\"C:\\\\bad.png\",\"target\":\"window\",\"window_handle\":\"0\"}",
        "{\"path\":\"C:\\\\bad.png\",\"method\":\"print_window\"}",
        "{\"path\":\"C:\\\\bad.png\",\"target\":\"region\",\"x\":0,\"y\":0,\"width\":0,\"height\":10}",
        "{\"path\":\"C:\\\\bad.png\",\"target\":\"region\",\"x\":0,\"y\":0,\"width\":32768,\"height\":32768}",
        "{\"path\":\"C:\\\\bad.png\",\"timeout_ms\":0}", "{\"action\":\"list_windows\",\"max_entries\":101}"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        cdx_json json; cdx_json_output output;
        int valid = cdx_json_parse(&json, invalid[i], strlen(invalid[i]));
        cdx_json_output_init(&output);
        if (valid) cdx_screenshot_call(&json, 0, NULL, &output);
        check(valid && output.data && strstr(output.data, "\"status\":\"validation_error\"") != NULL, "invalid screenshot request rejected");
        cdx_json_output_free(&output); cdx_json_free(&json);
    }
}

typedef struct cancel_request { HANDLE event; } cancel_request;
static DWORD WINAPI cancel_later(void *value) {
    cancel_request *request = (cancel_request *)value;
    (void)WaitForSingleObject(worker_entered, 3000); SetEvent(request->event);
    return 0;
}

static int run_worker_fixture(int argc, wchar_t **argv) {
    wchar_t token[64], name[128];
    if (GetEnvironmentVariableW(L"CDX_SCREENSHOT_TEST_BLOCK", token, 64)) {
        HANDLE entered, release;
        (void)swprintf_s(name, 128, L"Local\\CordonixScreenshotEntered-%s", token);
        entered = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
        (void)swprintf_s(name, 128, L"Local\\CordonixScreenshotRelease-%s", token);
        release = OpenEventW(SYNCHRONIZE, FALSE, name);
        if (!entered || !release) return 2;
        SetEvent(entered); (void)WaitForSingleObject(release, 10000);
        CloseHandle(entered); CloseHandle(release);
    }
    return cdx_screenshot_worker(argc, argv);
}

int wmain(int argc, wchar_t **argv) {
    cdx_screenshot_request request = {0};
    cdx_screenshot_result result;
    wchar_t directory[32768], path[32768];
    wchar_t token[64], name[128];
    HANDLE thread, cancel_thread, cancelled;
    cancel_request cancel;
    int found = 0;
    if (argc == 9 && !wcscmp(argv[1], L"--screenshot-worker")) return run_worker_fixture(argc, argv);
    if (argc != 2) return 2;
    (void)swprintf_s(directory, 32768, L"%s\\screenshot-%lu", argv[1], GetCurrentProcessId());
    if (!CreateDirectoryW(directory, NULL)) return 2;
    ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    (void)swprintf_s(token, 64, L"%lu-%llu", GetCurrentProcessId(), GetTickCount64());
    (void)swprintf_s(name, 128, L"Local\\CordonixScreenshotEntered-%s", token);
    worker_entered = CreateEventW(NULL, TRUE, FALSE, name);
    (void)swprintf_s(name, 128, L"Local\\CordonixScreenshotRelease-%s", token);
    worker_release = CreateEventW(NULL, TRUE, FALSE, name);
    thread = CreateThread(NULL, 0, fixture_thread, NULL, 0, NULL);
    if (!thread || WaitForSingleObject(ready, 5000) != WAIT_OBJECT_0 || !target_window || !overlay_window) return 2;
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) return 2;
    protocol_validation();
    request.timeout_ms = 5000; request.max_entries = 100; request.list_windows = 1;
    cdx_screenshot(&request, &result);
    for (size_t i = 0; i < result.count; ++i) if (result.windows[i].handle == (uint64_t)(uintptr_t)target_window && strstr(result.windows[i].title, "fixture")) found = 1;
    check(!strcmp(result.status, "ok") && found, "window listing discovers native fixture and Unicode title"); free(result.windows);
    request.max_entries = 1; cdx_screenshot(&request, &result);
    check(result.count == 1 && result.truncated, "window listing reports truncation"); free(result.windows);
    request.max_entries = 50; request.list_windows = 0; request.target = CDX_CAPTURE_WINDOW; request.print_window = 1;
    request.window = (uint64_t)(uintptr_t)target_window;
    (void)swprintf_s(path, 32768, L"%s\\原生 截图.png", directory); request.path = path;
    cdx_screenshot(&request, &result);
    if (strcmp(result.status, "ok")) (void)fprintf(stderr, "capture status=%s native_error=%lu\n", result.status, (unsigned long)result.native_error);
    check(!strcmp(result.status, "ok") && result.written_to_disk && result.cleanup_complete && result.width == 200 && result.height == 120, "PrintWindow saves a new PNG with physical dimensions");
    check(png_pixel(path, 200, 120, 220, 30, 40), "PNG decoder confirms target pixels despite overlapping window");
    cdx_screenshot(&request, &result);
    check(!strcmp(result.status, "already_exists") && !result.written_to_disk && png_pixel(path, 200, 120, 220, 30, 40), "existing screenshot is preserved");
    (void)DeleteFileW(path);
    request.window = UINTPTR_MAX;
    cdx_screenshot(&request, &result); check(!strcmp(result.status, "window_not_found") && !result.written_to_disk, "stale window handle is explicit");
    request.window = (uint64_t)(uintptr_t)target_window;
    cancelled = CreateEventW(NULL, TRUE, TRUE, NULL); request.cancellation = cancelled;
    cdx_screenshot(&request, &result); check(!strcmp(result.status, "cancelled") && !result.written_to_disk, "pre-cancelled request writes no PNG");
    ResetEvent(cancelled); request.cancellation = NULL;
    SetEnvironmentVariableW(L"CDX_SCREENSHOT_TEST_BLOCK", token); request.timeout_ms = 1000;
    cdx_screenshot(&request, &result);
    check(!strcmp(result.status, "timed_out") && !result.written_to_disk && result.cleanup_complete && result.elapsed_ms < 4000 &&
          WaitForSingleObject(worker_entered, 0) == WAIT_OBJECT_0, "blocked capture worker times out and is cleaned");
    if (result.written_to_disk) (void)DeleteFileW(path);
    request.timeout_ms = 5000; request.cancellation = cancelled; cancel.event = cancelled;
    ResetEvent(worker_entered);
    cancel_thread = CreateThread(NULL, 0, cancel_later, &cancel, 0, NULL);
    cdx_screenshot(&request, &result);
    if (strcmp(result.status, "cancelled")) (void)fprintf(stderr, "cancel status=%s native_error=%lu written=%d\n", result.status, (unsigned long)result.native_error, result.written_to_disk);
    check(!strcmp(result.status, "cancelled") && !result.written_to_disk && result.cleanup_complete, "in-flight cancellation cleans the capture worker");
    SetEnvironmentVariableW(L"CDX_SCREENSHOT_TEST_BLOCK", NULL);
    if (result.written_to_disk) (void)DeleteFileW(path);
    (void)WaitForSingleObject(cancel_thread, 5000); CloseHandle(cancel_thread); CloseHandle(cancelled); request.cancellation = NULL;
    request.target = CDX_CAPTURE_REGION; request.print_window = 0; request.window = 0;
    request.x = 0; request.y = 0; request.width = 32; request.height = 24;
    cdx_screenshot(&request, &result);
    if (strcmp(result.status, "ok")) (void)fprintf(stderr, "region status=%s native_error=%lu\n", result.status, (unsigned long)result.native_error);
    check(!strcmp(result.status, "ok") && result.written_to_disk && png_pixel(path, 32, 24, -1, 0, 0), "screen region produces a decodable PNG"); (void)DeleteFileW(path);
    request.x = -100000;
    cdx_screenshot(&request, &result); check(!strcmp(result.status, "invalid_region") && !result.written_to_disk, "off-screen region is rejected");
    request.x = 0; request.path = L"relative.png";
    cdx_screenshot(&request, &result); check(!strcmp(result.status, "validation_error") && !result.written_to_disk, "relative output path is rejected");
    request.path = L"C:\\Temp\\$(capture).png";
    cdx_screenshot(&request, &result); check(!strcmp(result.status, "validation_error") && !result.written_to_disk, "code-shaped output path is rejected");
    PostMessageW(target_window, WM_CLOSE, 0, 0);
    check(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0, "fixture windows and thread close");
    CloseHandle(thread); CloseHandle(ready); CloseHandle(worker_release); CloseHandle(worker_entered); CoUninitialize();
    check(RemoveDirectoryW(directory) != FALSE, "all temporary PNG files are cleaned");
    (void)printf("Screenshot: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
