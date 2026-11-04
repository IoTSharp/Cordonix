#include "cdx/http.h"
#include "cdx/operations.h"
#include "cdx/document.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int cdx_decode_to_utf8(const unsigned char *, size_t, cdx_encoding, char **, size_t *);

static const char *text_field(const cdx_json *json, int object, const char *key, const char *fallback, int *valid) {
    int node = cdx_json_get(json, object, key);
    if (node < 0) return fallback;
    if (json->nodes[node].type != CDX_JSON_STRING) { *valid = 0; return fallback; }
    return json->nodes[node].string;
}

static unsigned int number_field(const cdx_json *json, int object, const char *key, unsigned int fallback, unsigned int maximum, int *valid) {
    int node = cdx_json_get(json, object, key);
    double value;
    if (node < 0) return fallback;
    value = json->nodes[node].number;
    if (json->nodes[node].type != CDX_JSON_NUMBER || value < 1 || value > maximum || floor(value) != value) { *valid = 0; return fallback; }
    return (unsigned int)value;
}

static void base64(cdx_json_output *output, const unsigned char *bytes, size_t size) {
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    cdx_json_raw(output, "\"");
    for (size_t i = 0; i < size; i += 3u) {
        unsigned int value = (unsigned int)bytes[i] << 16;
        char block[4];
        if (i + 1u < size) value |= (unsigned int)bytes[i+1u] << 8;
        if (i + 2u < size) value |= bytes[i+2u];
        block[0] = digits[value >> 18]; block[1] = digits[(value >> 12) & 63u];
        block[2] = i + 1u < size ? digits[(value >> 6) & 63u] : '=';
        block[3] = i + 2u < size ? digits[value & 63u] : '=';
        cdx_json_rawn(output, block, 4u);
    }
    cdx_json_raw(output, "\"");
}

void cdx_http_call(const cdx_json *json, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output) {
    cdx_http_request request;
    cdx_http_result result;
    cdx_http_header headers[CDX_HTTP_MAX_HEADERS];
    const char *body, *encoding;
    cdx_encoding decoder = CDX_ENCODING_UTF8;
    char *decoded = NULL;
    size_t decoded_size = 0;
    int valid = 1, body_decoded = 1, array, node;
    memset(&request, 0, sizeof(request)); memset(&result, 0, sizeof(result)); memset(headers, 0, sizeof(headers));
    request.url = text_field(json, arguments, "url", "", &valid);
    request.method = text_field(json, arguments, "method", "GET", &valid);
    request.proxy = text_field(json, arguments, "proxy", "", &valid);
    body = text_field(json, arguments, "body", "", &valid);
    request.body = (const unsigned char *)body; request.body_size = strlen(body);
    request.timeout_ms = number_field(json, arguments, "timeout_ms", 30000u, 300000u, &valid);
    request.response_limit_bytes = number_field(json, arguments, "response_limit_bytes", 65536u, CDX_HTTP_MAX_RESPONSE, &valid);
    request.cancellation = cancellation;
    encoding = text_field(json, arguments, "output_encoding", "utf-8", &valid);
    if (!strcmp(encoding, "cp936") || !strcmp(encoding, "gbk")) decoder = CDX_ENCODING_CP936;
    else if (strcmp(encoding, "utf-8")) valid = 0;
    array = cdx_json_get(json, arguments, "headers");
    if (array >= 0) {
        if (json->nodes[array].type != CDX_JSON_ARRAY) valid = 0;
        else for (node = json->nodes[array].first; node >= 0 && request.header_count < CDX_HTTP_MAX_HEADERS + 1u; node = json->nodes[node].next) {
            if (request.header_count == CDX_HTTP_MAX_HEADERS || json->nodes[node].type != CDX_JSON_OBJECT) { valid = 0; break; }
            headers[request.header_count].name = text_field(json, node, "name", "", &valid);
            headers[request.header_count].value = text_field(json, node, "value", "", &valid);
            ++request.header_count;
        }
    }
    request.headers = headers;
    if (valid) cdx_http_execute(&request, &result);
    else {
        result.status = "validation_error"; result.stage = "validation"; result.message = "Use typed HTTP fields within the documented bounds";
        result.cleanup_complete = 1;
    }
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, result.status, strlen(result.status));
    cdx_json_raw(output, ",\"stage\":"); cdx_json_quote(output, result.stage, strlen(result.stage));
    cdx_json_raw(output, ",\"message\":"); cdx_json_quote(output, result.message, strlen(result.message));
    cdx_json_raw(output, ",\"status_code\":");
    if (result.status_code) cdx_json_uint(output, result.status_code); else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"http_success\":"); cdx_json_raw(output, result.status_code >= 200u && result.status_code < 300u ? "true" : "false");
    cdx_json_raw(output, ",\"response_headers\":");
    cdx_json_quote(output, result.headers ? result.headers : "", result.headers_size);
    cdx_json_raw(output, ",\"body\":");
    if (!result.body_size) cdx_json_raw(output, "\"\"");
    else if (cdx_decode_to_utf8(result.body, result.body_size, decoder, &decoded, &decoded_size)) cdx_json_quote(output, decoded, decoded_size);
    else { cdx_json_raw(output, "null"); body_decoded = 0; }
    cdx_json_raw(output, ",\"body_encoding_status\":"); cdx_json_raw(output, body_decoded ? "\"ok\"" : "\"encoding_error\"");
    cdx_json_raw(output, ",\"body_base64\":"); base64(output, result.body, result.body_size);
    cdx_json_raw(output, ",\"body_bytes\":"); cdx_json_uint(output, result.body_size);
    cdx_json_raw(output, ",\"request_started\":"); cdx_json_raw(output, result.request_started ? "true" : "false");
    cdx_json_raw(output, ",\"response_complete\":"); cdx_json_raw(output, result.response_complete ? "true" : "false");
    cdx_json_raw(output, ",\"request_send_started\":"); cdx_json_raw(output, result.request_send_started ? "true" : "false");
    cdx_json_raw(output, ",\"proxy_bridge_used\":"); cdx_json_raw(output, result.proxy_bridge_used ? "true" : "false");
    cdx_json_raw(output, ",\"proxy_bridge_error\":"); cdx_json_uint(output, result.proxy_bridge_error);
    cdx_json_raw(output, ",\"proxy\":{\"status\":");
    cdx_json_quote(output, result.proxy.status ? result.proxy.status : "not_run", strlen(result.proxy.status ? result.proxy.status : "not_run"));
    cdx_json_raw(output, ",\"selected_proxy\":"); cdx_json_quote(output, result.proxy.selected_proxy, strlen(result.proxy.selected_proxy));
    cdx_json_raw(output, ",\"direct_attempted\":"); cdx_json_raw(output, result.proxy.direct_attempted ? "true" : "false");
    cdx_json_raw(output, ",\"direct_error\":"); cdx_json_uint(output, result.proxy.direct_error);
    cdx_json_raw(output, ",\"native_error\":"); cdx_json_uint(output, result.proxy.native_error);
    cdx_json_raw(output, ",\"probed_ports\":[");
    for (size_t i = 0; i < result.proxy.probe_count && i < 2u; ++i) {
        if (i) cdx_json_raw(output, ",");
        cdx_json_raw(output, "{\"port\":"); cdx_json_uint(output, result.proxy.probed_ports[i]);
        cdx_json_raw(output, ",\"open\":"); cdx_json_raw(output, result.proxy.probe_open[i] ? "true}" : "false}");
    }
    cdx_json_raw(output, "],\"tunnel_attempts\":"); cdx_json_uint(output, result.proxy.tunnel_attempts);
    cdx_json_raw(output, ",\"cleanup_complete\":"); cdx_json_raw(output, !result.proxy.status || result.proxy.cleanup_complete ? "true}" : "false}");
    cdx_json_raw(output, ",\"remote_outcome\":"); cdx_json_raw(output, result.response_complete ? "\"known\"" : result.request_started ? "\"unknown\"" : "\"not_run\"");
    cdx_json_raw(output, ",\"native_error\":"); cdx_json_uint(output, result.native_error);
    cdx_json_raw(output, ",\"elapsed_ms\":"); cdx_json_uint(output, result.elapsed_ms);
    cdx_json_raw(output, ",\"cleanup_complete\":"); cdx_json_raw(output, result.cleanup_complete ? "true" : "false");
    cdx_json_raw(output, "}"); free(decoded); cdx_http_result_free(&result);
}

const char *cdx_http_tool_schema(void) {
    return "{\"name\":\"http\",\"title\":\"HTTP/HTTPS 网络请求\","
        "\"description\":\"Native Windows WinHTTP with typed requests and strict system TLS verification. No curl process. proxy defaults to auto: direct first, then evaluate local 7890/1080 HTTP CONNECT/SOCKS5 only after a connection failure before any SENDING_REQUEST/REQUEST_SENT phase. HTTP statuses, TLS/auth failures and requests with send-phase evidence never trigger route replay. A bounded per-call local bridge supports SOCKS without changing TLS or machine settings. Redirects/cookies/automatic authentication are disabled. Binary response bytes remain in body_base64; HTTP status_code and body decoding are independent of transport status. Interrupted dispatch retains remote_outcome=unknown. Linux returns not_supported.\","
        "\"inputSchema\":{\"type\":\"object\",\"required\":[\"url\"],\"properties\":{"
        "\"url\":{\"type\":\"string\",\"maxLength\":8192,\"description\":\"Absolute HTTP/HTTPS URL without credentials or fragment\"},"
        "\"method\":{\"type\":\"string\",\"enum\":[\"GET\",\"HEAD\",\"POST\",\"PUT\",\"PATCH\",\"DELETE\",\"OPTIONS\"],\"default\":\"GET\"},"
        "\"headers\":{\"type\":\"array\",\"maxItems\":32,\"items\":{\"type\":\"object\",\"required\":[\"name\",\"value\"],\"properties\":{\"name\":{\"type\":\"string\"},\"value\":{\"type\":\"string\"}}}},"
        "\"body\":{\"type\":\"string\",\"maxLength\":65536,\"description\":\"UTF-8 bytes; GET/HEAD require empty body\"},"
        "\"proxy\":{\"type\":\"string\",\"default\":\"auto\",\"maxLength\":4096,\"description\":\"auto/empty evaluates local proxies after proven pre-send direct failure; direct disables fallback. Explicit HTTP proxy preserves per-request behavior. socks5://127.0.0.1:PORT uses only that tunnel.\"},"
        "\"timeout_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":300000,\"default\":30000},"
        "\"response_limit_bytes\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":262144,\"default\":65536},"
        "\"output_encoding\":{\"type\":\"string\",\"enum\":[\"utf-8\",\"cp936\",\"gbk\"],\"default\":\"utf-8\"}}},"
        "\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":true,\"idempotentHint\":false}}";
}
