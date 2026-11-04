#include "cdx/process_query.h"
#include <tlhelp32.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef NTSTATUS (NTAPI *query_information_fn)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

typedef struct query_context {
    const cdx_process_query_request *request;
    cdx_process_query_result *result;
    query_information_fn query;
    wchar_t *terms[CDX_QUERY_MAX_TERMS];
    unsigned char *buffer;
    uint64_t deadline;
    size_t output_bytes, data_bytes;
} query_context;

static int prepare(query_context *context);
static int stopped(query_context *context);
static void observe(query_context *context, const PROCESSENTRY32W *entry);
static void release(query_context *context);

void cdx_query_processes(const cdx_process_query_request *request, cdx_process_query_result *result) {
    uint64_t started = GetTickCount64();
    query_context context;
    PROCESSENTRY32W entry;
    HANDLE snapshot = INVALID_HANDLE_VALUE;
    size_t walked;
    BOOL more;
    memset(result, 0, sizeof(*result));
    memset(&context, 0, sizeof(context));
    context.request = request; context.result = result;
    result->status = "validation_error";
    if (!prepare(&context)) goto done;
    context.deadline = started + request->timeout_ms;
    result->excluded_self_pid = request->include_self ? 0 : GetCurrentProcessId();
    if (stopped(&context)) goto done;
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        result->status = "query_error"; result->native_error = GetLastError(); goto done;
    }
    memset(&entry, 0, sizeof(entry)); entry.dwSize = sizeof(entry);
    more = Process32FirstW(snapshot, &entry);
    for (walked = 0; more && walked < request->max_processes; ++walked) {
        size_t i;
        int selected = !request->pid_count;
        if (stopped(&context)) goto done;
        for (i = 0; i < request->pid_count; ++i) if (request->pids[i] == entry.th32ProcessID) selected = 1;
        if (selected && entry.th32ProcessID != result->excluded_self_pid) {
            ++result->examined;
            observe(&context, &entry);
            if (result->stop_reason) goto done;
        }
        more = Process32NextW(snapshot, &entry);
    }
    if (more) { result->status = "partial"; result->stop_reason = "process_limit"; }
    else if (GetLastError() != ERROR_NO_MORE_FILES) {
        result->status = "query_error"; result->native_error = GetLastError();
    } else if (result->access_denied || result->unavailable || result->encoding_errors) {
        result->status = "partial"; result->stop_reason = "unreadable_processes";
    } else {
        result->scan_complete = 1;
        result->status = result->count ? "completed" : "no_match";
    }
done:
    if (snapshot && snapshot != INVALID_HANDLE_VALUE) CloseHandle(snapshot);
    release(&context);
    result->elapsed_ms = GetTickCount64() - started;
}

static int prepare(query_context *context) {
    const cdx_process_query_request *request = context->request;
    cdx_process_query_result *result = context->result;
    FARPROC symbol;
    HMODULE module;
    size_t i, total = 0;
    if (!request || !request->term_count || request->term_count > CDX_QUERY_MAX_TERMS ||
        request->pid_count > CDX_QUERY_MAX_PIDS || !request->limit || request->limit > CDX_QUERY_MAX_RESULTS ||
        !request->max_processes || request->max_processes > 4096u || !request->timeout_ms || request->timeout_ms > 10000u ||
        !request->output_limit_bytes || request->output_limit_bytes > 262144u) return 0;
    for (i = 0; i < request->pid_count; ++i) if (!request->pids[i]) return 0;
    for (i = 0; i < request->term_count; ++i) {
        int length;
        if (!request->terms[i] || !*request->terms[i] || (total += strlen(request->terms[i])) > 4096u) return 0;
        length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, request->terms[i], -1, NULL, 0);
        if (!length) { result->status = "encoding_error"; return 0; }
        context->terms[i] = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
        if (!context->terms[i]) { result->status = "io_error"; return 0; }
        (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, request->terms[i], -1, context->terms[i], length);
    }
    module = GetModuleHandleW(L"ntdll.dll");
    symbol = module ? GetProcAddress(module, "NtQueryInformationProcess") : NULL;
    if (!symbol) { result->status = "not_supported"; return 0; }
    memcpy(&context->query, &symbol, sizeof(context->query));
    context->buffer = (unsigned char *)malloc(131072u);
    result->entries = (cdx_process_entry *)calloc(request->limit, sizeof(*result->entries));
    result->command_lines = (char *)malloc(request->output_limit_bytes + 1u);
    if (!context->buffer || !result->entries || !result->command_lines) { result->status = "io_error"; return 0; }
    result->status = "completed";
    return 1;
}

static int stopped(query_context *context) {
    if (context->request->cancellation && WaitForSingleObject(context->request->cancellation, 0) == WAIT_OBJECT_0) {
        context->result->status = "cancelled"; context->result->stop_reason = "cancellation"; return 1;
    }
    if (GetTickCount64() >= context->deadline) {
        context->result->status = "timed_out"; context->result->stop_reason = "deadline"; return 1;
    }
    return 0;
}

static void observe(query_context *context, const PROCESSENTRY32W *entry) {
    cdx_process_query_result *result = context->result;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry->th32ProcessID);
    FILETIME created, exited, kernel, user;
    UNICODE_STRING *text = (UNICODE_STRING *)context->buffer;
    NTSTATUS status;
    ULONG returned = 0;
    size_t i, quoted_bytes = 2, chars;
    char *utf8 = NULL;
    int bytes, matched = 0;
    if (!process) {
        DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED) ++result->access_denied;
        else if (error == ERROR_INVALID_PARAMETER) ++result->disappeared;
        else ++result->unavailable;
        return;
    }
    status = context->query(process, (PROCESSINFOCLASS)60, context->buffer, 131072u, &returned);
    if (status < 0) {
        if ((ULONG)status == 0xc0000022u) ++result->access_denied; else ++result->unavailable;
        goto done;
    }
    /* The returned UNICODE_STRING references this bounded local query buffer. */
    if (returned < sizeof(*text) || (text->Length & 1u) || !text->Length ||
        (uintptr_t)text->Buffer < (uintptr_t)context->buffer + sizeof(*text) ||
        (uintptr_t)text->Buffer > (uintptr_t)context->buffer + 131072u - text->Length) {
        ++result->unavailable; goto done;
    }
    chars = text->Length / sizeof(wchar_t);
    for (i = 0; i < context->request->term_count; ++i) {
        if (stopped(context)) goto done;
        if (FindStringOrdinal(FIND_FROMSTART, text->Buffer, (int)chars, context->terms[i], -1, TRUE) >= 0) matched = 1;
    }
    if (!matched) goto done;
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) { ++result->unavailable; goto done; }
    bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text->Buffer, (int)chars, NULL, 0, NULL, NULL);
    if (!bytes) { ++result->encoding_errors; goto done; }
    if (context->data_bytes + (size_t)bytes + 1u > context->request->output_limit_bytes) {
        result->status = "output_limit"; result->stop_reason = "output_bytes"; goto done;
    }
    utf8 = result->command_lines + context->data_bytes;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text->Buffer, (int)chars, utf8, bytes, NULL, NULL)) {
        ++result->encoding_errors; goto done;
    }
    utf8[bytes] = 0;
    for (i = 0; i < (size_t)bytes; ++i) quoted_bytes += (unsigned char)utf8[i] < 32u ? 6u :
        utf8[i] == '"' || utf8[i] == '\\' ? 2u : 1u;
    if (context->output_bytes + quoted_bytes + 512u > context->request->output_limit_bytes) {
        result->status = "output_limit"; result->stop_reason = "output_bytes"; goto done;
    }
    if (stopped(context)) goto done;
    {
        cdx_process_entry *item = &result->entries[result->count];
        SYSTEMTIME utc;
        ULARGE_INTEGER ticks;
        ticks.LowPart = created.dwLowDateTime; ticks.HighPart = created.dwHighDateTime;
        if (!FileTimeToSystemTime(&created, &utc)) { ++result->unavailable; goto done; }
        item->pid = entry->th32ProcessID; item->parent_pid = entry->th32ParentProcessID;
        item->creation_filetime = ticks.QuadPart;
        (void)snprintf(item->created_utc, sizeof(item->created_utc), "%04u-%02u-%02uT%02u:%02u:%02u.%07lluZ",
            (unsigned)utc.wYear, (unsigned)utc.wMonth, (unsigned)utc.wDay, (unsigned)utc.wHour,
            (unsigned)utc.wMinute, (unsigned)utc.wSecond, ticks.QuadPart % 10000000u);
        item->command_line = utf8;
        context->data_bytes += (size_t)bytes + 1u;
        context->output_bytes += quoted_bytes + 512u;
        if (++result->count == context->request->limit) {
            result->status = "partial"; result->stop_reason = "result_limit";
        }
    }
done:
    CloseHandle(process);
}

static void release(query_context *context) {
    size_t i;
    for (i = 0; i < CDX_QUERY_MAX_TERMS; ++i) free(context->terms[i]);
    free(context->buffer);
}

void cdx_process_query_free(cdx_process_query_result *result) {
    free(result->command_lines);
    free(result->entries);
    memset(result, 0, sizeof(*result));
}
