#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "cdx/proxy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct proxy_context {
    const cdx_proxy_request *request;
    cdx_proxy_result *result;
    uint64_t deadline;
    unsigned int steps;
} proxy_context;
typedef enum tunnel_result { TUNNEL_READY, TUNNEL_UNRECOGNIZED, TUNNEL_REFUSED, TUNNEL_STOPPED } tunnel_result;

static int stopped(proxy_context *context);
static int validate(const cdx_proxy_request *, int *, unsigned short *);
static SOCKET connect_ip(proxy_context *, const char *, unsigned short);
static int ready(proxy_context *, SOCKET, int);
static int send_all(proxy_context *, SOCKET, const unsigned char *, size_t);
static int receive_all(proxy_context *, SOCKET, unsigned char *, size_t);
static tunnel_result http_connect(proxy_context *, SOCKET);
static tunnel_result socks_connect(proxy_context *, SOCKET);
static uint64_t part_deadline(const cdx_proxy_request *, unsigned int, unsigned int);
static void close_socket(proxy_context *, SOCKET);

void cdx_proxy_connect(const cdx_proxy_request *request, cdx_proxy_result *result) {
    proxy_context context;
    unsigned short explicit_port = 0, defaults[2] = {7890u, 1080u};
    const unsigned short *ports;
    size_t count;
    int mode = 0;
    SOCKET socket_value = INVALID_SOCKET;
    if (!result) return;
    memset(result, 0, sizeof(*result)); memset(&context, 0, sizeof(context));
    result->socket = UINTPTR_MAX; result->status = "validation_error"; result->cleanup_complete = 1;
    if (!validate(request, &mode, &explicit_port)) return;
    context.request = request; context.result = result; context.deadline = request->deadline_ms;
    if (stopped(&context)) return;
    if (!mode || mode == 1) {
        if (!request->skip_direct) {
            result->direct_attempted = 1;
            context.deadline = mode == 1 ? request->deadline_ms : part_deadline(request, 1000u, 4u);
            socket_value = connect_ip(&context, request->host, (unsigned short)request->port);
            if (socket_value != INVALID_SOCKET) {
                result->socket = (uintptr_t)socket_value; result->status = "completed";
                (void)strcpy_s(result->selected_proxy, sizeof(result->selected_proxy), "direct"); return;
            }
            result->direct_error = result->native_error;
            if (!strcmp(result->status, "cancelled")) return;
            context.deadline = request->deadline_ms;
            if (stopped(&context)) return;
        }
        if (mode == 1) { result->status = "connection_error"; return; }
    }
    if (mode >= 2) { ports = &explicit_port; count = 1u; }
    else { ports = request->candidate_count ? request->candidate_ports : defaults; count = request->candidate_count ? request->candidate_count : 2u; }
    result->status = "proxy_unavailable";
    for (size_t i = 0; i < count && i < 2u; ++i) {
        context.deadline = part_deadline(request, 200u, (unsigned int)(count - i));
        if (stopped(&context)) {
            if (!strcmp(result->status, "cancelled")) return;
            context.deadline = request->deadline_ms;
            if (stopped(&context)) return;
        }
        result->probed_ports[result->probe_count] = ports[i];
        socket_value = connect_ip(&context, "127.0.0.1", ports[i]);
        result->probe_open[result->probe_count++] = socket_value != INVALID_SOCKET;
        if (socket_value != INVALID_SOCKET) close_socket(&context, socket_value);
        if (!strcmp(result->status, "cancelled")) return;
    }
    for (size_t i = 0; i < count && i < 2u; ++i) {
        int first = mode >= 2 ? mode : i == 0 ? 2 : 3;
        if (!result->probe_open[i]) continue;
        for (unsigned int variant = 0; variant < (mode >= 2 ? 1u : 2u) && result->tunnel_attempts < 5u; ++variant) {
            int protocol = variant ? (first == 2 ? 3 : 2) : first;
            tunnel_result tunnel;
            context.deadline = part_deadline(request, 3000u, mode >= 2 ? 1u : 4u - result->tunnel_attempts);
            if (stopped(&context)) {
                if (!strcmp(result->status, "cancelled")) return;
                context.deadline = request->deadline_ms;
                if (stopped(&context)) return;
                continue;
            }
            ++result->tunnel_attempts;
            socket_value = connect_ip(&context, "127.0.0.1", ports[i]);
            if (socket_value == INVALID_SOCKET) {
                if (!strcmp(result->status, "cancelled")) return;
                continue;
            }
            tunnel = protocol == 2 ? http_connect(&context, socket_value) : socks_connect(&context, socket_value);
            if (tunnel == TUNNEL_READY) {
                result->socket = (uintptr_t)socket_value; result->status = "completed"; result->native_error = 0;
                (void)snprintf(result->selected_proxy, sizeof(result->selected_proxy), "%s://127.0.0.1:%u", protocol == 2 ? "http" : "socks5", (unsigned int)ports[i]);
                return;
            }
            close_socket(&context, socket_value);
            if (!strcmp(result->status, "cancelled")) return;
            context.deadline = request->deadline_ms;
            if (stopped(&context)) return;
            if (tunnel == TUNNEL_REFUSED) { result->status = "proxy_error"; break; }
            result->status = "proxy_unavailable";
        }
    }
    context.deadline = request->deadline_ms;
    if (!stopped(&context) && strcmp(result->status, "proxy_error")) result->status = "proxy_unavailable";
}

void cdx_proxy_close(uintptr_t socket_value) {
    if (socket_value != UINTPTR_MAX) (void)closesocket((SOCKET)socket_value);
}

static int literal_ip(const char *host) {
    struct in6_addr address;
    return InetPtonA(AF_INET, host, &address) == 1 || InetPtonA(AF_INET6, host, &address) == 1;
}

static int validate(const cdx_proxy_request *request, int *mode, unsigned short *port) {
    const char *proxy, *number;
    size_t length;
    unsigned int value = 0;
    if (!request || !request->host || !request->proxy || !request->port || request->port > 65535u ||
        !request->deadline_ms || request->candidate_count > 2u || (request->candidate_count && !request->candidate_ports)) return 0;
    length = strlen(request->host);
    if (!length || length > 253u) return 0;
    for (size_t i = 0; i < length; ++i)
        if (!((request->host[i] >= 'a' && request->host[i] <= 'z') || (request->host[i] >= 'A' && request->host[i] <= 'Z') ||
              (request->host[i] >= '0' && request->host[i] <= '9') || strchr(".-:", request->host[i]))) return 0;
    for (size_t i = 0; i < request->candidate_count; ++i) if (!request->candidate_ports[i]) return 0;
    proxy = request->proxy;
    if (!*proxy || !strcmp(proxy, "auto")) *mode = 0;
    else if (!strcmp(proxy, "direct")) *mode = 1;
    else if (!strncmp(proxy, "http://127.0.0.1:", 17u)) { *mode = 2; number = proxy + 17u; goto parse_port; }
    else if (!strncmp(proxy, "socks5://127.0.0.1:", 19u)) { *mode = 3; number = proxy + 19u; goto parse_port; }
    else return 0;
    return request->skip_direct || literal_ip(request->host);
parse_port:
    length = strlen(number);
    if (!length || length > 5u) return 0;
    for (size_t i = 0; i < length; ++i) {
        if (number[i] < '0' || number[i] > '9') return 0;
        value = value * 10u + (unsigned int)(number[i] - '0');
    }
    if (!value || value > 65535u) return 0;
    *port = (unsigned short)value;
    return 1;
}

static uint64_t part_deadline(const cdx_proxy_request *request, unsigned int maximum_ms, unsigned int portions) {
    uint64_t now = GetTickCount64(), remaining = request->deadline_ms > now ? request->deadline_ms - now : 0;
    uint64_t slice = remaining / (portions ? portions : 1u);
    if (slice > maximum_ms) slice = maximum_ms;
    return now + slice;
}

static int stopped(proxy_context *context) {
    if (context->request->cancellation && WaitForSingleObject(context->request->cancellation, 0) == WAIT_OBJECT_0) {
        context->result->status = "cancelled"; context->result->native_error = ERROR_CANCELLED; return 1;
    }
    if (++context->steps > 60000u || GetTickCount64() >= context->deadline) {
        context->result->status = "timed_out"; context->result->native_error = WSAETIMEDOUT; return 1;
    }
    return 0;
}

static int ready(proxy_context *context, SOCKET socket_value, int write) {
    fd_set set, errors;
    struct timeval timeout = {0, 10000};
    int rc;
    if (stopped(context)) return -1;
    FD_ZERO(&set); FD_ZERO(&errors); FD_SET(socket_value, &set); FD_SET(socket_value, &errors);
    rc = select(0, write ? NULL : &set, write ? &set : NULL, &errors, &timeout);
    if (rc == SOCKET_ERROR) { context->result->native_error = (uint32_t)WSAGetLastError(); return -1; }
    if (FD_ISSET(socket_value, &errors)) {
        int error = 0, length = sizeof(error);
        if (getsockopt(socket_value, SOL_SOCKET, SO_ERROR, (char *)&error, &length)) error = WSAGetLastError();
        context->result->native_error = (uint32_t)error; return -1;
    }
    return rc > 0;
}

static void close_socket(proxy_context *context, SOCKET socket_value) {
    if (socket_value != INVALID_SOCKET && closesocket(socket_value)) context->result->cleanup_complete = 0;
}

static SOCKET connect_ip(proxy_context *context, const char *host, unsigned short port) {
    SOCKADDR_STORAGE address;
    SOCKET socket_value;
    u_long nonblocking = 1;
    int size, rc;
    memset(&address, 0, sizeof(address));
    if (InetPtonA(AF_INET, host, &((struct sockaddr_in *)&address)->sin_addr) == 1) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)&address;
        ipv4->sin_family = AF_INET; ipv4->sin_port = htons(port); size = sizeof(*ipv4);
    } else {
        struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&address;
        ipv6->sin6_family = AF_INET6; ipv6->sin6_port = htons(port); size = sizeof(*ipv6);
        (void)InetPtonA(AF_INET6, host, &ipv6->sin6_addr);
    }
    socket_value = socket(address.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (socket_value == INVALID_SOCKET) { context->result->native_error = (uint32_t)WSAGetLastError(); return INVALID_SOCKET; }
    if (ioctlsocket(socket_value, FIONBIO, &nonblocking)) goto failed;
    rc = connect(socket_value, (const struct sockaddr *)&address, size);
    if (!rc) return socket_value;
    if (WSAGetLastError() != WSAEWOULDBLOCK) goto failed;
    for (unsigned int i = 0; i < 6000u; ++i) {
        int error = 0, error_size = sizeof(error);
        rc = ready(context, socket_value, 1);
        if (rc < 0) { close_socket(context, socket_value); return INVALID_SOCKET; }
        if (!rc) continue;
        if (getsockopt(socket_value, SOL_SOCKET, SO_ERROR, (char *)&error, &error_size)) goto failed;
        if (error) { context->result->native_error = (uint32_t)error; close_socket(context, socket_value); return INVALID_SOCKET; }
        return socket_value;
    }
    context->result->native_error = WSAETIMEDOUT;
    close_socket(context, socket_value); return INVALID_SOCKET;
failed:
    context->result->native_error = (uint32_t)WSAGetLastError();
    close_socket(context, socket_value); return INVALID_SOCKET;
}

static int send_all(proxy_context *context, SOCKET socket_value, const unsigned char *bytes, size_t size) {
    size_t sent = 0;
    for (unsigned int i = 0; i < 6000u && sent < size; ++i) {
        int can_write = ready(context, socket_value, 1), wrote;
        if (can_write < 0) return 0;
        if (!can_write) continue;
        wrote = send(socket_value, (const char *)bytes + sent, (int)(size - sent), 0);
        if (wrote == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
        if (wrote <= 0) { context->result->native_error = (uint32_t)(wrote ? WSAGetLastError() : WSAECONNRESET); return 0; }
        sent += (size_t)wrote;
    }
    return sent == size;
}

static int receive_all(proxy_context *context, SOCKET socket_value, unsigned char *bytes, size_t size) {
    size_t received = 0;
    for (unsigned int i = 0; i < 6000u && received < size; ++i) {
        int can_read = ready(context, socket_value, 0), got;
        if (can_read < 0) return 0;
        if (!can_read) continue;
        got = recv(socket_value, (char *)bytes + received, (int)(size - received), 0);
        if (got == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
        if (got <= 0) { context->result->native_error = (uint32_t)(got ? WSAGetLastError() : WSAECONNRESET); return 0; }
        received += (size_t)got;
    }
    return received == size;
}

static tunnel_result http_connect(proxy_context *context, SOCKET socket_value) {
    char authority[288], request[640], response[8193];
    size_t size = 0;
    int status;
    if (strchr(context->request->host, ':')) (void)snprintf(authority, sizeof(authority), "[%s]:%u", context->request->host, context->request->port);
    else (void)snprintf(authority, sizeof(authority), "%s:%u", context->request->host, context->request->port);
    (void)snprintf(request, sizeof(request), "CONNECT %s HTTP/1.1\r\nHost: %s\r\n\r\n", authority, authority);
    if (!send_all(context, socket_value, (const unsigned char *)request, strlen(request))) return TUNNEL_STOPPED;
    for (unsigned int i = 0; i < 8192u && size < sizeof(response) - 1u; ++i) {
        if (!receive_all(context, socket_value, (unsigned char *)response + size, 1u)) return TUNNEL_UNRECOGNIZED;
        ++size; response[size] = 0;
        if (size >= 4u && !memcmp(response + size - 4u, "\r\n\r\n", 4u)) break;
    }
    if (!strstr(response, "\r\n\r\n") || (strncmp(response, "HTTP/1.0 ", 9u) && strncmp(response, "HTTP/1.1 ", 9u))) return TUNNEL_UNRECOGNIZED;
    if (size < 13u || response[12] != ' ' || response[9] < '0' || response[9] > '9' || response[10] < '0' || response[10] > '9' || response[11] < '0' || response[11] > '9') return TUNNEL_UNRECOGNIZED;
    status = (response[9] - '0') * 100 + (response[10] - '0') * 10 + response[11] - '0';
    if (status < 200 || status > 299) { context->result->native_error = (uint32_t)status; return TUNNEL_REFUSED; }
    return TUNNEL_READY;
}

static tunnel_result socks_connect(proxy_context *context, SOCKET socket_value) {
    unsigned char request[264] = {5u, 1u, 0u}, response[4];
    size_t size = 0, tail;
    struct in6_addr address;
    if (!send_all(context, socket_value, request, 3u) || !receive_all(context, socket_value, response, 2u)) return TUNNEL_UNRECOGNIZED;
    if (response[0] != 5u) return TUNNEL_UNRECOGNIZED;
    if (response[1] != 0u) { context->result->native_error = response[1]; return TUNNEL_REFUSED; }
    request[size++] = 5u; request[size++] = 1u; request[size++] = 0u;
    if (InetPtonA(AF_INET, context->request->host, &address) == 1) {
        request[size++] = 1u; memcpy(request + size, &address, 4u); size += 4u;
    } else if (InetPtonA(AF_INET6, context->request->host, &address) == 1) {
        request[size++] = 4u; memcpy(request + size, &address, 16u); size += 16u;
    } else {
        size_t length = strlen(context->request->host);
        request[size++] = 3u; request[size++] = (unsigned char)length;
        memcpy(request + size, context->request->host, length); size += length;
    }
    request[size++] = (unsigned char)(context->request->port >> 8); request[size++] = (unsigned char)context->request->port;
    if (!send_all(context, socket_value, request, size) || !receive_all(context, socket_value, response, 4u)) return TUNNEL_UNRECOGNIZED;
    if (response[0] != 5u || response[2] != 0u) return TUNNEL_UNRECOGNIZED;
    if (response[1]) { context->result->native_error = response[1]; return TUNNEL_REFUSED; }
    if (response[3] == 1u) tail = 6u;
    else if (response[3] == 4u) tail = 18u;
    else if (response[3] == 3u) {
        if (!receive_all(context, socket_value, request, 1u)) return TUNNEL_UNRECOGNIZED;
        tail = (size_t)request[0] + 2u;
    } else return TUNNEL_UNRECOGNIZED;
    if (!receive_all(context, socket_value, request, tail)) return TUNNEL_UNRECOGNIZED;
    return TUNNEL_READY;
}
