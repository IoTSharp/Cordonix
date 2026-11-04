#include "cdx/json.h"
#include "cdx/serial.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <time.h>
#endif

static const char *string_field(const cdx_json *json, int arguments, const char *key, const char *fallback, int *valid) {
    int node = cdx_json_get(json, arguments, key);
    if (node < 0) return fallback;
    if (json->nodes[node].type != CDX_JSON_STRING) { *valid = 0; return fallback; }
    return json->nodes[node].string;
}

static unsigned int integer_field(const cdx_json *json, int arguments, const char *key, unsigned int fallback,
                                  unsigned int minimum, unsigned int maximum, int *valid) {
    int node = cdx_json_get(json, arguments, key);
    double value;
    if (node < 0) return fallback;
    value = json->nodes[node].number;
    if (json->nodes[node].type != CDX_JSON_NUMBER || value < minimum || value > maximum || floor(value) != value) { *valid = 0; return fallback; }
    return (unsigned int)value;
}

static int boolean_field(const cdx_json *json, int arguments, const char *key, int *valid) {
    int node = cdx_json_get(json, arguments, key);
    if (node < 0) return 0;
    if (json->nodes[node].type != CDX_JSON_BOOL) { *valid = 0; return 0; }
    return json->nodes[node].boolean;
}

static uint64_t clock_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec value;
    (void)clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint64_t)value.tv_sec * 1000u + (uint64_t)value.tv_nsec / 1000000u;
#endif
}

static int interrupted(cdx_cancel_handle cancellation, uint64_t deadline) {
#ifdef _WIN32
    if (cancellation && WaitForSingleObject(cancellation, 0) == WAIT_OBJECT_0) return 1;
#else
    if (cancellation && atomic_load(cancellation)) return 1;
#endif
    return clock_ms() >= deadline;
}

static int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static void hex_output(cdx_json_output *output, const unsigned char *bytes, size_t count) {
    static const char digits[] = "0123456789abcdef";
    cdx_json_raw(output, "\"");
    for (size_t i = 0; i < count; ++i) {
        char pair[2] = {digits[bytes[i] >> 4], digits[bytes[i] & 15u]};
        cdx_json_rawn(output, pair, 2u);
    }
    cdx_json_raw(output, "\"");
}

static void settings_output(cdx_json_output *output, const cdx_serial_settings *settings, int known) {
    const char *parity = settings->parity == 'N' ? "none" : settings->parity == 'O' ? "odd" :
        settings->parity == 'E' ? "even" : settings->parity == 'M' ? "mark" : settings->parity == 'S' ? "space" : "unknown";
    if (!known) { cdx_json_raw(output, "null"); return; }
    cdx_json_raw(output, "{\"baud_rate\":"); cdx_json_uint(output, settings->baud_rate);
    cdx_json_raw(output, ",\"data_bits\":"); cdx_json_uint(output, settings->data_bits);
    cdx_json_raw(output, ",\"stop_bits\":"); cdx_json_raw(output, settings->stop_bits_tenths == 10u ? "1" : settings->stop_bits_tenths == 15u ? "1.5" : "2");
    cdx_json_raw(output, ",\"parity\":"); cdx_json_quote(output, parity, strlen(parity));
    cdx_json_raw(output, ",\"dtr_control\":"); cdx_json_uint(output, settings->dtr_control);
    cdx_json_raw(output, ",\"rts_control\":"); cdx_json_uint(output, settings->rts_control);
    cdx_json_raw(output, ",\"cts_flow\":"); cdx_json_raw(output, settings->cts_flow ? "true" : "false");
    cdx_json_raw(output, ",\"dsr_flow\":"); cdx_json_raw(output, settings->dsr_flow ? "true" : "false");
    cdx_json_raw(output, ",\"xon_input\":"); cdx_json_raw(output, settings->xon_input ? "true" : "false");
    cdx_json_raw(output, ",\"xon_output\":"); cdx_json_raw(output, settings->xon_output ? "true" : "false");
    cdx_json_raw(output, "}");
}

void cdx_serial_call(const cdx_json *json, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output) {
    cdx_serial_request request;
    cdx_serial_result result;
    unsigned char *transmit = NULL;
    const char *hex, *parity, *availability;
    const char *message = "Use typed serial options; tx_hex needs allow_send=true and line-format fields need configure=true";
    int valid = 1, node, preparation_interrupted = 0;
    uint64_t deadline;
    size_t length;
    memset(&request, 0, sizeof(request)); memset(&result, 0, sizeof(result));
    request.action = string_field(json, arguments, "action", "list", &valid);
    request.port = string_field(json, arguments, "port", "", &valid);
    request.timeout_ms = integer_field(json, arguments, "timeout_ms", 5000u, 1u, 60000u, &valid);
    request.read_idle_ms = integer_field(json, arguments, "read_idle_ms", 100u, 1u, 5000u, &valid);
    request.read_max_bytes = integer_field(json, arguments, "read_max_bytes", 1024u, 0u, CDX_SERIAL_MAX_BYTES, &valid);
    request.max_entries = integer_field(json, arguments, "max_entries", 64u, 1u, CDX_SERIAL_MAX_PORTS, &valid);
    request.allow_send = boolean_field(json, arguments, "allow_send", &valid);
    request.configure = boolean_field(json, arguments, "configure", &valid);
    request.open_probe = boolean_field(json, arguments, "open_probe", &valid);
    request.settings.baud_rate = integer_field(json, arguments, "baud_rate", 115200u, 50u, 4000000u, &valid);
    request.settings.data_bits = integer_field(json, arguments, "data_bits", 8u, 5u, 8u, &valid);
    parity = string_field(json, arguments, "parity", "none", &valid);
    request.settings.parity = !strcmp(parity, "none") ? 'N' : !strcmp(parity, "odd") ? 'O' :
        !strcmp(parity, "even") ? 'E' : !strcmp(parity, "mark") ? 'M' : !strcmp(parity, "space") ? 'S' : 0;
    if (!request.settings.parity) valid = 0;
    request.settings.stop_bits_tenths = 10u;
    node = cdx_json_get(json, arguments, "stop_bits");
    if (node >= 0) {
        double value = json->nodes[node].number;
        if (json->nodes[node].type != CDX_JSON_NUMBER || (value != 1 && value != 1.5 && value != 2)) valid = 0;
        else request.settings.stop_bits_tenths = (unsigned int)(value * 10);
    }
    if (!request.configure && (cdx_json_get(json, arguments, "baud_rate") >= 0 || cdx_json_get(json, arguments, "data_bits") >= 0 ||
        cdx_json_get(json, arguments, "parity") >= 0 || node >= 0)) valid = 0;
    request.cancellation = cancellation;
    hex = string_field(json, arguments, "tx_hex", "", &valid); length = strlen(hex);
    deadline = clock_ms() + request.timeout_ms;
    if (length % 2u || length > CDX_SERIAL_MAX_BYTES * 2u || (length && !request.allow_send)) valid = 0;
    if (valid && length) {
        transmit = (unsigned char *)malloc(length / 2u);
        if (!transmit) valid = 0;
        for (size_t i = 0; valid && i < length / 2u; ++i) {
            int high = hex_digit(hex[2u*i]), low = hex_digit(hex[2u*i+1u]);
            if (high < 0 || low < 0) { valid = 0; break; }
            if (!(i % 256u) && interrupted(cancellation, deadline)) { valid = 0; preparation_interrupted = 1; message = "Serial preparation interrupted before opening the device"; break; }
            transmit[i] = (unsigned char)((high << 4) | low);
        }
    }
    request.tx_data = transmit; request.tx_size = length / 2u;
    if (valid) cdx_serial_execute(&request, &result);
    else {
        result.status = preparation_interrupted ? (clock_ms() >= deadline ? "timed_out" : "cancelled") : "validation_error";
        result.stop_reason = preparation_interrupted ? "preparation" : "validation"; result.message = message; result.cleanup_complete = 1;
    }
    availability = !strcmp(result.status, "not_found") ? "unavailable" : !strcmp(result.status, "busy") ? "busy" :
        !strcmp(result.status, "access_denied_or_busy") ? "access_denied_or_busy" : result.opened ? "opened" : result.mapped ? "mapped_unprobed" : "not_probed";
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, result.status, strlen(result.status));
    cdx_json_raw(output, ",\"action\":"); cdx_json_quote(output, request.action, strlen(request.action));
    cdx_json_raw(output, ",\"port\":"); cdx_json_quote(output, request.port, strlen(request.port));
    cdx_json_raw(output, ",\"message\":"); cdx_json_quote(output, result.message, strlen(result.message));
    cdx_json_raw(output, ",\"stop_reason\":"); cdx_json_quote(output, result.stop_reason, strlen(result.stop_reason));
    cdx_json_raw(output, ",\"availability\":"); cdx_json_quote(output, availability, strlen(availability));
    cdx_json_raw(output, ",\"ports\":[");
    for (size_t i = 0; i < result.count; ++i) {
        if (i) cdx_json_raw(output, ",");
        cdx_json_quote(output, result.ports[i], strlen(result.ports[i]));
    }
    cdx_json_raw(output, "],\"skipped\":"); cdx_json_uint(output, result.skipped);
    cdx_json_raw(output, ",\"truncated\":"); cdx_json_raw(output, result.truncated ? "true" : "false");
    cdx_json_raw(output, ",\"opened\":"); cdx_json_raw(output, result.opened ? "true" : "false");
    cdx_json_raw(output, ",\"open_side_effects_possible\":"); cdx_json_raw(output, result.opened ? "true" : "false");
    cdx_json_raw(output, ",\"dos_device\":"); cdx_json_quote(output, result.dos_device, strlen(result.dos_device));
    cdx_json_raw(output, ",\"default_settings\":"); settings_output(output, &result.default_settings, result.default_settings_known);
    cdx_json_raw(output, ",\"active_settings\":"); settings_output(output, &result.active_settings, result.active_settings_known);
    cdx_json_raw(output, ",\"modem_status\":");
    if (result.modem_status_known) cdx_json_uint(output, result.modem_status); else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"communication_errors\":"); cdx_json_uint(output, result.communication_errors);
    cdx_json_raw(output, ",\"settings_changed\":"); cdx_json_raw(output, result.settings_changed ? "true" : "false");
    cdx_json_raw(output, ",\"write_attempted\":"); cdx_json_raw(output, result.write_attempted ? "true" : "false");
    cdx_json_raw(output, ",\"write_completed\":"); cdx_json_raw(output, result.write_completed ? "true" : "false");
    cdx_json_raw(output, ",\"bytes_written\":"); cdx_json_uint(output, result.tx_size);
    cdx_json_raw(output, ",\"send_outcome\":"); cdx_json_raw(output, result.write_completed ? "\"known\"" : result.write_attempted ? "\"unknown\"" : "\"not_run\"");
    cdx_json_raw(output, ",\"device_outcome\":"); cdx_json_raw(output, result.write_attempted ? "\"unknown\"" : "\"not_run\"");
    cdx_json_raw(output, ",\"rx_hex\":"); hex_output(output, result.rx_data, result.rx_size);
    cdx_json_raw(output, ",\"bytes_read\":"); cdx_json_uint(output, result.rx_size);
    cdx_json_raw(output, ",\"win32_error\":"); cdx_json_uint(output, result.win32_error);
    cdx_json_raw(output, ",\"elapsed_ms\":"); cdx_json_uint(output, result.elapsed_ms);
    cdx_json_raw(output, ",\"cleanup_complete\":"); cdx_json_raw(output, result.cleanup_complete ? "true" : "false");
    cdx_json_raw(output, "}");
    free(transmit); cdx_serial_result_free(&result);
}

const char *cdx_serial_tool_schema(void) {
    return "{\"name\":\"serial\",\"title\":\"串口枚举、诊断与收发\",\"description\":\"Native Windows serial communication without external components. list and default diagnose never open the device. diagnose open_probe=true explicitly opens it to inspect active settings/modem status; opening/closing may affect hardware lines through the driver. exchange reads bounded binary bytes; a nonempty tx_hex requires allow_send=true. Line format changes require configure=true, preserve existing DTR/RTS/flow modes and are reported. No baud scan, AT command, BREAK or automatic reset. Cancellation/timeouts retain confirmed partial bytes; an attempted transmission may have device_outcome=unknown. read_max_bytes is a cap; read_idle_ms ends an idle capture. All call-owned local handles close, except explicitly reported driver cancellation failures. Linux returns not_supported.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"list\",\"diagnose\",\"exchange\"],\"default\":\"list\"},\"port\":{\"type\":\"string\",\"pattern\":\"^[Cc][Oo][Mm][1-9][0-9]{0,4}$\"},\"open_probe\":{\"type\":\"boolean\",\"default\":false},\"allow_send\":{\"type\":\"boolean\",\"default\":false},\"tx_hex\":{\"type\":\"string\",\"maxLength\":131072,\"pattern\":\"^([0-9a-fA-F]{2})*$\"},\"configure\":{\"type\":\"boolean\",\"default\":false},\"baud_rate\":{\"type\":\"integer\",\"minimum\":50,\"maximum\":4000000,\"default\":115200},\"data_bits\":{\"type\":\"integer\",\"minimum\":5,\"maximum\":8,\"default\":8},\"parity\":{\"type\":\"string\",\"enum\":[\"none\",\"odd\",\"even\",\"mark\",\"space\"],\"default\":\"none\"},\"stop_bits\":{\"type\":\"number\",\"enum\":[1,1.5,2],\"default\":1},\"read_max_bytes\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":65536,\"default\":1024},\"read_idle_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":5000,\"default\":100},\"timeout_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":60000,\"default\":5000},\"max_entries\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":128,\"default\":64}}},\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":true,\"idempotentHint\":false}}";
}
