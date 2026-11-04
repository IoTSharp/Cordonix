#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "cdx/http_proxy.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define BRIDGE_HEADER 32768u
#define BRIDGE_BYTES (4u * 1024u * 1024u)
#define BRIDGE_POLLS 30002u

struct cdx_http_proxy_bridge {
    SOCKET listener, upstream, client;
    HANDLE thread, stop;
    CRITICAL_SECTION lock;
    volatile LONG references;
    uint64_t deadline;
    cdx_cancel_handle cancellation;
    char authority[1200], default_authority[1200];
    int tls;
    DWORD error;
};

static int stopped(cdx_http_proxy_bridge *bridge) {
    return GetTickCount64() >= bridge->deadline || WaitForSingleObject(bridge->stop, 0) == WAIT_OBJECT_0 ||
        (bridge->cancellation && WaitForSingleObject(bridge->cancellation, 0) == WAIT_OBJECT_0);
}

static void release(cdx_http_proxy_bridge *bridge) {
    if (InterlockedDecrement(&bridge->references)) return;
    DeleteCriticalSection(&bridge->lock); CloseHandle(bridge->stop); free(bridge);
}

static int send_all(cdx_http_proxy_bridge *bridge, SOCKET socket, const char *bytes, size_t length) {
    size_t sent = 0;
    for (unsigned int poll = 0; poll < BRIDGE_POLLS && sent < length && !stopped(bridge); ++poll) {
        int rc = send(socket, bytes + sent, (int)(length - sent), 0);
        if (rc > 0) sent += (size_t)rc;
        else if (rc == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) Sleep(10);
        else { bridge->error = (DWORD)WSAGetLastError(); return 0; }
    }
    return sent == length;
}

static int authority_matches(cdx_http_proxy_bridge *bridge, const char *authority, size_t size) {
    return (strlen(bridge->authority) == size && !_strnicmp(authority, bridge->authority, size)) ||
        (strlen(bridge->default_authority) == size && !_strnicmp(authority, bridge->default_authority, size));
}

static int initial_request(cdx_http_proxy_bridge *bridge) {
    char *input = (char *)calloc(BRIDGE_HEADER + 1u, 1), *rewritten = NULL;
    size_t size = 0;
    int success = 0;
    if (!input) return 0;
    for (unsigned int poll = 0; poll < BRIDGE_POLLS && size < BRIDGE_HEADER && !stopped(bridge); ++poll) {
        int got = recv(bridge->client, input + size, (int)(BRIDGE_HEADER - size), 0);
        char *line_end, *method_end, *target_end, *headers_end;
        if (got == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) { Sleep(10); continue; }
        if (got <= 0) break;
        size += (size_t)got; input[size] = 0;
        headers_end = strstr(input, "\r\n\r\n");
        if (!headers_end) continue;
        line_end = strstr(input, "\r\n"); method_end = strchr(input, ' ');
        target_end = method_end ? strchr(method_end + 1, ' ') : NULL;
        if (!line_end || !method_end || !target_end || target_end >= line_end) break;
        if (bridge->tls) {
            static const char established[] = "HTTP/1.1 200 Connection Established\r\n\r\n";
            if ((size_t)(method_end - input) != 7u || memcmp(input, "CONNECT", 7u) ||
                !authority_matches(bridge, method_end + 1, (size_t)(target_end - method_end - 1))) break;
            if (!send_all(bridge, bridge->client, established, sizeof(established) - 1u)) break;
            if (headers_end + 4 < input + size && !send_all(bridge, bridge->upstream, headers_end + 4,
                (size_t)(input + size - headers_end - 4))) break;
            success = 1; break;
        } else {
            char *authority = method_end + 1, *path;
            size_t prefix, tail, slash;
            if ((size_t)(target_end - authority) < 7u || _strnicmp(authority, "http://", 7u)) break;
            authority += 7;
            path = authority;
            for (; path < target_end && *path != '/' && *path != '?'; ++path) { }
            if (!authority_matches(bridge, authority, (size_t)(path - authority))) break;
            prefix = (size_t)(method_end + 1 - input); tail = (size_t)(input + size - path);
            slash = path == target_end || *path == '?' ? 1u : 0u;
            rewritten = (char *)malloc(prefix + tail + slash);
            if (!rewritten) break;
            memcpy(rewritten, input, prefix);
            if (slash) rewritten[prefix] = '/';
            memcpy(rewritten + prefix + slash, path, tail);
            success = send_all(bridge, bridge->upstream, rewritten, prefix + slash + tail); break;
        }
    }
    if (!success && !bridge->error) bridge->error = ERROR_INVALID_DATA;
    free(rewritten); free(input); return success;
}

static DWORD WINAPI run_bridge(void *argument) {
    cdx_http_proxy_bridge *bridge = (cdx_http_proxy_bridge *)argument;
    u_long nonblocking = 1;
    size_t total = 0;
    for (unsigned int poll = 0; poll < BRIDGE_POLLS && !stopped(bridge); ++poll) {
        SOCKET client = accept(bridge->listener, NULL, NULL);
        if (client == INVALID_SOCKET && WSAGetLastError() == WSAEWOULDBLOCK) { Sleep(10); continue; }
        if (client == INVALID_SOCKET) { bridge->error = (DWORD)WSAGetLastError(); goto done; }
        EnterCriticalSection(&bridge->lock); bridge->client = client; LeaveCriticalSection(&bridge->lock);
        if (ioctlsocket(client, FIONBIO, &nonblocking)) { bridge->error = (DWORD)WSAGetLastError(); goto done; }
        break;
    }
    if (bridge->client == INVALID_SOCKET || !initial_request(bridge)) goto done;
    for (unsigned int poll = 0; poll < BRIDGE_POLLS && total < BRIDGE_BYTES && !stopped(bridge); ++poll) {
        fd_set reads;
        struct timeval delay = {0, 10000};
        char bytes[8192];
        int ready;
        FD_ZERO(&reads); FD_SET(bridge->client, &reads); FD_SET(bridge->upstream, &reads);
        ready = select(0, &reads, NULL, NULL, &delay);
        if (ready == SOCKET_ERROR) { bridge->error = (DWORD)WSAGetLastError(); break; }
        if (!ready) continue;
        for (unsigned int side = 0; side < 2u; ++side) {
            SOCKET source = side ? bridge->upstream : bridge->client, target = side ? bridge->client : bridge->upstream;
            int got;
            if (!FD_ISSET(source, &reads)) continue;
            got = recv(source, bytes, sizeof(bytes), 0);
            if (!got) goto done;
            if (got == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
            if (got < 0) { bridge->error = (DWORD)WSAGetLastError(); goto done; }
            total += (size_t)got;
            if (total > BRIDGE_BYTES || !send_all(bridge, target, bytes, (size_t)got)) goto done;
        }
    }
    if (total >= BRIDGE_BYTES && !bridge->error) bridge->error = ERROR_BUFFER_OVERFLOW;
done:
    EnterCriticalSection(&bridge->lock);
    if (bridge->client != INVALID_SOCKET) { closesocket(bridge->client); bridge->client = INVALID_SOCKET; }
    if (bridge->listener != INVALID_SOCKET) { closesocket(bridge->listener); bridge->listener = INVALID_SOCKET; }
    if (bridge->upstream != INVALID_SOCKET) { closesocket(bridge->upstream); bridge->upstream = INVALID_SOCKET; }
    LeaveCriticalSection(&bridge->lock);
    WSACleanup();
    release(bridge); return 0;
}

int cdx_http_proxy_bridge_start(uintptr_t socket, const char *host, unsigned int port, int tls,
    uint64_t deadline_ms, cdx_cancel_handle cancellation, cdx_http_proxy_bridge **output, char proxy[64]) {
    cdx_http_proxy_bridge *bridge = (cdx_http_proxy_bridge *)calloc(1, sizeof(*bridge));
    struct sockaddr_in address;
    int size = sizeof(address), exclusive = 1;
    WSADATA winsock;
    u_long nonblocking = 1;
    *output = NULL;
    if (!bridge || !host || strlen(host) > 1024u || !port || port > 65535u) { free(bridge); return 0; }
    if (WSAStartup(MAKEWORD(2, 2), &winsock)) { free(bridge); return 0; }
    bridge->listener = bridge->client = INVALID_SOCKET; bridge->upstream = (SOCKET)socket;
    bridge->deadline = deadline_ms; bridge->cancellation = cancellation; bridge->tls = tls; bridge->references = 1;
    InitializeCriticalSection(&bridge->lock); bridge->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!bridge->stop) { DeleteCriticalSection(&bridge->lock); free(bridge); WSACleanup(); return 0; }
    (void)snprintf(bridge->authority, sizeof(bridge->authority), strchr(host, ':') ? "[%s]:%u" : "%s:%u", host, port);
    if (port == (tls ? 443u : 80u)) (void)snprintf(bridge->default_authority, sizeof(bridge->default_authority), strchr(host, ':') ? "[%s]" : "%s", host);
    else (void)strcpy_s(bridge->default_authority, sizeof(bridge->default_authority), bridge->authority);
    memset(&address, 0, sizeof(address)); address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bridge->listener = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    if (bridge->listener == INVALID_SOCKET ||
        setsockopt(bridge->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof(exclusive)) ||
        bind(bridge->listener, (const struct sockaddr *)&address, sizeof(address)) || listen(bridge->listener, 1) ||
        getsockname(bridge->listener, (struct sockaddr *)&address, &size) || ioctlsocket(bridge->listener, FIONBIO, &nonblocking)) goto failed;
    (void)snprintf(proxy, 64u, "http://127.0.0.1:%u", (unsigned int)ntohs(address.sin_port));
    InterlockedIncrement(&bridge->references);
    bridge->thread = CreateThread(NULL, 0, run_bridge, bridge, 0, NULL);
    if (!bridge->thread) { release(bridge); goto failed; }
    *output = bridge; return 1;
failed:
    if (bridge->listener != INVALID_SOCKET) closesocket(bridge->listener);
    release(bridge); WSACleanup(); return 0;
}

int cdx_http_proxy_bridge_stop(cdx_http_proxy_bridge *bridge, uint32_t *error) {
    int complete;
    if (!bridge) return 1;
    SetEvent(bridge->stop);
    EnterCriticalSection(&bridge->lock);
    if (bridge->client != INVALID_SOCKET) (void)shutdown(bridge->client, SD_BOTH);
    if (bridge->upstream != INVALID_SOCKET) (void)shutdown(bridge->upstream, SD_BOTH);
    LeaveCriticalSection(&bridge->lock);
    complete = WaitForSingleObject(bridge->thread, 2000u) == WAIT_OBJECT_0;
    if (error) *error = complete ? bridge->error : ERROR_IO_PENDING;
    CloseHandle(bridge->thread); release(bridge); return complete;
}
