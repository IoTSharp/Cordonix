#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "cdx/http.h"
#include "cdx/http_proxy.h"
#include <winhttp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define HTTP_HEADER_BYTES 16384u
#define HTTP_RESPONSE_HEADERS 65536u
#define HTTP_POLLS 30002u

typedef struct http_async {
    HANDLE ready, closed;
    volatile LONG references, notification;
    volatile LONG connected, send_started, secure_failure;
    DWORD error, bytes;
    unsigned char read_buffer[8192];
    unsigned char body_buffer[CDX_HTTP_MAX_BODY];
} http_async;

typedef struct http_context {
    const cdx_http_request *request;
    cdx_http_result *result;
    uint64_t deadline;
    uint64_t direct_deadline;
    unsigned int port;
    int tls, automatic, defer_transport;
    int loopback_target;
    int connected_snapshot, secure_failure_snapshot;
    http_async *async;
    HINTERNET session, connection, handle;
    wchar_t *url, *method, *host, *resource, *proxy, *headers;
} http_context;

static int prepare(http_context *);
static int exchange(http_context *);
static void cleanup(http_context *);
static int proxy_attempt(const cdx_http_request *, cdx_http_result *, const char *, unsigned int, int, uint64_t, int);

static int automatic_proxy(const char *proxy) { return !*proxy || !strcmp(proxy, "auto"); }

static int fallback_allowed(const http_context *context) {
    DWORD error = context->result->native_error;
    const http_async *state = context->async;
    return context->automatic && !context->loopback_target && state && !state->send_started && !state->connected && !state->secure_failure &&
        !context->result->status_code && strcmp(context->result->status, "cancelled") && GetTickCount64() < context->deadline &&
        (error == ERROR_WINHTTP_NAME_NOT_RESOLVED || error == ERROR_WINHTTP_CANNOT_CONNECT ||
         error == ERROR_WINHTTP_CONNECTION_ERROR || error == ERROR_WINHTTP_TIMEOUT);
}

void cdx_http_execute(const cdx_http_request *request, cdx_http_result *result) {
    http_context context;
    uint64_t started = GetTickCount64();
    if (!result) return;
    memset(result, 0, sizeof(*result)); memset(&context, 0, sizeof(context));
    context.request = request; context.result = result;
    result->status = "validation_error"; result->stage = "validation";
    result->message = "Invalid HTTP URL, method, headers, body, proxy or bounds"; result->cleanup_complete = 1;
    result->proxy.socket = UINTPTR_MAX; result->proxy.status = "not_run"; result->proxy.cleanup_complete = 1;
    if (request && request->proxy) {
        context.deadline = started + request->timeout_ms;
        context.automatic = automatic_proxy(request->proxy);
        context.defer_transport = !strncmp(request->proxy, "socks5://", 9u);
        result->proxy.direct_attempted = context.automatic || !strcmp(request->proxy, "direct");
    }
    if (prepare(&context)) {
        if (!context.defer_transport) {
            if (context.automatic || !strcmp(request->proxy, "direct"))
                (void)strcpy_s(result->proxy.selected_proxy, sizeof(result->proxy.selected_proxy), "direct");
            else (void)_snprintf_s(result->proxy.selected_proxy, sizeof(result->proxy.selected_proxy), _TRUNCATE,
                !strncmp(request->proxy, "http://", 7u) ? "%s" : "http://%s", request->proxy);
            result->proxy.status = "completed";
            (void)exchange(&context);
        }
        if (context.defer_transport || fallback_allowed(&context)) {
            int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, context.host, -1, NULL, 0, NULL, NULL);
            char *host = bytes > 0 ? (char *)calloc((size_t)bytes, 1) : NULL;
            unsigned int port = context.port;
            int tls = context.tls;
            uint64_t deadline = context.deadline;
            if (host) (void)WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, context.host, -1, host, bytes, NULL, NULL);
            if (host && host[0] == '[' && strlen(host) >= 2u && host[strlen(host)-1u] == ']') {
                size_t length = strlen(host);
                memmove(host, host + 1, length - 2u); host[length - 2u] = 0;
            }
            if (context.automatic) result->proxy.direct_error = result->native_error;
            cleanup(&context);
            if (host && result->cleanup_complete && (context.defer_transport ||
                (!result->request_send_started && !context.connected_snapshot && !context.secure_failure_snapshot)))
                (void)proxy_attempt(request, result, host, port, tls, deadline, context.automatic);
            free(host);
            result->elapsed_ms = GetTickCount64() - started;
            return;
        }
    }
    else if (!strcmp(result->status, "connection_error")) result->native_error = GetLastError();
    cleanup(&context);
    if (result->proxy.selected_proxy[0]) {
        result->proxy.status = result->status; result->proxy.native_error = result->native_error;
        if (result->proxy.direct_attempted) result->proxy.direct_error = result->native_error;
    }
    result->elapsed_ms = GetTickCount64() - started;
}

void cdx_http_result_free(cdx_http_result *result) {
    if (!result) return;
    free(result->headers); free(result->body); memset(result, 0, sizeof(*result));
}

static wchar_t *wide(const char *text) {
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    wchar_t *value = length ? (wchar_t *)calloc((size_t)length, sizeof(*value)) : NULL;
    if (value) (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, value, length);
    return value;
}

static wchar_t *slice(const wchar_t *text, size_t size) {
    wchar_t *value = (wchar_t *)calloc(size + 1u, sizeof(*value));
    if (value && size) memcpy(value, text, size * sizeof(*value));
    return value;
}

static int loopback_target(const wchar_t *host) {
    IN_ADDR ipv4;
    IN6_ADDR ipv6;
    size_t length = wcslen(host);
    wchar_t literal[80];
    static const unsigned char local6[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    if (!_wcsicmp(host, L"localhost") || (length >= 10u && !_wcsicmp(host + length - 10u, L".localhost"))) return 1;
    if (length >= 2u && length < 80u && host[0] == '[' && host[length-1u] == ']') {
        memcpy(literal, host + 1, (length - 2u) * sizeof(wchar_t)); literal[length - 2u] = 0; host = literal;
    }
    if (InetPtonW(AF_INET, host, &ipv4) == 1) return (ntohl(ipv4.S_un.S_addr) >> 24) == 127u;
    if (InetPtonW(AF_INET6, host, &ipv6) == 1) return !memcmp(&ipv6, local6, sizeof(local6));
    return 0;
}

static int valid_header_name(const char *name) {
    size_t length = strlen(name);
    if (!length || length > 256u) return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && !strchr("!#$%&'*+-.^_`|~", c)) return 0;
    }
    return 1;
}

static int valid_request(const cdx_http_request *request) {
    static const char *methods[] = {"GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"};
    size_t total = 0;
    int method_found = 0;
    if (!request || !request->url || !request->method || !request->proxy ||
        strlen(request->url) > 8192u || strpbrk(request->url, "\r\n\t#") ||
        (strncmp(request->url, "http://", 7u) && strncmp(request->url, "https://", 8u)) ||
        strlen(request->proxy) > 4096u || strpbrk(request->proxy, "\r\n\t@") ||
        request->header_count > CDX_HTTP_MAX_HEADERS || (request->header_count && !request->headers) ||
        request->body_size > CDX_HTTP_MAX_BODY || (request->body_size && !request->body) ||
        !request->response_limit_bytes || request->response_limit_bytes > CDX_HTTP_MAX_RESPONSE ||
        !request->timeout_ms || request->timeout_ms > 300000u || request->proxy_port_count > 2u ||
        (request->proxy_port_count && !request->proxy_ports)) return 0;
    for (size_t i = 0; i < request->proxy_port_count; ++i) if (!request->proxy_ports[i]) return 0;
    if ((!strcmp(request->method, "GET") || !strcmp(request->method, "HEAD")) && request->body_size) return 0;
    for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) if (!strcmp(request->method, methods[i])) method_found = 1;
    if (!method_found) return 0;
    for (size_t i = 0; i < request->header_count; ++i) {
        const cdx_http_header *header = &request->headers[i];
        size_t size;
        if (!header->name || !header->value || !valid_header_name(header->name) || strpbrk(header->value, "\r\n")) return 0;
        size = strlen(header->name) + strlen(header->value) + 4u;
        if (size > HTTP_HEADER_BYTES - total) return 0;
        total += size;
        if (!_stricmp(header->name, "Content-Length") || !_stricmp(header->name, "Transfer-Encoding") ||
            !_stricmp(header->name, "Host") || !_stricmp(header->name, "Connection")) return 0;
    }
    return 1;
}

static int prepare(http_context *context) {
    const cdx_http_request *request = context->request;
    URL_COMPONENTS parts;
    size_t used = 0;
    if (!valid_request(request)) return 0;
    context->url = wide(request->url); context->method = wide(request->method);
    context->headers = (wchar_t *)calloc(HTTP_HEADER_BYTES + 1u, sizeof(wchar_t));
    if (!context->url || !context->method || !context->headers) return 0;
    memset(&parts, 0, sizeof(parts)); parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = parts.dwUserNameLength = parts.dwPasswordLength = (DWORD)-1;
    if (!WinHttpCrackUrl(context->url, 0, 0, &parts) || !parts.dwHostNameLength || parts.dwUserNameLength || parts.dwPasswordLength) return 0;
    context->port = parts.nPort; context->tls = parts.nScheme == INTERNET_SCHEME_HTTPS;
    context->host = slice(parts.lpszHostName, parts.dwHostNameLength);
    context->resource = (wchar_t *)calloc((size_t)parts.dwUrlPathLength + parts.dwExtraInfoLength + 2u, sizeof(wchar_t));
    if (!context->host || !context->resource) return 0;
    context->loopback_target = loopback_target(context->host);
    if (context->loopback_target && !context->automatic && strcmp(request->proxy, "direct")) {
        context->result->status = "not_supported";
        context->result->message = "WinHTTP bypasses proxies for loopback URL destinations; use direct for this target"; return 0;
    }
    if (parts.dwUrlPathLength) memcpy(context->resource, parts.lpszUrlPath, (size_t)parts.dwUrlPathLength * sizeof(wchar_t));
    else context->resource[0] = L'/';
    if (parts.dwExtraInfoLength) wcscat_s(context->resource, (size_t)parts.dwUrlPathLength + parts.dwExtraInfoLength + 2u, parts.lpszExtraInfo);
    if (*request->proxy && !automatic_proxy(request->proxy) && strcmp(request->proxy, "direct") && !context->defer_transport) {
        const char *proxy = request->proxy;
        if (!strncmp(proxy, "http://", 7u)) proxy += 7u;
        if (!*proxy || strpbrk(proxy, "/@?# ") || strchr(proxy, '=')) return 0;
        context->proxy = wide(proxy);
        if (!context->proxy) return 0;
    }
    for (size_t i = 0; i < request->header_count; ++i) {
        wchar_t *name = wide(request->headers[i].name), *value = wide(request->headers[i].value);
        size_t length;
        if (!name || !value) { free(name); free(value); return 0; }
        length = wcslen(name) + wcslen(value) + 4u;
        if (used + length > HTTP_HEADER_BYTES) { free(name); free(value); return 0; }
        (void)wcscpy_s(context->headers + used, HTTP_HEADER_BYTES + 1u - used, name); used += wcslen(name);
        context->headers[used++] = L':'; context->headers[used++] = L' ';
        (void)wcscpy_s(context->headers + used, HTTP_HEADER_BYTES + 1u - used, value); used += wcslen(value);
        context->headers[used++] = L'\r'; context->headers[used++] = L'\n'; context->headers[used] = 0;
        free(name); free(value);
    }
    context->result->status = "connection_error"; context->result->message = "HTTP transport did not complete";
    if (context->defer_transport) return 1;
    context->async = (http_async *)calloc(1, sizeof(http_async));
    if (!context->async) return 0;
    context->async->references = 1;
    context->async->ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    context->async->closed = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!context->async->ready || !context->async->closed) return 0;
    context->session = WinHttpOpen(L"Cordonix native HTTP", context->proxy ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_NO_PROXY,
        context->proxy ? context->proxy : WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (!context->session) return 0;
    {
        uint64_t now = GetTickCount64();
        uint64_t remaining = now < context->deadline ? context->deadline - now : 1u;
        uint32_t connect_budget = (uint32_t)(context->automatic ? remaining / 2u : remaining);
        if (!connect_budget) connect_budget = 1u;
        if (context->automatic && connect_budget > 3000u) connect_budget = 3000u;
        context->direct_deadline = now + connect_budget;
        if (!WinHttpSetTimeouts(context->session, (int)connect_budget, (int)connect_budget, (int)remaining, (int)remaining)) return 0;
    }
    context->connection = WinHttpConnect(context->session, context->host, parts.nPort, 0);
    if (!context->connection) return 0;
    context->handle = WinHttpOpenRequest(context->connection, context->method, context->resource, NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0u);
    return context->handle != NULL;
}

static void release_async(http_async *state) {
    if (InterlockedDecrement(&state->references)) return;
    if (state->ready) CloseHandle(state->ready);
    if (state->closed) CloseHandle(state->closed);
    SecureZeroMemory(state, sizeof(*state)); free(state);
}

static void CALLBACK notification(HINTERNET handle, DWORD_PTR context, DWORD status, LPVOID information, DWORD size) {
    http_async *state = (http_async *)context;
    (void)handle;
    if (!state) return;
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
        SetEvent(state->closed); release_async(state); return;
    }
    if (status == WINHTTP_CALLBACK_STATUS_CONNECTED_TO_SERVER) { InterlockedExchange(&state->connected, 1); return; }
    if (status == WINHTTP_CALLBACK_STATUS_SENDING_REQUEST || status == WINHTTP_CALLBACK_STATUS_REQUEST_SENT) {
        InterlockedExchange(&state->send_started, 1); return;
    }
    if (status == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE) { InterlockedExchange(&state->secure_failure, 1); return; }
    if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR && size == sizeof(WINHTTP_ASYNC_RESULT))
        state->error = ((WINHTTP_ASYNC_RESULT *)information)->dwError;
    else if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) state->bytes = size;
    else if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE && status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) return;
    InterlockedExchange(&state->notification, (LONG)status);
    SetEvent(state->ready);
}

static int fail_native(http_context *context, DWORD error) {
    cdx_http_result *result = context->result;
    result->native_error = error;
    result->status = error == ERROR_WINHTTP_TIMEOUT ? "timed_out" : error == ERROR_WINHTTP_SECURE_FAILURE ? "tls_error" : "connection_error";
    result->message = error == ERROR_WINHTTP_SECURE_FAILURE ? "TLS certificate verification or secure-channel negotiation failed" : "WinHTTP request failed";
    return 0;
}

static int stopped(http_context *context) {
    if (context->request->cancellation && WaitForSingleObject(context->request->cancellation, 0) == WAIT_OBJECT_0) {
        context->result->status = "cancelled"; context->result->message = "HTTP operation cancelled"; return 1;
    }
    if (GetTickCount64() >= context->deadline) {
        context->result->status = "timed_out"; context->result->message = "HTTP operation exceeded the shared deadline"; return 1;
    }
    if (context->automatic && context->async && !context->async->connected && !context->async->send_started &&
        context->direct_deadline && GetTickCount64() >= context->direct_deadline) {
        context->result->status = "timed_out"; context->result->native_error = ERROR_WINHTTP_TIMEOUT;
        context->result->message = "Initial direct connection budget expired before request send"; return 1;
    }
    return 0;
}

static int wait_notification(http_context *context, DWORD expected) {
    http_async *state = context->async;
    for (unsigned int poll = 0; poll < HTTP_POLLS && !stopped(context); ++poll) {
        DWORD wait = WaitForSingleObject(state->ready, 10u);
        if (wait == WAIT_OBJECT_0) {
            DWORD status = (DWORD)InterlockedCompareExchange(&state->notification, 0, 0);
            if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) return fail_native(context, state->error);
            if (status == expected) return 1;
        } else if (wait != WAIT_TIMEOUT) return fail_native(context, GetLastError());
    }
    if (!stopped(context)) {
        context->result->status = "timed_out"; context->result->message = "HTTP wait iteration budget reached";
    }
    return 0;
}

static int read_headers(http_context *context) {
    DWORD size = 0, index = 0, code_size = sizeof(DWORD);
    wchar_t *headers;
    int bytes;
    if (!WinHttpQueryHeaders(context->handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
        &context->result->status_code, &code_size, WINHTTP_NO_HEADER_INDEX)) return fail_native(context, GetLastError());
    if (WinHttpQueryHeaders(context->handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &size, &index) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER || size > HTTP_RESPONSE_HEADERS || !size) {
        context->result->status = "output_limit"; context->result->message = "Response headers exceed their 64 KiB bound"; return 0;
    }
    headers = (wchar_t *)calloc((size_t)size + sizeof(wchar_t), 1);
    if (!headers) return 0;
    if (!WinHttpQueryHeaders(context->handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, headers, &size, &index)) {
        free(headers); return fail_native(context, GetLastError());
    }
    bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, headers, -1, NULL, 0, NULL, NULL);
    context->result->headers = bytes > 0 ? (char *)calloc((size_t)bytes, 1) : NULL;
    if (context->result->headers) {
        (void)WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, headers, -1, context->result->headers, bytes, NULL, NULL);
        context->result->headers_size = (size_t)bytes - 1u;
    }
    free(headers);
    return context->result->headers != NULL;
}

static int exchange(http_context *context) {
    http_async *state = context->async;
    DWORD_PTR pointer = (DWORD_PTR)state;
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    BOOL ok;
    if (stopped(context)) return 0;
    if (!WinHttpSetOption(context->handle, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
        !WinHttpSetOption(context->handle, WINHTTP_OPTION_CONTEXT_VALUE, &pointer, sizeof(pointer))) return fail_native(context, GetLastError());
    InterlockedIncrement(&state->references);
    if (WinHttpSetStatusCallback(context->handle, notification,
        WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0) == WINHTTP_INVALID_STATUS_CALLBACK) {
        release_async(state); return fail_native(context, GetLastError());
    }
    context->result->stage = "send"; context->result->request_started = 1;
    if (context->request->body_size) memcpy(state->body_buffer, context->request->body, context->request->body_size);
    ok = WinHttpSendRequest(context->handle, *context->headers ? context->headers : WINHTTP_NO_ADDITIONAL_HEADERS, *context->headers ? (DWORD)-1 : 0u,
        context->request->body_size ? state->body_buffer : WINHTTP_NO_REQUEST_DATA,
        (DWORD)context->request->body_size, (DWORD)context->request->body_size, pointer);
    if (!ok && GetLastError() != ERROR_IO_PENDING) return fail_native(context, GetLastError());
    if (!wait_notification(context, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE)) return 0;
    context->result->stage = "headers";
    if (!WinHttpReceiveResponse(context->handle, NULL) && GetLastError() != ERROR_IO_PENDING) return fail_native(context, GetLastError());
    if (!wait_notification(context, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) || !read_headers(context)) return 0;
    context->result->body = (unsigned char *)calloc(context->request->response_limit_bytes + 1u, 1);
    if (!context->result->body) return 0;
    context->result->stage = "body";
    for (unsigned int block = 0; block < CDX_HTTP_MAX_RESPONSE + 1u && !stopped(context); ++block) {
        DWORD got;
        size_t remaining = context->request->response_limit_bytes - context->result->body_size;
        state->bytes = 0;
        if (!WinHttpReadData(context->handle, state->read_buffer, sizeof(state->read_buffer), NULL) && GetLastError() != ERROR_IO_PENDING)
            return fail_native(context, GetLastError());
        if (!wait_notification(context, WINHTTP_CALLBACK_STATUS_READ_COMPLETE)) return 0;
        got = state->bytes;
        if (!got) {
            context->result->status = "completed"; context->result->message = "HTTP response completed";
            context->result->response_complete = 1; return 1;
        }
        if ((size_t)got > remaining) {
            memcpy(context->result->body + context->result->body_size, state->read_buffer, remaining);
            context->result->body_size += remaining;
            context->result->status = "output_limit"; context->result->message = "HTTP response body exceeded its requested limit"; return 0;
        }
        memcpy(context->result->body + context->result->body_size, state->read_buffer, got); context->result->body_size += got;
    }
    return 0;
}

static void cleanup(http_context *context) {
    http_async *state = context->async;
    int callback_set = state && InterlockedCompareExchange(&state->references, 0, 0) > 1;
    if (context->handle && !WinHttpCloseHandle(context->handle)) context->result->cleanup_complete = 0;
    if (context->connection && !WinHttpCloseHandle(context->connection)) context->result->cleanup_complete = 0;
    if (context->session && !WinHttpCloseHandle(context->session)) context->result->cleanup_complete = 0;
    if (callback_set && WaitForSingleObject(state->closed, 2000u) != WAIT_OBJECT_0) context->result->cleanup_complete = 0;
    if (state) {
        context->connected_snapshot = InterlockedCompareExchange(&state->connected, 0, 0) != 0;
        context->secure_failure_snapshot = InterlockedCompareExchange(&state->secure_failure, 0, 0) != 0;
    }
    if (state && InterlockedCompareExchange(&state->send_started, 0, 0)) context->result->request_send_started = 1;
    if (state) release_async(state);
    free(context->url); free(context->method); free(context->host); free(context->resource); free(context->proxy); free(context->headers);
}

static int proxy_attempt(const cdx_http_request *request, cdx_http_result *result, const char *host,
    unsigned int port, int tls, uint64_t deadline, int skip_direct) {
    WSADATA winsock;
    cdx_proxy_request connection;
    cdx_http_proxy_bridge *bridge = NULL;
    cdx_http_request fallback = *request;
    http_context context;
    uint32_t direct_error = result->proxy.direct_error;
    char proxy[64];
    int success = 0;
    memset(&connection, 0, sizeof(connection)); memset(&context, 0, sizeof(context));
    if (WSAStartup(MAKEWORD(2, 2), &winsock)) return 0;
    connection.host = host; connection.port = port; connection.proxy = skip_direct ? "auto" : request->proxy;
    connection.deadline_ms = deadline; connection.cancellation = request->cancellation; connection.skip_direct = skip_direct;
    connection.candidate_ports = request->proxy_ports; connection.candidate_count = request->proxy_port_count;
    cdx_proxy_connect(&connection, &result->proxy);
    if (skip_direct) { result->proxy.direct_attempted = 1; result->proxy.direct_error = direct_error; }
    result->native_error = result->proxy.native_error; result->status = result->proxy.status;
    result->stage = "proxy"; result->message = "Local proxy tunnel did not complete";
    if (!result->proxy.cleanup_complete) result->cleanup_complete = 0;
    if (result->proxy.socket == UINTPTR_MAX) goto done;
    if (!cdx_http_proxy_bridge_start(result->proxy.socket, host, port, tls, deadline, request->cancellation, &bridge, proxy)) {
        cdx_proxy_close(result->proxy.socket); result->proxy.socket = UINTPTR_MAX;
        result->status = "connection_error"; result->message = "Cannot create the bounded per-call HTTP proxy bridge"; goto done;
    }
    result->proxy.socket = UINTPTR_MAX; result->proxy_bridge_used = 1;
    result->status = "validation_error";
    fallback.proxy = proxy;
    context.request = &fallback; context.result = result; context.deadline = deadline;
    if (prepare(&context)) success = exchange(&context);
    else if (!strcmp(result->status, "connection_error")) result->native_error = GetLastError();
    cleanup(&context);
    if (!cdx_http_proxy_bridge_stop(bridge, &result->proxy_bridge_error)) result->cleanup_complete = 0;
done:
    WSACleanup(); return success;
}
