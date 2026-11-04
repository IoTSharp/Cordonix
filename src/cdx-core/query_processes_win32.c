#include "cdx/process_query.h"
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef LONG NTSTATUS;
typedef struct process_basic_information {
    PVOID reserved1;
    PVOID peb_base_address;
    PVOID reserved2[2];
    ULONG_PTR unique_process_id;
    PVOID reserved3;
} process_basic_information;
typedef NTSTATUS (NTAPI *nt_query_information_process)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef struct unicode_string_view { USHORT length; USHORT maximum_length; PWSTR buffer; } unicode_string_view;

#ifdef _WIN64
#define PEB_PROCESS_PARAMETERS_OFFSET 0x20u
#define COMMAND_LINE_OFFSET 0x70u
#else
#define PEB_PROCESS_PARAMETERS_OFFSET 0x10u
#define COMMAND_LINE_OFFSET 0x48u
#endif

#define QUERY_MAX_ENTRIES 1000u
#define QUERY_MAX_TIMEOUT_MS 60000u
#define QUERY_MAX_OUTPUT_BYTES 262144u
#define QUERY_MAX_COMMAND_BYTES 131070u

typedef struct query_options {
    const char *name;
    const char *command_contains;
    unsigned max_entries;
    unsigned timeout_ms;
    size_t output_limit;
} query_options;

static int valid_string(const cdx_json *json, int node) {
    return node < 0 || json->nodes[node].type == CDX_JSON_STRING;
}

static int read_uint(const cdx_json *json, int object, const char *key, unsigned fallback,
                     unsigned maximum, unsigned *value) {
    int node = cdx_json_get(json, object, key);
    double number;
    if (node < 0) { *value = fallback; return 1; }
    if (json->nodes[node].type != CDX_JSON_NUMBER) return 0;
    number = json->nodes[node].number;
    if (number < 1.0 || number > (double)maximum || number != (double)(unsigned)number) return 0;
    *value = (unsigned)number;
    return 1;
}

static int read_options(const cdx_json *json, int arguments, query_options *options) {
    int name = cdx_json_get(json, arguments, "name");
    int contains = cdx_json_get(json, arguments, "command_contains");
    unsigned output_limit;
    if (!valid_string(json, name) || !valid_string(json, contains)) return 0;
    options->name = name >= 0 ? json->nodes[name].string : NULL;
    options->command_contains = contains >= 0 ? json->nodes[contains].string : NULL;
    if (!read_uint(json, arguments, "max_entries", 200u, QUERY_MAX_ENTRIES, &options->max_entries) ||
        !read_uint(json, arguments, "timeout_ms", 10000u, QUERY_MAX_TIMEOUT_MS, &options->timeout_ms) ||
        !read_uint(json, arguments, "output_limit_bytes", QUERY_MAX_OUTPUT_BYTES, QUERY_MAX_OUTPUT_BYTES, &output_limit)) return 0;
    options->output_limit = output_limit;
    return 1;
}

static uint64_t now_ms(void) { return GetTickCount64(); }

static const char *stop_reason(cdx_cancel_handle cancellation, uint64_t deadline) {
    if (cancellation && WaitForSingleObject(cancellation, 0) == WAIT_OBJECT_0) return "cancelled";
    return now_ms() >= deadline ? "timed_out" : NULL;
}

static char *utf8_from_wide(const wchar_t *text, size_t length) {
    int count;
    char *result;
    if (length > INT_MAX) return NULL;
    count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, (int)length, NULL, 0, NULL, NULL);
    if (!count) return NULL;
    result = (char *)malloc((size_t)count + 1u);
    if (!result) return NULL;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, (int)length, result, count, NULL, NULL) != count) {
        free(result);
        return NULL;
    }
    result[count] = '\0';
    return result;
}

static char *process_command_line(HANDLE process, nt_query_information_process query) {
    process_basic_information basic;
    ULONG returned = 0;
    PVOID parameters = NULL;
    unicode_string_view command;
    wchar_t *wide;
    char *result;
    SIZE_T read = 0;
    NTSTATUS status;
    if (!query) return NULL;
    status = query(process, 0u, &basic, (ULONG)sizeof(basic), &returned);
    if (status < 0 || !basic.peb_base_address) return NULL;
    if (!ReadProcessMemory(process, (const unsigned char *)basic.peb_base_address + PEB_PROCESS_PARAMETERS_OFFSET,
                           &parameters, sizeof(parameters), &read) || read != sizeof(parameters) || !parameters) return NULL;
    if (!ReadProcessMemory(process, (const unsigned char *)parameters + COMMAND_LINE_OFFSET,
                           &command, sizeof(command), &read) || read != sizeof(command) ||
        !command.buffer || !command.length || command.length > QUERY_MAX_COMMAND_BYTES ||
        command.length & 1u) return NULL;
    wide = (wchar_t *)malloc((size_t)command.length + sizeof(wchar_t));
    if (!wide) return NULL;
    if (!ReadProcessMemory(process, command.buffer, wide, command.length, &read) || read != command.length) {
        free(wide);
        return NULL;
    }
    wide[command.length / sizeof(wchar_t)] = L'\0';
    result = utf8_from_wide(wide, command.length / sizeof(wchar_t));
    free(wide);
    return result;
}

static char *process_path(HANDLE process) {
    DWORD length = 32768u;
    wchar_t *wide = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
    char *result = NULL;
    if (!wide) return NULL;
    if (QueryFullProcessImageNameW(process, 0u, wide, &length)) result = utf8_from_wide(wide, length);
    free(wide);
    return result;
}

static int contains_ascii_insensitive(const char *text, const char *needle) {
    size_t i, j, text_length, needle_length;
    if (!needle || !needle[0]) return 1;
    if (!text) return 0;
    text_length = strlen(text); needle_length = strlen(needle);
    if (needle_length > text_length) return 0;
    for (i = 0; i + needle_length <= text_length; ++i) {
        for (j = 0; j < needle_length; ++j) {
            unsigned char left = (unsigned char)text[i + j];
            unsigned char right = (unsigned char)needle[j];
            if (left >= 'A' && left <= 'Z') left = (unsigned char)(left + ('a' - 'A'));
            if (right >= 'A' && right <= 'Z') right = (unsigned char)(right + ('a' - 'A'));
            if (left != right) break;
        }
        if (j == needle_length) return 1;
    }
    return 0;
}

static int equal_ascii_insensitive(const char *left, const char *right) {
    size_t i;
    if (!left || !right) return 0;
    if (strlen(left) != strlen(right)) return 0;
    for (i = 0; left[i]; ++i) {
        unsigned char a = (unsigned char)left[i], b = (unsigned char)right[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

void cdx_query_processes_call(const cdx_json *json, int arguments,
                              cdx_cancel_handle cancellation, cdx_json_output *output) {
    query_options options;
    HANDLE snapshot = INVALID_HANDLE_VALUE;
    HMODULE ntdll = NULL;
    nt_query_information_process query = NULL;
    PROCESSENTRY32W entry;
    uint64_t deadline;
    unsigned returned = 0, skipped = 0, inaccessible = 0;
    int first = 1, stop = 0, output_limit = 0, entry_limit = 0;
    const char *status = "ok";
    const char *message = NULL;
    if (arguments < 0 || !read_options(json, arguments, &options)) {
        cdx_json_raw(output, "{\"status\":\"validation_error\",\"message\":\"name and command_contains must be strings; numeric limits are bounded positive integers\"}");
        return;
    }
    deadline = now_ms() + options.timeout_ms;
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0u);
    if (snapshot == INVALID_HANDLE_VALUE) {
        cdx_json_raw(output, "{\"status\":\"failed\",\"message\":\"Cannot enumerate processes\",\"win32_error\":");
        cdx_json_uint(output, GetLastError()); cdx_json_raw(output, "}");
        return;
    }
    ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) query = (nt_query_information_process)GetProcAddress(ntdll, "NtQueryInformationProcess");
    memset(&entry, 0, sizeof(entry)); entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        CloseHandle(snapshot);
        cdx_json_raw(output, "{\"status\":\"failed\",\"message\":\"Cannot read the process snapshot\",\"win32_error\":");
        cdx_json_uint(output, GetLastError()); cdx_json_raw(output, "}");
        return;
    }
    cdx_json_raw(output, "{\"entries\":[");
    if (output->size > options.output_limit) output_limit = 1;
    do {
        HANDLE process = NULL;
        char *name = NULL, *path = NULL, *command = NULL;
        cdx_json_output entry_output;
        uint64_t creation_filetime = 0;
        int creation_known = 0;
        const char *reason = stop_reason(cancellation, deadline);
        if (reason) { status = reason; stop = 1; break; }
        if (output_limit) break;
        if (returned >= options.max_entries) { entry_limit = 1; break; }
        name = utf8_from_wide(entry.szExeFile, wcslen(entry.szExeFile));
        if (!name) { ++inaccessible; continue; }
        if (options.name && !equal_ascii_insensitive(name, options.name)) { free(name); continue; }
        process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID);
        if (process) {
            FILETIME created, exited, kernel, user;
            path = process_path(process);
            command = process_command_line(process, query);
            if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
                ULARGE_INTEGER value;
                value.LowPart = created.dwLowDateTime;
                value.HighPart = created.dwHighDateTime;
                creation_filetime = value.QuadPart;
                creation_known = 1;
            }
            CloseHandle(process);
        } else ++inaccessible;
        if (options.command_contains && !contains_ascii_insensitive(command, options.command_contains)) {
            if (!command) ++skipped;
            free(name); free(path); free(command); continue;
        }
        cdx_json_output_init(&entry_output);
        cdx_json_raw(&entry_output, "{\"pid\":"); cdx_json_uint(&entry_output, entry.th32ProcessID);
        cdx_json_raw(&entry_output, ",\"parent_pid\":"); cdx_json_uint(&entry_output, entry.th32ParentProcessID);
        cdx_json_raw(&entry_output, ",\"name\":"); cdx_json_quote(&entry_output, name, strlen(name));
        cdx_json_raw(&entry_output, ",\"executable_path\":");
        if (path) cdx_json_quote(&entry_output, path, strlen(path)); else cdx_json_raw(&entry_output, "null");
        cdx_json_raw(&entry_output, ",\"command_line\":");
        if (command) cdx_json_quote(&entry_output, command, strlen(command)); else cdx_json_raw(&entry_output, "null");
        cdx_json_raw(&entry_output, ",\"creation_filetime\":");
        if (creation_known) cdx_json_uint(&entry_output, creation_filetime); else cdx_json_raw(&entry_output, "null");
        cdx_json_raw(&entry_output, "}");
        if (entry_output.failed || output->size + entry_output.size + (first ? 0u : 1u) + 2u > options.output_limit) {
            output_limit = 1;
            cdx_json_output_free(&entry_output);
            free(name); free(path); free(command); break;
        }
        if (!first) cdx_json_raw(output, ","); first = 0;
        cdx_json_rawn(output, entry_output.data, entry_output.size);
        cdx_json_output_free(&entry_output);
        ++returned;
        free(name); free(path); free(command);
    } while (Process32NextW(snapshot, &entry));
    if (!stop && !output_limit && !entry_limit && GetLastError() != ERROR_NO_MORE_FILES) { status = "partial"; message = "Process enumeration ended before the snapshot was complete"; }
    if (output_limit || entry_limit) status = "output_limit";
    if (!strcmp(status, "ok") && inaccessible) { status = "partial"; message = "Some processes could not be opened or decoded"; }
    cdx_json_raw(output, "],\"returned\":"); cdx_json_uint(output, returned);
    cdx_json_raw(output, ",\"skipped\":"); cdx_json_uint(output, skipped);
    cdx_json_raw(output, ",\"inaccessible\":"); cdx_json_uint(output, inaccessible);
    cdx_json_raw(output, ",\"truncated\":"); cdx_json_raw(output, (output_limit || entry_limit) ? "true" : "false");
    cdx_json_raw(output, ",\"max_entries\":"); cdx_json_uint(output, options.max_entries);
    cdx_json_raw(output, ",\"status\":"); cdx_json_quote(output, status, strlen(status));
    if (message) { cdx_json_raw(output, ",\"message\":"); cdx_json_quote(output, message, strlen(message)); }
    if (entry_limit) cdx_json_raw(output, ",\"max_entries_reached\":true");
    if (output_limit) { cdx_json_raw(output, ",\"max_bytes\":"); cdx_json_uint(output, options.output_limit); }
    cdx_json_raw(output, "}");
    CloseHandle(snapshot);
}
