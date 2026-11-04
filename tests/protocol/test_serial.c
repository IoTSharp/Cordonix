#define WIN32_LEAN_AND_MEAN
#include "cdx/json.h"
#include "cdx/serial.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

void cdx_serial_call(const cdx_json *, int, cdx_cancel_handle, cdx_json_output *);
const char *cdx_serial_tool_schema(void);

static unsigned int total, passed;
static uint64_t deadline;

static void check(const char *name, int success) {
    ++total; if (success) ++passed;
    (void)printf("%s %s\n", success ? "PASS" : "FAIL", name);
}

static int string_is(const cdx_json *json, int object, const char *key, const char *value) {
    int node = cdx_json_get(json, object, key);
    return node >= 0 && json->nodes[node].type == CDX_JSON_STRING && !strcmp(json->nodes[node].string, value);
}

static int boolean_is(const cdx_json *json, const char *key, int value) {
    int node = cdx_json_get(json, 0, key);
    return node >= 0 && json->nodes[node].type == CDX_JSON_BOOL && json->nodes[node].boolean == value;
}

static int call(const char *arguments, HANDLE cancel, cdx_json *result) {
    cdx_json input;
    cdx_json_output output;
    int valid;
    memset(result, 0, sizeof(*result)); memset(&input, 0, sizeof(input));
    if (GetTickCount64() >= deadline || !cdx_json_parse(&input, arguments, strlen(arguments))) return 0;
    cdx_json_output_init(&output);
    cdx_serial_call(&input, 0, cancel, &output);
    valid = output.data && !output.failed && cdx_json_parse(result, output.data, output.size);
    if (!valid && output.data) (void)fprintf(stderr, "%s\n", output.data);
    cdx_json_output_free(&output); cdx_json_free(&input);
    return valid;
}

int wmain(int argc, wchar_t **argv) {
    cdx_json result;
    HANDLE cancel = NULL, file = INVALID_HANDLE_VALUE;
    int pilot = argc == 3 && !wcscmp(argv[2], L"--pilot");
    int valid, mapped = 0;
    wchar_t port[16] = L"", mapping[512], path[32768] = L"", target[32768] = L"";
    char arguments[512], port_utf8[16];
    unsigned int selected = 0;
    if (argc < 2 || argc > 3) return 2;
    deadline = GetTickCount64() + 15000u;
    memset(&result, 0, sizeof(result));
    (void)printf("serial_fixture_owner_pid=%lu parent_resources=call_owned deadline_ms=15000\n", (unsigned long)GetCurrentProcessId());
    valid = cdx_json_parse(&result, cdx_serial_tool_schema(), strlen(cdx_serial_tool_schema()));
    check("serial tool schema is valid JSON", valid && string_is(&result, 0, "name", "serial")); cdx_json_free(&result);
    valid = call("{\"action\":\"list\",\"max_entries\":1}", NULL, &result);
    check("native serial enumeration does not open devices", valid &&
        (string_is(&result, 0, "status", "completed") || string_is(&result, 0, "status", "partial")) && boolean_is(&result, "opened", 0) && boolean_is(&result, "cleanup_complete", 1));
    cdx_json_free(&result);
    valid = call("{\"action\":\"exchange\",\"port\":\"COM1\",\"tx_hex\":\"0102\"}", NULL, &result);
    check("transmit bytes require explicit allow_send", valid && string_is(&result, 0, "status", "validation_error") && boolean_is(&result, "write_attempted", 0)); cdx_json_free(&result);
    if (pilot) goto done;
    for (unsigned int i = 0; i < 36u && GetTickCount64() < deadline; ++i) {
        (void)swprintf_s(port, 16u, L"COM%u", 65535u - i);
        if (!QueryDosDeviceW(port, mapping, 512u) && GetLastError() == ERROR_FILE_NOT_FOUND) { selected = 65535u - i; break; }
    }
    check("fixture reserves an initially absent COM mapping", selected != 0);
    if (!selected) goto done;
    (void)snprintf(port_utf8, sizeof(port_utf8), "COM%u", selected);
    (void)snprintf(arguments, sizeof(arguments), "{\"action\":\"diagnose\",\"port\":\"%s\"}", port_utf8);
    valid = call(arguments, NULL, &result);
    check("passive diagnosis reports an unavailable mapping", valid && string_is(&result, 0, "status", "not_found") && string_is(&result, 0, "availability", "unavailable") && boolean_is(&result, "opened", 0)); cdx_json_free(&result);
    cancel = CreateEventW(NULL, TRUE, TRUE, NULL);
    valid = call(arguments, cancel, &result);
    check("pre-cancellation opens no serial device", valid && string_is(&result, 0, "status", "cancelled") && boolean_is(&result, "opened", 0)); cdx_json_free(&result);
    valid = call("{\"action\":\"exchange\",\"port\":\"COM1\",\"allow_send\":true,\"tx_hex\":\"0102\"}", cancel, &result);
    check("pre-cancellation during payload preparation remains cancelled", valid && string_is(&result, 0, "status", "cancelled") && boolean_is(&result, "opened", 0) && boolean_is(&result, "write_attempted", 0)); cdx_json_free(&result);
    CloseHandle(cancel); cancel = NULL;
    {
        static const char *invalid[] = {
            "{\"action\":\"diagnose\",\"port\":\"$(COM1)\"}",
            "{\"action\":\"diagnose\",\"port\":\"COM1\\n\"}",
            "{\"action\":\"diagnose\",\"port\":\"COM0\"}",
            "{\"action\":\"diagnose\",\"port\":\"COM65536\"}",
            "{\"action\":\"exchange\",\"port\":\"COM1\",\"allow_send\":true,\"tx_hex\":\"0\"}",
            "{\"action\":\"exchange\",\"port\":\"COM1\",\"allow_send\":true,\"tx_hex\":\"0z\"}",
            "{\"action\":\"exchange\",\"port\":\"COM1\",\"allow_send\":true,\"tx_hex\":\"00 11\"}",
            "{\"action\":\"exchange\",\"port\":\"COM1\",\"baud_rate\":9600}",
            "{\"action\":\"exchange\",\"port\":\"COM1\",\"configure\":true,\"baud_rate\":0}",
            "{\"action\":\"exchange\",\"port\":\"COM1\",\"configure\":true,\"data_bits\":8,\"stop_bits\":1.5}",
            "{\"action\":\"exchange\",\"port\":\"COM1\",\"configure\":true,\"data_bits\":5,\"stop_bits\":2}",
            "{\"action\":\"exchange\",\"port\":\"COM1\",\"allow_send\":\"yes\"}",
            "{\"action\":\"list\",\"timeout_ms\":0}",
            "{\"action\":\"list\",\"max_entries\":129}"
        };
        int all = 1;
        for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]) && GetTickCount64() < deadline; ++i) {
            valid = call(invalid[i], NULL, &result);
            if (!valid || !string_is(&result, 0, "status", "validation_error") || !boolean_is(&result, "opened", 0) || !boolean_is(&result, "write_attempted", 0)) {
                all = 0; (void)fprintf(stderr, "invalid case %zu failed\n", i);
            }
            cdx_json_free(&result);
        }
        check("typed bounds, line formats, literal COM names and hex reject before opening", all && GetTickCount64() < deadline);
    }
    if (swprintf_s(path, 32768u, L"%ls\\serial-fixture-%lu.bin", argv[1], (unsigned long)GetCurrentProcessId()) < 0 ||
        swprintf_s(target, 32768u, L"\\??\\%ls", path) < 0) goto done;
    file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    check("create task-owned regular-file fixture", file != INVALID_HANDLE_VALUE);
    if (file == INVALID_HANDLE_VALUE) goto done;
    if (!DefineDosDeviceW(DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM, port, target)) {
        check("create task-owned non-hardware DOS mapping", 0); goto done;
    }
    mapped = 1;
    valid = call(arguments, NULL, &result);
    check("passive diagnosis leaves a mapped regular-file fixture unopened", valid && string_is(&result, 0, "status", "completed") &&
        string_is(&result, 0, "availability", "mapped_unprobed") && boolean_is(&result, "opened", 0) && boolean_is(&result, "settings_changed", 0)); cdx_json_free(&result);
    (void)snprintf(arguments, sizeof(arguments), "{\"action\":\"diagnose\",\"port\":\"%s\",\"open_probe\":true}", port_utf8);
    valid = call(arguments, NULL, &result);
    check("explicit open probe reports a real exclusive-sharing conflict", valid &&
        (string_is(&result, 0, "status", "busy") || string_is(&result, 0, "status", "access_denied_or_busy")) && boolean_is(&result, "opened", 0)); cdx_json_free(&result);
    CloseHandle(file); file = INVALID_HANDLE_VALUE;
    valid = call(arguments, NULL, &result);
    check("non-serial device reports native I/O error without transmission", valid && string_is(&result, 0, "status", "io_error") &&
        boolean_is(&result, "opened", 1) && boolean_is(&result, "write_attempted", 0) && boolean_is(&result, "cleanup_complete", 1)); cdx_json_free(&result);
    {
        WIN32_FILE_ATTRIBUTE_DATA attributes;
        check("fixture file remains zero bytes", GetFileAttributesExW(path, GetFileExInfoStandard, &attributes) &&
            !attributes.nFileSizeHigh && !attributes.nFileSizeLow);
    }
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (cancel) CloseHandle(cancel);
    if (mapped) check("remove exact task-owned DOS mapping", DefineDosDeviceW(DDD_RAW_TARGET_PATH | DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE | DDD_NO_BROADCAST_SYSTEM, port, target));
    if (!pilot && selected && *path) check("remove task-owned file fixture", DeleteFileW(path) || GetLastError() == ERROR_FILE_NOT_FOUND);
    check("serial native fixture stays within deadline", GetTickCount64() < deadline);
    (void)printf("{\"status\":\"%s\",\"passed\":%u,\"total\":%u,\"physical_serial\":\"NOT_RUN\"}\n", passed == total ? "PASS" : "FAIL", passed, total);
    return passed == total ? 0 : 1;
}
