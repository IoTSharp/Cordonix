#include "cdx/operations.h"
#include "cdx/ssh.h"
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

static void endpoint(const cdx_json *json, int arguments, const char *key, cdx_ssh_endpoint *value, int required, int *valid) {
    int object = cdx_json_get(json, arguments, key);
    if ((object < 0 && required) || (object >= 0 && json->nodes[object].type != CDX_JSON_OBJECT)) *valid = 0;
    value->host = text_field(json, object, "host", "", valid);
    value->port = number_field(json, object, "port", 22u, 65535u, valid);
    value->user = text_field(json, object, "user", "", valid);
    value->credential = text_field(json, object, "credential", "", valid);
    value->private_key_path = text_field(json, object, "private_key_path", "", valid);
    value->host_key_sha256 = text_field(json, object, "host_key_sha256", "", valid);
    if (cdx_json_get(json, object, "password") >= 0) *valid = 0;
}

static void encoded_output(cdx_json_output *output, const unsigned char *bytes, size_t size, cdx_encoding encoding, int *valid) {
    char *text = NULL;
    size_t length = 0;
    if (!size) { cdx_json_raw(output, "\"\""); return; }
    if (cdx_decode_to_utf8(bytes, size, encoding, &text, &length)) cdx_json_quote(output, text, length);
    else { cdx_json_raw(output, "null"); *valid = 0; }
    free(text);
}

static void raw_base64(cdx_json_output *output, const unsigned char *bytes, size_t size) {
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

static void ssh_call(const cdx_json *json, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output, int sftp) {
    cdx_ssh_request request;
    cdx_ssh_result result;
    cdx_sftp_options transfer;
    unsigned char *payload = NULL;
    cdx_json_output streams;
    const char *encoding;
    cdx_encoding decoder = CDX_ENCODING_UTF8;
    int valid = 1, decoded = 1, object, node;
    memset(&request, 0, sizeof(request)); memset(&result, 0, sizeof(result));
    memset(&transfer, 0, sizeof(transfer));
    request.action = sftp ? "sftp" : text_field(json, arguments, "action", "probe", &valid);
    request.route = text_field(json, arguments, "route", "direct", &valid);
    request.script = text_field(json, arguments, "script", "", &valid);
    request.timeout_ms = number_field(json, arguments, "timeout_ms", 30000u, 300000u, &valid);
    request.output_limit_bytes = number_field(json, arguments, "output_limit_bytes", 65536u, CDX_SSH_MAX_OUTPUT, &valid);
    request.cancellation = cancellation;
    request.proxy = text_field(json, arguments, "proxy", "auto", &valid);
    endpoint(json, arguments, "target", &request.target, 1, &valid);
    endpoint(json, arguments, "bastion", &request.bastion, strcmp(request.route, "direct") != 0, &valid);
    node = cdx_json_get(json, arguments, "require_root");
    if (node >= 0) {
        if (json->nodes[node].type != CDX_JSON_BOOL) valid = 0;
        else request.require_root = json->nodes[node].boolean;
    }
    object = cdx_json_get(json, arguments, "interaction");
    if (object >= 0 && json->nodes[object].type != CDX_JSON_OBJECT) valid = 0;
    request.interaction.ready_prompt = text_field(json, object, "ready_prompt", "", &valid);
    request.interaction.selection = text_field(json, object, "selection", "", &valid);
    request.interaction.selection_prefix = text_field(json, object, "selection_prefix", "", &valid);
    request.interaction.password_prompt = text_field(json, object, "password_prompt", "Password:", &valid);
    request.interaction.shell_prompt = text_field(json, object, "shell_prompt", "", &valid);
    request.interaction.su_credential = text_field(json, object, "su_credential", "", &valid);
    request.interaction.su_password_prompt = text_field(json, object, "su_password_prompt", "Password:", &valid);
    request.interaction.root_prompt = text_field(json, object, "root_prompt", "", &valid);
    encoding = text_field(json, arguments, "output_encoding", "utf-8", &valid);
    if (!strcmp(encoding, "cp936") || !strcmp(encoding, "gbk")) decoder = CDX_ENCODING_CP936;
    else if (strcmp(encoding, "utf-8")) valid = 0;
    if (cdx_json_get(json, arguments, "password") >= 0) valid = 0;
    if (sftp) {
        const char *hex = text_field(json, arguments, "data_hex", "", &valid);
        size_t length = strlen(hex);
        int permission = cdx_json_get(json, arguments, "allow_write");
        transfer.operation = text_field(json, arguments, "action", "stat", &valid);
        transfer.path = text_field(json, arguments, "path", "", &valid);
        transfer.read_bytes = number_field(json, arguments, "read_bytes", (unsigned int)request.output_limit_bytes,
            (unsigned int)request.output_limit_bytes, &valid);
        if (permission >= 0) {
            if (json->nodes[permission].type != CDX_JSON_BOOL) valid = 0;
            else transfer.allow_write = json->nodes[permission].boolean;
        }
        node = cdx_json_get(json, arguments, "offset");
        if (node >= 0) {
            double value = json->nodes[node].number;
            if (json->nodes[node].type != CDX_JSON_NUMBER || value < 0 || value > 9007199254740991.0 || floor(value) != value) valid = 0;
            else transfer.offset = (uint64_t)value;
        }
        if (request.require_root || length > 2u * CDX_SSH_MAX_SCRIPT || length % 2u) valid = 0;
        if (valid && length) {
            payload = (unsigned char *)malloc(length / 2u);
            if (!payload) valid = 0;
            for (size_t i = 0; valid && i < length; i += 2u) {
                unsigned int value = 0;
                for (size_t digit = 0; digit < 2u; ++digit) {
                    char ch = hex[i + digit];
                    if (ch >= '0' && ch <= '9') value = value * 16u + (unsigned int)(ch - '0');
                    else if (ch >= 'a' && ch <= 'f') value = value * 16u + (unsigned int)(ch - 'a' + 10);
                    else if (ch >= 'A' && ch <= 'F') value = value * 16u + (unsigned int)(ch - 'A' + 10);
                    else valid = 0;
                }
                payload[i / 2u] = (unsigned char)value;
            }
        }
        transfer.data = payload; transfer.data_size = length / 2u; request.sftp = &transfer;
    }
    if (valid) cdx_ssh_execute(&request, &result);
    else {
        result.status = "validation_error"; result.stage = "validation";
        result.message = "Use typed SSH fields and credential aliases; plaintext passwords are not accepted"; result.cleanup_complete = 1;
    }
    cdx_json_output_init(&streams);
    cdx_json_raw(&streams, ",\"stdout\":"); encoded_output(&streams, result.stdout_data, sftp ? 0 : result.stdout_size, decoder, &decoded);
    cdx_json_raw(&streams, ",\"stderr\":"); encoded_output(&streams, result.stderr_data, result.stderr_size, decoder, &decoded);
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, decoded ? result.status : "encoding_error", strlen(decoded ? result.status : "encoding_error"));
    cdx_json_raw(output, ",\"transport_status\":"); cdx_json_quote(output, result.status, strlen(result.status));
    cdx_json_raw(output, ",\"stage\":"); cdx_json_quote(output, result.stage, strlen(result.stage));
    cdx_json_raw(output, ",\"message\":"); cdx_json_quote(output, result.message, strlen(result.message));
    cdx_json_raw(output, ",\"route\":"); cdx_json_quote(output, request.route, strlen(request.route));
    cdx_json_raw(output, ",\"host_key_sha256\":"); cdx_json_quote(output, result.host_key_sha256, strlen(result.host_key_sha256));
    cdx_json_raw(output, ",\"bastion_host_key_sha256\":"); cdx_json_quote(output, result.bastion_host_key_sha256, strlen(result.bastion_host_key_sha256));
    cdx_json_raw(output, ",\"target_ssh_verified\":");
    cdx_json_raw(output, result.target_host_key_verified ? "true" : "false");
    cdx_json_raw(output, ",\"bastion_ssh_verified\":"); cdx_json_raw(output, result.bastion_host_key_verified ? "true" : "false");
    cdx_json_raw(output, ",\"exit_code\":");
    if (result.exit_code_known) cdx_json_uint(output, (unsigned int)result.exit_code); else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"remote_command_started\":"); cdx_json_raw(output, result.remote_command_started ? "true" : "false");
    cdx_json_raw(output, ",\"remote_outcome\":");
    cdx_json_raw(output, result.exit_code_known || result.remote_result_known ? "\"known\"" : result.remote_command_started ? "\"unknown\"" : "\"not_run\"");
    cdx_json_raw(output, ",\"combined_output\":"); cdx_json_raw(output, !strcmp(request.route, "interactive") ? "true" : "false");
    cdx_json_raw(output, ",\"elapsed_ms\":"); cdx_json_uint(output, result.elapsed_ms);
    cdx_json_raw(output, ",\"cleanup_complete\":"); cdx_json_raw(output, result.cleanup_complete ? "true" : "false");
    cdx_json_raw(output, ",\"session_allocations_remaining\":"); cdx_json_uint(output, result.session_allocations_remaining);
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
    if (streams.data) cdx_json_rawn(output, streams.data, streams.size);
    if (sftp) {
        static const char digits[] = "0123456789abcdef";
        cdx_json_raw(output, ",\"data_hex\":\"");
        for (size_t i = 0; i < result.stdout_size; ++i) {
            char pair[2] = {digits[result.stdout_data[i] >> 4], digits[result.stdout_data[i] & 15u]};
            cdx_json_rawn(output, pair, 2u);
        }
        cdx_json_raw(output, "\",\"file_size\":");
        if (result.file_attribute_flags & 1u) cdx_json_uint(output, result.file_size); else cdx_json_raw(output, "null");
        cdx_json_raw(output, ",\"file_permissions\":");
        if (result.file_attribute_flags & 4u) cdx_json_uint(output, result.file_permissions); else cdx_json_raw(output, "null");
        cdx_json_raw(output, ",\"sftp_error\":"); cdx_json_uint(output, result.sftp_error);
        cdx_json_raw(output, ",\"transferred_bytes\":"); cdx_json_uint(output, result.transferred_bytes);
    }
    if (!decoded) {
        cdx_json_raw(output, ",\"stdout_base64\":"); raw_base64(output, result.stdout_data, result.stdout_size);
        cdx_json_raw(output, ",\"stderr_base64\":"); raw_base64(output, result.stderr_data, result.stderr_size);
    }
    cdx_json_raw(output, "}"); cdx_json_output_free(&streams); cdx_ssh_result_free(&result);
    if (payload) { memset(payload, 0, transfer.data_size); free(payload); }
}

void cdx_ssh_call(const cdx_json *json, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output) {
    ssh_call(json, arguments, cancellation, output, 0);
}
void cdx_sftp_call(const cdx_json *json, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output) {
    ssh_call(json, arguments, cancellation, output, 1);
}

const char *cdx_sftp_tool_schema(void) {
    return "{\"name\":\"sftp\",\"title\":\"SFTP 文件通信\",\"description\":\"Native libssh2 SFTP stat, bounded binary read and exclusive new-file write, with direct or SSH direct-tcpip jump transport. Reuses SSH credential aliases and independent host-key pins. Writes require allow_write=true and never overwrite existing paths. No interactive-bastion SFTP, shell client, remote elevation or retry. Windows only; Linux returns not_supported. Remote mutation can remain unknown after timeout/cancel.\",\"inputSchema\":{\"type\":\"object\",\"required\":[\"target\",\"path\"],\"properties\":{\"proxy\":{\"type\":\"string\",\"default\":\"auto\",\"description\":\"auto probes local 7890/1080 only after direct network connect fails; direct bypasses; explicit http://127.0.0.1:port or socks5://127.0.0.1:port selects one proxy. No retry after application dispatch.\"},\"action\":{\"type\":\"string\",\"enum\":[\"stat\",\"read\",\"write\"],\"default\":\"stat\"},\"route\":{\"type\":\"string\",\"enum\":[\"direct\",\"jump\"],\"default\":\"direct\"},\"target\":{\"$ref\":\"#/$defs/endpoint\"},\"bastion\":{\"$ref\":\"#/$defs/endpoint\"},\"path\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":4096},\"data_hex\":{\"type\":\"string\",\"maxLength\":131072},\"allow_write\":{\"type\":\"boolean\",\"default\":false},\"offset\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":9007199254740991},\"read_bytes\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":262144},\"timeout_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":300000},\"output_limit_bytes\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":262144}},\"$defs\":{\"endpoint\":{\"type\":\"object\",\"required\":[\"host\",\"host_key_sha256\"],\"properties\":{\"host\":{\"type\":\"string\"},\"port\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":65535},\"user\":{\"type\":\"string\"},\"credential\":{\"type\":\"string\"},\"private_key_path\":{\"type\":\"string\"},\"host_key_sha256\":{\"type\":\"string\",\"pattern\":\"^[0-9a-fA-F]{64}$\"}}}}},\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":true,\"idempotentHint\":false}}";
}
