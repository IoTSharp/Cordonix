#include "cdx/operations.h"
#include "cdx/vnc.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *string_field(const cdx_json *json, int object, const char *name, int *valid) {
    int node = cdx_json_get(json, object, name);
    if (node < 0) return "";
    if (json->nodes[node].type != CDX_JSON_STRING) { *valid = 0; return ""; }
    return json->nodes[node].string;
}

static unsigned number_field(const cdx_json *json, int object, const char *name, unsigned fallback, unsigned maximum, int *valid) {
    int node = cdx_json_get(json, object, name);
    double value;
    if (node < 0) return fallback;
    value = json->nodes[node].number;
    if (json->nodes[node].type != CDX_JSON_NUMBER || !isfinite(value) || value < 1 || value > maximum || floor(value) != value) {
        *valid = 0; return fallback;
    }
    return (unsigned)value;
}

void cdx_vnc_call(const cdx_json *json, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output) {
    cdx_vnc_request request = {0};
    cdx_vnc_result result;
    cdx_path_char *native_path = NULL;
    const char *path;
    int valid = 1, node;
    request.host = string_field(json, arguments, "host", &valid);
    path = string_field(json, arguments, "path", &valid);
    request.password = string_field(json, arguments, "password", &valid);
    request.credential = string_field(json, arguments, "credential", &valid);
    request.proxy = string_field(json, arguments, "proxy", &valid);
    if (!*request.proxy) request.proxy = "auto";
    request.port = number_field(json, arguments, "port", 5900, 65535, &valid);
    request.timeout_ms = number_field(json, arguments, "timeout_ms", 60000, 300000, &valid);
    request.prompt_for_password = 1;
    node = cdx_json_get(json, arguments, "prompt_for_password");
    if (node >= 0) {
        if (json->nodes[node].type != CDX_JSON_BOOL) valid = 0;
        else request.prompt_for_password = json->nodes[node].boolean;
    }
    if (!*request.host || !*path || strlen(request.host) > 45 || strlen(request.password) > 512 ||
        strlen(request.credential) > 256 || strpbrk(request.credential, "\r\n")) valid = 0;
    if (!valid) {
        cdx_json_raw(output, "{\"status\":\"validation_error\",\"written_to_disk\":false,\"message\":\"Supply host IP and a new absolute PNG path; port defaults to 5900. Password and credential are optional strings; prompt_for_password is boolean.\"}"); return;
    }
#ifdef _WIN32
    {
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
        if (length > 0 && length <= 32768) native_path = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
        if (native_path && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, native_path, length)) {
            free(native_path); native_path = NULL;
        }
    }
#else
    native_path = (char *)malloc(strlen(path) + 1u);
    if (native_path) memcpy(native_path, path, strlen(path) + 1u);
#endif
    if (!native_path) { cdx_json_raw(output, "{\"status\":\"encoding_error\",\"written_to_disk\":false}"); return; }
    request.path = native_path; request.cancellation = cancellation;
    cdx_vnc_screenshot(&request, &result);
    free(native_path);
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, result.image.status, strlen(result.image.status));
    cdx_json_raw(output, ",\"stage\":"); cdx_json_quote(output, result.stage, strlen(result.stage));
    cdx_json_raw(output, ",\"written_to_disk\":"); cdx_json_raw(output, result.image.written_to_disk ? "true" : "false");
    cdx_json_raw(output, ",\"path\":");
    if (result.image.written_to_disk) cdx_json_quote(output, path, strlen(path)); else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"width\":"); cdx_json_uint(output, (unsigned)result.image.width);
    cdx_json_raw(output, ",\"height\":"); cdx_json_uint(output, (unsigned)result.image.height);
    cdx_json_raw(output, ",\"bytes\":"); cdx_json_uint(output, result.image.bytes);
    cdx_json_raw(output, ",\"security_type\":"); cdx_json_uint(output, result.security_type);
    cdx_json_raw(output, ",\"credential\":"); cdx_json_quote(output, result.credential, strlen(result.credential));
    cdx_json_raw(output, ",\"credential_status\":"); cdx_json_quote(output, result.credential_status, strlen(result.credential_status));
    cdx_json_raw(output, ",\"credential_error\":"); cdx_json_uint(output, result.credential_error);
    cdx_json_raw(output, ",\"native_error\":"); cdx_json_uint(output, result.image.native_error);
    cdx_json_raw(output, ",\"elapsed_ms\":"); cdx_json_uint(output, result.image.elapsed_ms);
    cdx_json_raw(output, ",\"cleanup_complete\":"); cdx_json_raw(output, result.image.cleanup_complete ? "true" : "false");
    if (!strcmp(result.image.status, "credential_required") || !strcmp(result.image.status, "authentication_failed"))
        cdx_json_raw(output, ",\"message\":\"Ask the user for the VNC password or enable prompt_for_password. A supplied password is saved in the current user's OS credentials only after successful authentication.\"");
    cdx_json_raw(output, "}");
}

int cdx_vnc_status_is_error(const char *status) {
    return !strcmp(status, "authentication_failed") || !strcmp(status, "authentication_error") ||
        !strcmp(status, "credential_error") || !strcmp(status, "protocol_error") ||
        !strcmp(status, "incomplete_frame") || !strcmp(status, "encode_failed") ||
        !strcmp(status, "too_large") || !strcmp(status, "already_exists");
}

const char *cdx_vnc_tool_schema(void) {
    return "{\"name\":\"vnc_screenshot\",\"title\":\"VNC 远程截图\",\"description\":\"Capture one complete remote framebuffer to a new PNG using its IP, optional port (5900) and password. Windows native RFB 3.3/3.7/3.8, None or classic VNC authentication, Raw pixels; Linux returns not_supported. Without a password try None when offered; if authentication is required reuse the current user's saved credential or show a masked native password dialog. Save a supplied/prompted password only after successful authentication; never return it. No keyboard, pointer or clipboard writes. Shared connection, bounded deadline/cancellation, no authentication retry. Check written_to_disk and credential_status separately.\",\"inputSchema\":{\"type\":\"object\",\"required\":[\"host\",\"path\"],\"additionalProperties\":false,\"properties\":{\"host\":{\"type\":\"string\",\"description\":\"Remote IPv4 or IPv6 literal, without URL, brackets or display-number notation.\"},\"port\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":65535,\"default\":5900,\"description\":\"Remote TCP port; omitted means 5900.\"},\"path\":{\"type\":\"string\",\"description\":\"New absolute .png file in an existing local directory; never overwrites.\"},\"password\":{\"type\":\"string\",\"maxLength\":256,\"writeOnly\":true,\"description\":\"Optional supplied password; omitted/empty tries no password. Prefer native prompting to keep secrets out of chat/request files. Classic VNC uses the first eight Latin-1 characters. Saved after successful authentication.\"},\"credential\":{\"type\":\"string\",\"maxLength\":256,\"description\":\"Optional Windows Generic UTF-16 credential alias; default Cordonix/VNC/<canonical-IP>:<port>. A supplied password replaces this entry only after authentication.\"},\"prompt_for_password\":{\"type\":\"boolean\",\"default\":true,\"description\":\"Show a masked Windows dialog only when VNC authentication needs a missing password. False returns credential_required so the agent can ask the user.\"},\"timeout_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":300000,\"default\":60000,\"description\":\"Whole operation deadline including password dialog, network and PNG publication.\"},\"proxy\":{\"type\":\"string\",\"default\":\"auto\",\"description\":\"auto uses existing local proxy fallback only after connect failure; direct bypasses; explicit http://127.0.0.1:port or socks5://127.0.0.1:port selects a proxy.\"}}},\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":false,\"idempotentHint\":false}}";
}
