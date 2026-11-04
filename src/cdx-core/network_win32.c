#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "cdx/network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct network_context {
    const cdx_network_request *request;
    cdx_network_result *result;
    SOCKET socket;
    uint64_t deadline;
    unsigned int steps;
} network_context;
static int validate(const cdx_network_request *);
static int stopped(network_context *);
static int wait_socket(network_context *, int);
static int connect_socket(network_context *);
static int send_bytes(network_context *, const unsigned char *, size_t);
static int exchange(network_context *);
static int mqtt(network_context *);

void cdx_network_execute(const cdx_network_request *request, cdx_network_result *result) {
    network_context context;
    WSADATA winsock;
    uint64_t start = GetTickCount64();
    int initialized = 0;
    if (!result) return;
    memset(result, 0, sizeof(*result)); memset(&context, 0, sizeof(context));
    result->status = "validation_error"; result->stage = "validation";
    result->proxy.socket = UINTPTR_MAX; result->proxy.status = "not_run"; result->proxy.cleanup_complete = 1;
    result->mqtt_connack = -1;
    result->completion = "invalid request"; result->cleanup_complete = 1;
    if (!validate(request)) return;
    if (request->tls) { result->status = "not_supported"; result->completion = "TLS is not implemented for these protocols"; return; }
    context.request = request; context.result = result; context.socket = INVALID_SOCKET;
    context.deadline = start + request->timeout_ms;
    result->status = "connection_error"; result->stage = "connect"; result->completion = "connection failed";
    if (stopped(&context) || WSAStartup(MAKEWORD(2, 2), &winsock)) goto done;
    initialized = 1;
    if (!connect_socket(&context)) goto done;
    result->connected = 1;
    if (!strcmp(request->protocol, "mqtt")) (void)mqtt(&context);
    else if (!strcmp(request->action, "connect")) { result->status = "completed"; result->completion = "connected"; }
    else (void)exchange(&context);
done:
    if (context.socket != INVALID_SOCKET && closesocket(context.socket)) result->cleanup_complete = 0;
    if (initialized) WSACleanup();
    result->elapsed_ms = GetTickCount64() - start;
}

void cdx_network_result_free(cdx_network_result *result) {
    if (!result) return;
    free(result->data); memset(result, 0, sizeof(*result));
}

static int validate(const cdx_network_request *request) {
    struct in6_addr address;
    int is_mqtt;
    if (!request || !request->protocol || !request->action || !request->host || !request->client_id || !request->topic ||
        (strcmp(request->protocol, "tcp") && strcmp(request->protocol, "telnet") && strcmp(request->protocol, "mqtt")) ||
        (InetPtonA(AF_INET, request->host, &address) != 1 && InetPtonA(AF_INET6, request->host, &address) != 1) ||
        !request->port || request->port > 65535u || !request->timeout_ms || request->timeout_ms > 60000u ||
        !request->idle_timeout_ms || request->idle_timeout_ms > request->timeout_ms ||
        !request->read_limit_bytes || request->read_limit_bytes > CDX_NETWORK_MAX_BYTES ||
        request->payload_size > CDX_NETWORK_MAX_BYTES || (request->payload_size && !request->payload)) return 0;
    is_mqtt = !strcmp(request->protocol, "mqtt");
    if (is_mqtt) {
        size_t length = strlen(request->topic), id_length = strlen(request->client_id);
        if (strcmp(request->action, "connect") && strcmp(request->action, "publish")) return 0;
        if (id_length > 64u || (!strcmp(request->action, "publish") && (!length || length > 1024u || strpbrk(request->topic, "+#")))) return 0;
        if (!strcmp(request->action, "publish") && !request->allow_send) return 0;
    } else {
        if (strcmp(request->action, "connect") && strcmp(request->action, "exchange")) return 0;
        if (request->payload_size && (!request->allow_send || !strcmp(request->action, "connect"))) return 0;
    }
    return 1;
}

static int stopped(network_context *context) {
    if (context->request->cancellation && WaitForSingleObject(context->request->cancellation, 0) == WAIT_OBJECT_0) {
        context->result->status = "cancelled"; context->result->completion = "cancelled"; return 1;
    }
    if (++context->steps > 100000u || GetTickCount64() >= context->deadline) {
        context->result->status = "timed_out"; context->result->completion = "deadline reached"; return 1;
    }
    return 0;
}

static int wait_socket(network_context *context, int write) {
    fd_set descriptors, errors;
    struct timeval timeout = {0, 10000};
    int rc;
    if (stopped(context)) return -1;
    FD_ZERO(&descriptors); FD_ZERO(&errors);
    FD_SET(context->socket, &descriptors); FD_SET(context->socket, &errors);
    rc = select(0, write ? NULL : &descriptors, write ? &descriptors : NULL, &errors, &timeout);
    if (rc == SOCKET_ERROR || FD_ISSET(context->socket, &errors)) return -1;
    return rc > 0;
}

static int connect_socket(network_context *context) {
    const cdx_network_request *request = context->request;
    cdx_proxy_request route;
    memset(&route, 0, sizeof(route));
    route.host = request->host; route.port = request->port;
    route.proxy = request->proxy ? request->proxy : "auto";
    route.deadline_ms = context->deadline; route.cancellation = request->cancellation;
    cdx_proxy_connect(&route, &context->result->proxy);
    context->result->cleanup_complete = context->result->proxy.cleanup_complete;
    if (context->result->proxy.socket == UINTPTR_MAX) {
        context->result->status = context->result->proxy.status;
        context->result->completion = "Initial direct/proxy transport did not connect";
        return 0;
    }
    context->socket = (SOCKET)context->result->proxy.socket;
    context->result->proxy.socket = UINTPTR_MAX;
    return 1;
}

static int send_bytes(network_context *context, const unsigned char *data, size_t size) {
    size_t sent = 0;
    for (unsigned int i = 0; i < 100000u && sent < size && !stopped(context); ++i) {
        int ready = wait_socket(context, 1), count;
        if (ready < 0) return 0;
        if (!ready) continue;
        count = send(context->socket, (const char *)data + sent, (int)(size - sent), 0);
        if (count == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
        if (count <= 0) return 0;
        sent += (size_t)count; context->result->wire_bytes_sent += (size_t)count;
    }
    return sent == size;
}

static int store_byte(network_context *context, unsigned char value) {
    cdx_network_result *result = context->result;
    if (result->size == context->request->read_limit_bytes) {
        result->status = "output_limit"; result->completion = "response exceeds read limit"; return 0;
    }
    result->data[result->size++] = value;
    return 1;
}

static int exchange(network_context *context) {
    const cdx_network_request *request = context->request;
    cdx_network_result *result = context->result;
    int telnet = !strcmp(request->protocol, "telnet"), state = 0, command = 0;
    unsigned int subnegotiation = 0;
    uint64_t idle_deadline;
    result->stage = "exchange";
    result->data = (unsigned char *)malloc(request->read_limit_bytes);
    if (!result->data) { result->status = "allocation_error"; return 0; }
    if (request->payload_size) {
        unsigned char *wire = (unsigned char *)malloc(request->payload_size * 2u);
        size_t size = 0;
        if (!wire) { result->status = "allocation_error"; return 0; }
        for (size_t i = 0; i < request->payload_size; ++i) {
            wire[size++] = request->payload[i];
            if (telnet && request->payload[i] == 255u) wire[size++] = 255u;
        }
        /* A partial socket write cannot establish whether an application mutation ran. */
        result->application_write_attempted = 1;
        if (!send_bytes(context, wire, size)) { free(wire); return 0; }
        result->payload_bytes_sent = request->payload_size;
        free(wire);
    }
    idle_deadline = GetTickCount64() + request->idle_timeout_ms;
    for (unsigned int i = 0; i < 100000u && !stopped(context); ++i) {
        unsigned char bytes[2048];
        int ready = wait_socket(context, 0), count;
        if (ready < 0) return 0;
        if (!ready) {
            if (result->size && GetTickCount64() >= idle_deadline) {
                if (telnet && state) { result->status = "protocol_error"; result->completion = "incomplete Telnet control sequence"; return 0; }
                result->status = "completed"; result->completion = "idle"; return 1;
            }
            continue;
        }
        count = recv(context->socket, (char *)bytes, sizeof(bytes), 0);
        if (!count) {
            result->status = telnet && state ? "protocol_error" : "completed";
            result->completion = telnet && state ? "incomplete Telnet control sequence" : "eof";
            return !state;
        }
        if (count == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
        if (count < 0) return 0;
        idle_deadline = GetTickCount64() + request->idle_timeout_ms;
        for (int at = 0; at < count; ++at) {
            unsigned char value = bytes[at];
            if (!telnet || (!state && value != 255u)) { if (!store_byte(context, value)) return 0; continue; }
            if (!state) state = 1;
            else if (state == 1) {
                if (value == 255u) { if (!store_byte(context, value)) return 0; state = 0; }
                else if (value >= 251u && value <= 254u) { command = value; state = 2; }
                else if (value == 250u) { state = 3; subnegotiation = 0; }
                else state = 0;
            } else if (state == 2) {
                unsigned char reply[3] = {255u, command == 251 ? 254u : 252u, value};
                if ((command == 251 || command == 253) && !send_bytes(context, reply, sizeof(reply))) return 0;
                state = 0;
            } else {
                if (++subnegotiation > 4096u) { result->status = "protocol_error"; result->completion = "Telnet subnegotiation limit"; return 0; }
                if (state == 4) state = value == 240u ? 0 : 3;
                else if (value == 255u) state = 4;
            }
        }
    }
    return 0;
}

static size_t mqtt_length(unsigned char *target, size_t size) {
    size_t used = 0;
    for (unsigned int i = 0; i < 4u; ++i) {
        unsigned char digit = (unsigned char)(size % 128u);
        size /= 128u; if (size) digit |= 128u;
        target[used++] = digit; if (!size) break;
    }
    return used;
}

static int mqtt(network_context *context) {
    const cdx_network_request *request = context->request;
    cdx_network_result *result = context->result;
    unsigned char packet[128], connack[4], disconnect[2] = {0xe0u, 0};
    char generated_id[64];
    const char *id = request->client_id;
    size_t id_size, at = 0, length_size;
    result->stage = "mqtt_connect";
    if (!*id) { (void)snprintf(generated_id, sizeof(generated_id), "cdx-%lu-%llu", (unsigned long)GetCurrentProcessId(), (unsigned long long)GetTickCount64()); id = generated_id; }
    id_size = strlen(id);
    packet[at++] = 0x10u; length_size = mqtt_length(packet + at, 12u + id_size); at += length_size;
    memcpy(packet + at, "\0\4MQTT\4\2\0\0", 10u); at += 10u;
    packet[at++] = (unsigned char)(id_size >> 8); packet[at++] = (unsigned char)id_size;
    memcpy(packet + at, id, id_size); at += id_size;
    if (!send_bytes(context, packet, at)) return 0;
    at = 0;
    for (unsigned int i = 0; i < 6000u && at < sizeof(connack) && !stopped(context); ++i) {
        int ready = wait_socket(context, 0), got;
        if (ready < 0) return 0;
        if (!ready) continue;
        got = recv(context->socket, (char *)connack + at, (int)(sizeof(connack) - at), 0);
        if (got == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
        if (got <= 0) return 0;
        at += (size_t)got;
    }
    if (at != sizeof(connack)) return 0;
    if (connack[0] != 0x20u || connack[1] != 2u || connack[2] != 0u || connack[3] > 5u) {
        result->status = "protocol_error"; result->completion = "invalid MQTT 3.1.1 CONNACK"; return 0;
    }
    result->mqtt_connack = connack[3];
    if (connack[3]) { result->status = "broker_refused"; result->completion = "MQTT connection refused"; return 0; }
    if (!strcmp(request->action, "publish")) {
        size_t topic_size = strlen(request->topic), remaining = 2u + topic_size + request->payload_size;
        unsigned char *publish = (unsigned char *)malloc(remaining + 5u);
        if (!publish) { result->status = "allocation_error"; return 0; }
        at = 0; publish[at++] = request->retain ? 0x31u : 0x30u;
        at += mqtt_length(publish + at, remaining);
        publish[at++] = (unsigned char)(topic_size >> 8); publish[at++] = (unsigned char)topic_size;
        memcpy(publish + at, request->topic, topic_size); at += topic_size;
        if (request->payload_size) memcpy(publish + at, request->payload, request->payload_size);
        at += request->payload_size;
        result->stage = "mqtt_publish";
        result->application_write_attempted = 1;
        if (!send_bytes(context, publish, at)) { free(publish); return 0; }
        result->payload_bytes_sent = request->payload_size;
        free(publish);
        result->completion = "qos0_sent_not_acknowledged";
    } else result->completion = "mqtt_connected";
    (void)send_bytes(context, disconnect, sizeof(disconnect));
    if (!strcmp(result->status, "timed_out") || !strcmp(result->status, "cancelled")) return 0;
    result->status = "completed";
    return 1;
}
