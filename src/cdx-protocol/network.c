#include "cdx/operations.h"
#include "cdx/network.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *string_field(const cdx_json *json, int object, const char *name, const char *fallback, int *valid) {
    int node = cdx_json_get(json, object, name);
    if (node < 0) return fallback;
    if (json->nodes[node].type != CDX_JSON_STRING) { *valid = 0; return fallback; }
    return json->nodes[node].string;
}
static unsigned int number_field(const cdx_json *json, int object, const char *name, unsigned int fallback, unsigned int maximum, int *valid) {
    int node = cdx_json_get(json, object, name);
    double number;
    if (node < 0) return fallback;
    number = json->nodes[node].number;
    if (json->nodes[node].type != CDX_JSON_NUMBER || number < 1 || number > maximum || floor(number) != number) { *valid = 0; return fallback; }
    return (unsigned int)number;
}
static int boolean_field(const cdx_json *json, int object, const char *name, int *valid) {
    int node = cdx_json_get(json, object, name);
    if (node < 0) return 0;
    if (json->nodes[node].type != CDX_JSON_BOOL) { *valid = 0; return 0; }
    return json->nodes[node].boolean;
}
static int hex_digit(char digit) {
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return -1;
}
static void hex_output(cdx_json_output *output, const unsigned char *data, size_t size) {
    static const char digits[] = "0123456789abcdef";
    cdx_json_raw(output, "\"");
    for (size_t i = 0; i < size; ++i) {
        char encoded[2] = {digits[data[i] >> 4], digits[data[i] & 15u]};
        cdx_json_rawn(output, encoded, sizeof(encoded));
    }
    cdx_json_raw(output, "\"");
}

void cdx_network_call(const cdx_json *json, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output) {
    cdx_network_request request;
    cdx_network_result result;
    const char *hex;
    unsigned char *payload = NULL;
    size_t length;
    int valid = 1;
    memset(&request, 0, sizeof(request)); memset(&result, 0, sizeof(result));
    request.protocol = string_field(json, arguments, "protocol", "tcp", &valid);
    request.action = string_field(json, arguments, "action", "connect", &valid);
    request.host = string_field(json, arguments, "host", "", &valid);
    request.client_id = string_field(json, arguments, "client_id", "", &valid);
    request.topic = string_field(json, arguments, "topic", "", &valid);
    request.proxy = string_field(json, arguments, "proxy", "auto", &valid);
    request.port = number_field(json, arguments, "port", 0u, 65535u, &valid);
    request.timeout_ms = number_field(json, arguments, "timeout_ms", 10000u, 60000u, &valid);
    request.idle_timeout_ms = number_field(json, arguments, "idle_timeout_ms", request.timeout_ms < 200u ? request.timeout_ms : 200u, 10000u, &valid);
    request.read_limit_bytes = number_field(json, arguments, "read_limit_bytes", 4096u, CDX_NETWORK_MAX_BYTES, &valid);
    request.allow_send = boolean_field(json, arguments, "allow_send", &valid);
    request.retain = boolean_field(json, arguments, "retain", &valid);
    request.tls = boolean_field(json, arguments, "tls", &valid);
    request.cancellation = cancellation;
    hex = string_field(json, arguments, "payload_hex", "", &valid);
    length = strlen(hex);
    if ((length & 1u) || length > 2u * CDX_NETWORK_MAX_BYTES || cdx_json_get(json, arguments, "password") >= 0 ||
        cdx_json_get(json, arguments, "username") >= 0) valid = 0;
    if (valid && length) {
        payload = (unsigned char *)malloc(length / 2u);
        if (!payload) valid = 0;
        for (size_t i = 0; valid && i < length; i += 2u) {
            int high = hex_digit(hex[i]), low = hex_digit(hex[i+1u]);
            if (high < 0 || low < 0) { valid = 0; break; }
            payload[i / 2u] = (unsigned char)((high << 4) | low);
        }
    }
    request.payload = payload; request.payload_size = length / 2u;
    if (valid) cdx_network_execute(&request, &result);
    else { result.status = "validation_error"; result.stage = "validation"; result.completion = "Use typed protocol fields and even-length hexadecimal payload"; result.cleanup_complete = 1; result.mqtt_connack = -1; }
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, result.status, strlen(result.status));
    cdx_json_raw(output, ",\"stage\":"); cdx_json_quote(output, result.stage, strlen(result.stage));
    cdx_json_raw(output, ",\"completion\":"); cdx_json_quote(output, result.completion, strlen(result.completion));
    cdx_json_raw(output, ",\"protocol\":"); cdx_json_quote(output, request.protocol, strlen(request.protocol));
    cdx_json_raw(output, ",\"connected\":"); cdx_json_raw(output, result.connected ? "true" : "false");
    cdx_json_raw(output, ",\"application_write_attempted\":"); cdx_json_raw(output, result.application_write_attempted ? "true" : "false");
    cdx_json_raw(output, ",\"remote_outcome\":"); cdx_json_raw(output, result.application_write_attempted ? "\"unknown\"" : "\"not_run\"");
    cdx_json_raw(output, ",\"payload_bytes_sent\":"); cdx_json_uint(output, result.payload_bytes_sent);
    cdx_json_raw(output, ",\"wire_bytes_sent\":"); cdx_json_uint(output, result.wire_bytes_sent);
    cdx_json_raw(output, ",\"bytes_received\":"); cdx_json_uint(output, result.size);
    cdx_json_raw(output, ",\"data_hex\":"); hex_output(output, result.data, result.size);
    cdx_json_raw(output, ",\"mqtt_connack\":"); if (result.mqtt_connack < 0) cdx_json_raw(output, "null"); else cdx_json_uint(output, (unsigned int)result.mqtt_connack);
    cdx_json_raw(output, ",\"proxy\":{\"status\":");
    {
        const char *status = result.proxy.status ? result.proxy.status : "not_run";
        cdx_json_quote(output, status, strlen(status));
    }
    cdx_json_raw(output, ",\"selected_proxy\":"); cdx_json_quote(output, result.proxy.selected_proxy, strlen(result.proxy.selected_proxy));
    cdx_json_raw(output, ",\"direct_attempted\":"); cdx_json_raw(output, result.proxy.direct_attempted ? "true" : "false");
    cdx_json_raw(output, ",\"direct_error\":"); cdx_json_uint(output, result.proxy.direct_error);
    cdx_json_raw(output, ",\"native_error\":"); cdx_json_uint(output, result.proxy.native_error);
    cdx_json_raw(output, ",\"probed_ports\":[");
    for (size_t i = 0; i < result.proxy.probe_count && i < 2u; ++i) {
        if (i) cdx_json_raw(output, ",");
        cdx_json_raw(output, "{\"port\":"); cdx_json_uint(output, result.proxy.probed_ports[i]);
        cdx_json_raw(output, ",\"open\":"); cdx_json_raw(output, result.proxy.probe_open[i] ? "true" : "false");
        cdx_json_raw(output, "}");
    }
    cdx_json_raw(output, "],\"tunnel_attempts\":"); cdx_json_uint(output, result.proxy.tunnel_attempts);
    cdx_json_raw(output, ",\"cleanup_complete\":"); cdx_json_raw(output, result.proxy.status ? (result.proxy.cleanup_complete ? "true" : "false") : "true");
    cdx_json_raw(output, "}");
    cdx_json_raw(output, ",\"elapsed_ms\":"); cdx_json_uint(output, result.elapsed_ms);
    cdx_json_raw(output, ",\"cleanup_complete\":"); cdx_json_raw(output, result.cleanup_complete ? "true" : "false");
    cdx_json_raw(output, "}");
    cdx_network_result_free(&result); free(payload);
}

const char *cdx_network_tool_schema(void) {
    return "{\"name\":\"network\",\"title\":\"TCP、Telnet 与 MQTT 通信\","
        "\"description\":\"Native Windows Winsock client; no external programs. Literal IPv4/IPv6 targets only. TCP/Telnet support connect or bounded binary exchange; Telnet IAC/control is negotiated separately from data. MQTT 3.1.1 supports clean-session CONNECT and explicit QoS0 PUBLISH only, without authentication, subscriptions or TLS. proxy defaults to auto: try direct first and evaluate local 7890/1080 HTTP CONNECT/SOCKS5 routes only after initial connection failure. No route retry occurs after a connected transport or protocol dispatch. Application writes require allow_send=true. A successful socket send or QoS0 publish does not confirm application delivery; remote_outcome remains unknown and mutations are never retried. Each call closes its socket. Linux returns not_supported.\","
        "\"inputSchema\":{\"type\":\"object\",\"required\":[\"host\",\"port\"],\"properties\":{"
        "\"protocol\":{\"type\":\"string\",\"enum\":[\"tcp\",\"telnet\",\"mqtt\"],\"default\":\"tcp\"},"
        "\"action\":{\"type\":\"string\",\"enum\":[\"connect\",\"exchange\",\"publish\"],\"default\":\"connect\"},"
        "\"host\":{\"type\":\"string\"},\"port\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":65535},"
        "\"proxy\":{\"type\":\"string\",\"default\":\"auto\",\"description\":\"auto or empty tries direct then local 7890/1080 HTTP CONNECT/SOCKS5 only before any protocol dispatch. direct suppresses proxy fallback. Explicit http://127.0.0.1:PORT or socks5://127.0.0.1:PORT uses only that local tunnel.\"},"
        "\"payload_hex\":{\"type\":\"string\",\"maxLength\":131072},\"allow_send\":{\"type\":\"boolean\",\"default\":false},"
        "\"tls\":{\"type\":\"boolean\",\"default\":false,\"description\":\"true returns not_supported; there is no silent plaintext fallback\"},"
        "\"client_id\":{\"type\":\"string\",\"maxLength\":64},\"topic\":{\"type\":\"string\",\"maxLength\":1024},"
        "\"retain\":{\"type\":\"boolean\",\"default\":false},\"timeout_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":60000,\"default\":10000},"
        "\"idle_timeout_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":10000,\"default\":200},"
        "\"read_limit_bytes\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":65536,\"default\":4096}}},"
        "\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":true,\"idempotentHint\":false}}";
}
