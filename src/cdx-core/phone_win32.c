#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2bth.h>
#include <bluetoothapis.h>
#include "cdx/phone.h"
#include "cdx/process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct phone_socket {
    SOCKET socket;
    ULONGLONG deadline;
    cdx_cancel_handle cancel;
    cdx_phone_stream *stream;
} phone_socket;

struct cdx_phone_session {
    phone_socket transport;
    cdx_phone_stream stream;
    cdx_phone_hfp hfp;
    uint64_t address, radio_address;
    ULONGLONG expires, connected_at, checked_at;
    char name[1024];
    const char *last_error;
    int winsock;
    HWND parent_window;
};

static void quote_address(cdx_json_output *out, uint64_t address) {
    char value[13];
    (void)snprintf(value, sizeof(value), "%012llX", (unsigned long long)address);
    cdx_json_quote(out, value, 12);
}

static HANDLE open_radio(uint64_t address) {
    BLUETOOTH_FIND_RADIO_PARAMS params = {sizeof(params)};
    HBLUETOOTH_RADIO_FIND find;
    HANDLE radio = NULL, selected = NULL;
    unsigned count;
    find = BluetoothFindFirstRadio(&params, &radio);
    if (!find) return NULL;
    for (count = 0; count < 16; ++count) {
        BLUETOOTH_RADIO_INFO info = {0};
        info.dwSize = sizeof(info);
        if (!BluetoothGetRadioInfo(radio, &info) && (!address || info.address.ullLong == address)) {
            selected = radio; break;
        }
        CloseHandle(radio);
        if (!BluetoothFindNextRadio(find, &radio)) break;
        if (count == 15) CloseHandle(radio);
    }
    BluetoothFindRadioClose(find);
    return selected;
}

static int ready(phone_socket *s) {
    if (s->cancel && WaitForSingleObject(s->cancel, 0) == WAIT_OBJECT_0) {
        s->stream->error = "cancelled"; return 0;
    }
    if (GetTickCount64() >= s->deadline) { s->stream->error = "timed_out"; return 0; }
    return 1;
}

static int socket_wait(phone_socket *s, int writing) {
    unsigned i;
    for (i = 0; i < 1201 && ready(s); ++i) {
        fd_set events, failed;
        struct timeval timeout;
        int result;
        FD_ZERO(&events); FD_SET(s->socket, &events);
        FD_ZERO(&failed); FD_SET(s->socket, &failed);
        timeout.tv_sec = 0; timeout.tv_usec = 50000;
        result = select(0, writing ? NULL : &events, writing ? &events : NULL, &failed, &timeout);
        if (result == SOCKET_ERROR || (result && FD_ISSET(s->socket, &failed))) {
            s->stream->error = "connection_failed"; return 0;
        }
        if (result && ready(s)) return 1;
    }
    if (!s->stream->error) s->stream->error = "timed_out";
    return 0;
}

static size_t socket_read(void *context, void *bytes, size_t count) {
    phone_socket *s = (phone_socket *)context;
    int result;
    if (!socket_wait(s, 0)) return 0;
    result = recv(s->socket, (char *)bytes, (int)count, 0);
    if (result <= 0) { s->stream->error = "connection_closed"; return 0; }
    return (size_t)result;
}

static size_t socket_write(void *context, const void *bytes, size_t count) {
    phone_socket *s = (phone_socket *)context;
    int result;
    if (!socket_wait(s, 1)) return 0;
    result = send(s->socket, (const char *)bytes, (int)count, 0);
    if (result <= 0) { s->stream->error = "connection_closed"; return 0; }
    return (size_t)result;
}

static int socket_pause(void *context, unsigned ms) {
    phone_socket *s = (phone_socket *)context;
    unsigned elapsed;
    for (elapsed = 0; elapsed < ms && ready(s); elapsed += 50) {
        unsigned step = ms - elapsed < 50 ? ms - elapsed : 50;
        if (s->cancel) (void)WaitForSingleObject(s->cancel, step);
        else Sleep(step);
    }
    return ready(s);
}

static void adapters(phone_socket *s, cdx_json_output *out) {
    BLUETOOTH_FIND_RADIO_PARAMS params = {sizeof(params)};
    HBLUETOOTH_RADIO_FIND find;
    HANDLE radio = NULL;
    cdx_json_output entries;
    unsigned count = 0;
    int more = 0;
    cdx_json_output_init(&entries);
    find = BluetoothFindFirstRadio(&params, &radio);
    if (find) {
        for (count = 0; count < 16; ++count) {
            BLUETOOTH_RADIO_INFO info = {0};
            char name[1024];
            int size;
            info.dwSize = sizeof(info);
            if (!ready(s) || BluetoothGetRadioInfo(radio, &info)) { CloseHandle(radio); break; }
            size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, info.szName, -1, name, sizeof(name), NULL, NULL);
            if (!size) { s->stream->error = "encoding_error"; CloseHandle(radio); break; }
            if (count) cdx_json_raw(&entries, ",");
            cdx_json_raw(&entries, "{\"radio_address\":"); quote_address(&entries, info.address.ullLong);
            cdx_json_raw(&entries, ",\"name\":"); cdx_json_quote(&entries, name, (size_t)size - 1);
            cdx_json_raw(&entries, BluetoothIsConnectable(radio) ? ",\"connectable\":true" : ",\"connectable\":false");
            cdx_json_raw(&entries, BluetoothIsDiscoverable(radio) ? ",\"discoverable\":true}" : ",\"discoverable\":false}");
            CloseHandle(radio);
            if (!BluetoothFindNextRadio(find, &radio)) break;
            if (count == 15) { more = 1; CloseHandle(radio); }
        }
        BluetoothFindRadioClose(find);
    }
    if (s->stream->error) cdx_phone_error(out, s->stream->error, 0);
    else if (entries.failed) cdx_phone_error(out, "output_limit", 0);
    else {
        cdx_json_raw(out, more ? "{\"status\":\"output_limit\",\"truncated\":true,\"adapters\":[" : "{\"status\":\"ok\",\"truncated\":false,\"adapters\":[");
        if (entries.size) cdx_json_rawn(out, entries.data, entries.size);
        cdx_json_raw(out, "]}");
    }
    cdx_json_output_free(&entries);
}

static void devices(const cdx_phone_request *r, phone_socket *s, cdx_json_output *out) {
    BLUETOOTH_DEVICE_SEARCH_PARAMS search;
    BLUETOOTH_DEVICE_INFO device;
    HBLUETOOTH_DEVICE_FIND find;
    cdx_json_output entries;
    unsigned count = 0;
    DWORD error = ERROR_SUCCESS;
    int more = 0;
    HANDLE radio = r->radio_address ? open_radio(r->radio_address) : NULL;
    if (r->radio_address && !radio) { cdx_phone_error(out, "adapter_not_found", 0); return; }
    memset(&search, 0, sizeof(search)); memset(&device, 0, sizeof(device));
    search.dwSize = sizeof(search); device.dwSize = sizeof(device);
    search.fReturnAuthenticated = TRUE; search.fReturnRemembered = TRUE; search.fReturnConnected = TRUE;
    search.hRadio = radio;
    if (r->action == CDX_PHONE_DISCOVER) {
        search.fReturnUnknown = TRUE; search.fIssueInquiry = TRUE;
        search.cTimeoutMultiplier = (UCHAR)((r->timeout_ms - 500u) / 1280u);
        if (search.cTimeoutMultiplier > 8) search.cTimeoutMultiplier = 8;
        if (!search.cTimeoutMultiplier) search.cTimeoutMultiplier = 1;
    }
    cdx_json_output_init(&entries);
    find = ready(s) ? BluetoothFindFirstDevice(&search, &device) : NULL;
    if (!find && !s->stream->error) error = GetLastError();
    if (find) {
        do {
            char name[1024], address[13];
            int size;
            if (!ready(s)) break;
            if (count == r->max_entries) { more = 1; break; }
            size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, device.szName, -1, name, sizeof(name), NULL, NULL);
            if (!size) { s->stream->error = "encoding_error"; break; }
            (void)snprintf(address, sizeof(address), "%012llX", (unsigned long long)device.Address.ullLong);
            if (count++) cdx_json_raw(&entries, ",");
            cdx_json_raw(&entries, "{\"device_address\":"); cdx_json_quote(&entries, address, 12);
            cdx_json_raw(&entries, ",\"name\":"); cdx_json_quote(&entries, name, (size_t)size - 1u);
            cdx_json_raw(&entries, device.fAuthenticated ? ",\"paired\":true" : ",\"paired\":false");
            cdx_json_raw(&entries, device.fConnected ? ",\"connected\":true}" : ",\"connected\":false}");
            if (!BluetoothFindNextDevice(find, &device)) { error = GetLastError(); break; }
        } while (count <= r->max_entries);
        (void)BluetoothFindDeviceClose(find);
    }
    if (s->stream->error) cdx_phone_error(out, s->stream->error, 0);
    else if (error && error != ERROR_NO_MORE_ITEMS) {
        cdx_json_raw(out, "{\"status\":\"bluetooth_unavailable\",\"win32_error\":");
        cdx_json_uint(out, error); cdx_json_raw(out, ",\"submitted\":false}");
    }
    else if (entries.failed) cdx_phone_error(out, "output_limit", 0);
    else {
        cdx_json_raw(out, more ? "{\"status\":\"output_limit\",\"truncated\":true" : "{\"status\":\"ok\",\"truncated\":false");
        cdx_json_raw(out, r->action == CDX_PHONE_DISCOVER ? ",\"cached_devices\":false,\"inquiry_performed\":true,\"devices\":[" :
            ",\"cached_devices\":true,\"inquiry_performed\":false,\"devices\":[");
        if (entries.size) cdx_json_rawn(out, entries.data, entries.size);
        cdx_json_raw(out, "]}");
    }
    cdx_json_output_free(&entries);
    if (radio) CloseHandle(radio);
}

static void transport_init(phone_socket *s, cdx_phone_stream *stream, cdx_cancel_handle cancel, unsigned timeout_ms) {
    memset(stream, 0, sizeof(*stream)); memset(s, 0, sizeof(*s));
    stream->context = s; stream->read = socket_read; stream->write = socket_write; stream->pause = socket_pause;
    s->socket = INVALID_SOCKET; s->deadline = GetTickCount64() + timeout_ms; s->cancel = cancel; s->stream = stream;
}

static int connect_socket(phone_socket *s, uint64_t address, uint64_t radio_address, unsigned service) {
    SOCKADDR_BTH peer, local;
    u_long nonblocking = 1;
    int result, authenticated = TRUE, error = 0, size = sizeof(error);
    s->socket = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (s->socket == INVALID_SOCKET) { s->stream->error = "bluetooth_unavailable"; return 0; }
    if (ioctlsocket(s->socket, FIONBIO, &nonblocking) ||
        setsockopt(s->socket, SOL_RFCOMM, SO_BTH_AUTHENTICATE, (const char *)&authenticated, sizeof(authenticated))) {
        s->stream->error = "connection_failed"; return 0;
    }
    if (radio_address) {
        memset(&local, 0, sizeof(local)); local.addressFamily = AF_BTH;
        local.btAddr = radio_address; local.port = BT_PORT_ANY;
        if (bind(s->socket, (struct sockaddr *)&local, sizeof(local))) { s->stream->error = "adapter_unavailable"; return 0; }
    }
    memset(&peer, 0, sizeof(peer)); peer.addressFamily = AF_BTH; peer.btAddr = address;
    peer.serviceClassId.Data1 = service; peer.serviceClassId.Data3 = 0x1000;
    peer.serviceClassId.Data4[0] = 0x80; peer.serviceClassId.Data4[3] = 0x80;
    peer.serviceClassId.Data4[4] = 0x5F; peer.serviceClassId.Data4[5] = 0x9B; peer.serviceClassId.Data4[6] = 0x34; peer.serviceClassId.Data4[7] = 0xFB;
    /* A zero channel with serviceClassId asks Windows to resolve RFCOMM via SDP. */
    result = connect(s->socket, (const struct sockaddr *)&peer, sizeof(peer));
    if (result == SOCKET_ERROR) {
        error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK || !socket_wait(s, 1)) {
            if (!s->stream->error) s->stream->error = error == WSAEACCES ? "permission_denied" : "connection_failed";
            return 0;
        }
    }
    if (getsockopt(s->socket, SOL_SOCKET, SO_ERROR, (char *)&error, &size) || error) {
        s->stream->error = "connection_failed"; return 0;
    }
    return ready(s);
}

DWORD cdx_phone_authenticate(const cdx_phone_request *, const wchar_t *, int *, cdx_json_output *);

void cdx_phone_platform_helper(const cdx_phone_request *r, cdx_json_output *out) {
    phone_socket s;
    cdx_phone_stream stream;
    BLUETOOTH_DEVICE_INFO device = {0};
    HANDLE radio;
    DWORD error, observed;
    cdx_json_output diagnostics;
    int submitted;
    transport_init(&s, &stream, NULL, r->timeout_ms);
    if (r->action == CDX_PHONE_DISCOVER) { devices(r, &s, out); return; }
    if (r->action == CDX_PHONE_UNPAIR) {
        int removed;
        device.dwSize = sizeof(device); device.Address.ullLong = r->address;
        error = BluetoothRemoveDevice(&device.Address);
        observed = BluetoothGetDeviceInfo(NULL, &device);
        removed = observed == ERROR_NOT_FOUND || (observed == ERROR_SUCCESS && !device.fAuthenticated);
        if ((!error || error == ERROR_NOT_FOUND) && removed)
            cdx_json_raw(out, "{\"status\":\"ok\",\"paired\":false,\"removed\":true,\"submitted\":true,\"auto_retry\":false}");
        else {
            cdx_json_raw(out, error ? "{\"status\":\"unpair_failed\",\"win32_error\":" : "{\"status\":\"unknown\",\"win32_error\":");
            cdx_json_uint(out, error ? error : observed); cdx_json_raw(out, ",\"submitted\":true,\"auto_retry\":false}");
        }
        return;
    }
    radio = open_radio(r->radio_address);
    if (!radio) { cdx_phone_error(out, "adapter_not_found", 0); return; }
    device.dwSize = sizeof(device); device.Address.ullLong = r->address;
    (void)BluetoothGetDeviceInfo(radio, &device);
    if (device.fAuthenticated) {
        cdx_json_raw(out, "{\"status\":\"ok\",\"paired\":true,\"already_paired\":true,\"submitted\":false}");
        CloseHandle(radio); return;
    }
    /* Explicit human confirmation is displayed above the owner's window. */
    if (r->parent_window) {
        ShowWindow((HWND)r->parent_window, SW_RESTORE);
        SetForegroundWindow((HWND)r->parent_window);
    }
    cdx_json_output_init(&diagnostics);
    error = cdx_phone_authenticate(r, device.szName, &submitted, &diagnostics);
    device.fAuthenticated = FALSE;
    observed = BluetoothGetDeviceInfo(radio, &device);
    if ((!error || error == ERROR_NO_MORE_ITEMS) && observed == ERROR_SUCCESS && device.fAuthenticated)
        cdx_json_raw(out, "{\"status\":\"ok\",\"paired\":true,\"submitted\":true,\"auto_retry\":false");
    else {
        const char *status = error == ERROR_CANCELLED ? "cancelled" :
            !error || error == (DWORD)HRESULT_FROM_WIN32(ERROR_TIMEOUT) ||
            (submitted && error == (DWORD)HRESULT_FROM_WIN32(ERROR_CANCELLED)) ? "unknown" : "pairing_failed";
        cdx_json_raw(out, "{\"status\":");
        cdx_json_quote(out, status, strlen(status));
        cdx_json_raw(out, ",\"win32_error\":"); cdx_json_uint(out, error);
        cdx_json_raw(out, ",\"paired\":");
        cdx_json_raw(out, observed == ERROR_SUCCESS ? device.fAuthenticated ? "true" : "false" :
            observed == ERROR_NOT_FOUND ? "false" : "null");
        cdx_json_raw(out, submitted ? ",\"submitted\":true,\"auto_retry\":false" : ",\"submitted\":false,\"auto_retry\":false");
    }
    cdx_json_raw(out, ",\"readback_error\":"); cdx_json_uint(out, observed);
    if (diagnostics.size) cdx_json_rawn(out, diagnostics.data, diagnostics.size);
    cdx_json_raw(out, "}");
    cdx_json_output_free(&diagnostics);
    CloseHandle(radio);
}

static void run_helper(cdx_phone_session *session, const cdx_phone_request *r, cdx_cancel_handle cancel, cdx_json_output *out) {
    wchar_t *paths = calloc(65536, sizeof(wchar_t)), *executable, *directory, *slash;
    const wchar_t *args[] = {L"--phone-helper"};
    cdx_exec_request request;
    cdx_exec_result result;
    cdx_json_output input;
    cdx_json parsed;
    DWORD length;
    int mutation = r->action == CDX_PHONE_PAIR || r->action == CDX_PHONE_UNPAIR;
    if (!paths) { cdx_phone_error(out, "out_of_memory", 0); return; }
    executable = paths; directory = paths + 32768;
    length = GetModuleFileNameW(NULL, executable, 32768);
    if (!length || length >= 32768) { free(paths); cdx_phone_error(out, "helper_unavailable", 0); return; }
    executable[length] = 0;
    memcpy(directory, executable, ((size_t)length + 1) * sizeof(wchar_t));
    directory[length] = 0;
    slash = wcsrchr(directory, L'\\');
    if (!slash) { free(paths); cdx_phone_error(out, "helper_unavailable", 0); return; }
    slash[1] = 0;
    cdx_json_output_init(&input); memset(&request, 0, sizeof(request));
    cdx_json_raw(&input, r->action == CDX_PHONE_PAIR ? "{\"action\":\"pair\",\"device_address\":" :
        r->action == CDX_PHONE_UNPAIR ? "{\"action\":\"unpair\",\"device_address\":" : "{\"action\":\"discover\",\"device_address\":");
    quote_address(&input, r->address);
    if (r->radio_address) { cdx_json_raw(&input, ",\"radio_address\":"); quote_address(&input, r->radio_address); }
    if (session && session->parent_window) {
        cdx_json_raw(&input, ",\"parent_window\":"); cdx_json_uint(&input, (uintptr_t)session->parent_window);
    }
    cdx_json_raw(&input, ",\"dry_run\":false,\"timeout_ms\":"); cdx_json_uint(&input, r->timeout_ms);
    cdx_json_raw(&input, ",\"max_entries\":"); cdx_json_uint(&input, r->max_entries); cdx_json_raw(&input, "}");
    request.program = executable; request.cwd = directory; request.args = args; request.arg_count = 1;
    request.stdin_utf8 = (const unsigned char *)input.data; request.stdin_size = input.size;
    request.timeout_ms = r->timeout_ms; request.output_limit_bytes = 65536; request.cancel_event = cancel;
    cdx_result_init(&result); memset(&parsed, 0, sizeof(parsed));
    if (!input.failed) (void)cdx_execute(&request, &result);
    if (result.status == CDX_COMPLETED && result.exit_code_known && result.exit_code == 0 &&
        cdx_json_parse(&parsed, (const char *)result.stdout_bytes.data, result.stdout_bytes.size)) {
        if (parsed.nodes[0].type == CDX_JSON_OBJECT) cdx_json_rawn(out, (const char *)result.stdout_bytes.data, result.stdout_bytes.size);
        else cdx_phone_error(out, "helper_failed", mutation);
    } else cdx_phone_error(out, result.status == CDX_CANCELLED ? "cancelled" :
        result.status == CDX_TIMED_OUT ? "timed_out" : "helper_failed", mutation && result.pid != 0);
    cdx_json_free(&parsed); cdx_result_free(&result); cdx_json_output_free(&input); free(paths);
}

static void disconnect_session(cdx_phone_session *session, const char *reason) {
    if (session->transport.socket != INVALID_SOCKET) closesocket(session->transport.socket);
    session->transport.socket = INVALID_SOCKET; session->hfp.initialized = 0;
    if (session->winsock) WSACleanup();
    session->winsock = 0; session->last_error = reason;
}

cdx_phone_session *cdx_phone_session_create(void) {
    cdx_phone_session *session = calloc(1, sizeof(*session));
    if (session) transport_init(&session->transport, &session->stream, NULL, 15000);
    return session;
}

void cdx_phone_session_destroy(cdx_phone_session *session) {
    if (session) { disconnect_session(session, NULL); free(session); }
}

void cdx_phone_session_set_window(cdx_phone_session *session, uintptr_t window) {
    if (session) session->parent_window = (HWND)window;
}

#ifdef CDX_PHONE_TEST
int cdx_phone_test_attach(cdx_phone_session *session, uintptr_t socket_value, unsigned ttl) {
    cdx_phone_request request = {0};
    cdx_json_output out;
    u_long nonblocking = 1;
    session->transport.socket = (SOCKET)socket_value;
    if (ioctlsocket(session->transport.socket, FIONBIO, &nonblocking)) return 0;
    session->stream.retain_connection = 1;
    session->transport.deadline = GetTickCount64() + 5000;
    session->address = 0x001122334455ull; request.action = CDX_PHONE_CALL_STATUS;
    cdx_json_output_init(&out);
    cdx_phone_protocol_connected(&request, &session->stream, &session->hfp, &out);
    cdx_json_output_free(&out);
    session->checked_at = session->connected_at = GetTickCount64(); session->expires = session->connected_at + ttl;
    return session->hfp.initialized && !session->stream.error;
}
#endif

static const char *session_call_state(const cdx_phone_session *session) {
    if (session->transport.socket == INVALID_SOCKET) return "unknown";
    return session->hfp.setup == 1 ? "incoming" : session->hfp.setup == 2 ? "dialing" :
        session->hfp.setup == 3 ? "alerting" : session->hfp.call ? "active" : "idle";
}

static void session_battery_json(const cdx_phone_session *session, cdx_json_output *out) {
    const cdx_phone_hfp *state = session ? &session->hfp : NULL;
    if (state && state->battery_index > 0 && state->battery_level >= 0) {
        unsigned percent = state->battery_level <= 5 ? (unsigned)state->battery_level * 20u : (unsigned)state->battery_level;
        cdx_json_raw(out, ",\"battery_supported\":true,\"battery_level\":");
        cdx_json_uint(out, (unsigned)state->battery_level);
        cdx_json_raw(out, ",\"battery_scale\":5,\"battery_percent\":");
        cdx_json_uint(out, percent > 100u ? 100u : percent);
    } else {
        cdx_json_raw(out, ",\"battery_supported\":false,\"battery_level\":null,\"battery_scale\":null,\"battery_percent\":null");
    }
}

static void connection_status(const cdx_phone_session *session, cdx_json_output *out) {
    ULONGLONG now = GetTickCount64();
    int connected = session && session->transport.socket != INVALID_SOCKET;
    const char *state = connected ? "connected" : session && session->last_error &&
        !strcmp(session->last_error, "lease_expired") ? "expired" : "disconnected";
    cdx_json_raw(out, "{\"status\":\"ok\",\"connection_state\":"); cdx_json_quote(out, state, strlen(state));
    cdx_json_raw(out, connected ? ",\"connection_retained\":true" : ",\"connection_retained\":false");
    cdx_json_raw(out, ",\"device_address\":"); quote_address(out, session ? session->address : 0);
    cdx_json_raw(out, ",\"radio_address\":"); quote_address(out, session ? session->radio_address : 0);
    cdx_json_raw(out, ",\"phone_name\":"); cdx_json_quote(out, session ? session->name : "", session ? strlen(session->name) : 0);
    cdx_json_raw(out, ",\"call_state\":"); state = session ? session_call_state(session) : "unknown"; cdx_json_quote(out, state, strlen(state));
    session_battery_json(session, out);
    cdx_json_raw(out, ",\"remaining_ms\":"); cdx_json_uint(out, connected && session->expires > now ? session->expires - now : 0);
    cdx_json_raw(out, ",\"connected_ms\":"); cdx_json_uint(out, connected ? now - session->connected_at : 0);
    cdx_json_raw(out, ",\"last_error\":");
    if (session && session->last_error) cdx_json_quote(out, session->last_error, strlen(session->last_error));
    else cdx_json_raw(out, "null");
    cdx_json_raw(out, ",\"owner\":\"current_process\",\"owner_pid\":"); cdx_json_uint(out, GetCurrentProcessId());
    cdx_json_raw(out, ",\"audio_available\":false,\"auto_reconnect\":false}");
}

static void session_tick_until(cdx_phone_session *session, cdx_cancel_handle cancel, ULONGLONG deadline) {
    cdx_phone_request request = {0};
    cdx_json_output result;
    ULONGLONG now = GetTickCount64();
    if (!session || session->transport.socket == INVALID_SOCKET) return;
    if (now >= session->expires) { disconnect_session(session, "lease_expired"); return; }
    if (now - session->checked_at < 2000) return;
    request.action = CDX_PHONE_CALL_STATUS;
    session->stream.error = NULL; session->stream.retain_connection = 1;
    session->transport.cancel = cancel; session->transport.deadline = now + 1500;
    if (session->transport.deadline > deadline) session->transport.deadline = deadline;
    if (session->transport.deadline > session->expires) session->transport.deadline = session->expires;
    cdx_json_output_init(&result);
    cdx_phone_protocol_connected(&request, &session->stream, &session->hfp, &result);
    session->checked_at = GetTickCount64();
    if (session->stream.error) disconnect_session(session, session->stream.error);
    cdx_json_output_free(&result);
}

void cdx_phone_session_tick(cdx_phone_session *session) {
    session_tick_until(session, NULL, GetTickCount64() + 1500);
}

void cdx_phone_platform(const cdx_phone_request *r, cdx_cancel_handle cancel, cdx_json_output *out) {
    cdx_phone_session_platform(NULL, r, cancel, out);
}

void cdx_phone_session_platform(cdx_phone_session *session, const cdx_phone_request *r,
                                cdx_cancel_handle cancel, cdx_json_output *out) {
    cdx_phone_stream stream;
    phone_socket s;
    BLUETOOTH_DEVICE_INFO device = {0};
    WSADATA data;
    int hfp = r->action >= CDX_PHONE_CALL_STATUS && r->action <= CDX_PHONE_WAIT_CALL;
    if (r->action == CDX_PHONE_APP_NOTIFICATIONS) {
        cdx_phone_app_notifications_platform(r, cancel, out);
        return;
    }
    transport_init(&s, &stream, cancel, r->timeout_ms);
    if (!ready(&s)) { cdx_phone_error(out, stream.error, 0); return; }
    if (session && session->transport.socket != INVALID_SOCKET && GetTickCount64() >= session->expires)
        disconnect_session(session, "lease_expired");
    if (r->action == CDX_PHONE_CONNECTION_STATUS) {
        session_tick_until(session, cancel, s.deadline);
        if (!ready(&s)) cdx_phone_error(out, stream.error, 0);
        else connection_status(session, out);
        return;
    }
    if (r->action == CDX_PHONE_ADAPTERS) { adapters(&s, out); return; }
    if (r->action == CDX_PHONE_DEVICES) { devices(r, &s, out); return; }
    if (r->action == CDX_PHONE_DISCOVER || r->action == CDX_PHONE_PAIR) { run_helper(session, r, cancel, out); return; }
    if (r->action == CDX_PHONE_UNPAIR) {
        if (session && session->address == r->address) disconnect_session(session, NULL);
        run_helper(session, r, cancel, out); return;
    }
    if (r->action == CDX_PHONE_DISCONNECT) {
        if (!session) { cdx_phone_error(out, "session_required", 0); return; }
        if (session->address && session->address != r->address) { cdx_phone_error(out, "different_device_connected", 0); return; }
        disconnect_session(session, NULL); connection_status(session, out); return;
    }
    if (r->action == CDX_PHONE_CONNECT && !session) { cdx_phone_error(out, "session_required", 0); return; }
    if (session && session->transport.socket != INVALID_SOCKET && (hfp || r->action == CDX_PHONE_CONNECT)) {
        if (session->address != r->address || (r->radio_address && session->radio_address != r->radio_address)) {
            cdx_phone_error(out, "different_device_connected", 0); return;
        }
        if (r->action == CDX_PHONE_CONNECT) {
            session->expires = GetTickCount64() + r->connection_ttl_ms; session_tick_until(session, cancel, s.deadline);
            if (!ready(&s)) cdx_phone_error(out, stream.error, 0);
            else connection_status(session, out);
            return;
        }
        session->stream.error = NULL; session->transport.cancel = cancel;
        session->transport.deadline = s.deadline < session->expires ? s.deadline : session->expires;
        cdx_phone_protocol_connected(r, &session->stream, &session->hfp, out);
        session->checked_at = GetTickCount64();
        if (session->stream.error) disconnect_session(session, session->stream.error);
        return;
    }
    device.dwSize = sizeof(device); device.Address.ullLong = r->address;
    if (BluetoothGetDeviceInfo(NULL, &device) != ERROR_SUCCESS || !device.fAuthenticated) {
        cdx_phone_error(out, "pairing_required", 0); return;
    }
    if (WSAStartup(MAKEWORD(2, 2), &data)) { cdx_phone_error(out, "bluetooth_unavailable", 0); return; }
    if (!connect_socket(&s, r->address, r->radio_address, hfp || r->action == CDX_PHONE_CONNECT ? 0x111Fu :
        r->action == CDX_PHONE_CONTACTS || r->action == CDX_PHONE_CALL_HISTORY ? 0x112Fu : 0x1132u)) goto done;
    if (r->action == CDX_PHONE_CONNECT) {
        cdx_phone_request probe = *r;
        cdx_json_output response;
        SOCKADDR_BTH local;
        int size = sizeof(local);
        disconnect_session(session, NULL);
        session->winsock = 1; session->transport = s; session->stream = stream;
        session->stream.context = &session->transport; session->transport.stream = &session->stream;
        session->stream.retain_connection = 1; session->address = r->address;
        session->radio_address = r->radio_address;
        if (!getsockname(s.socket, (struct sockaddr *)&local, &size)) session->radio_address = local.btAddr;
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, device.szName, -1, session->name, sizeof(session->name), NULL, NULL))
            session->name[0] = 0;
        probe.action = CDX_PHONE_CALL_STATUS; cdx_json_output_init(&response);
        cdx_phone_protocol_connected(&probe, &session->stream, &session->hfp, &response);
        if (session->stream.error || !session->hfp.initialized) {
            cdx_phone_error(out, session->stream.error ? session->stream.error : "protocol_error", 0);
            disconnect_session(session, session->stream.error ? session->stream.error : "protocol_error");
        } else {
            session->connected_at = session->checked_at = GetTickCount64();
            session->expires = session->connected_at + r->connection_ttl_ms;
            connection_status(session, out);
        }
        cdx_json_output_free(&response); return;
    }
    cdx_phone_protocol(r, &stream, out);
done:
    if (!out->size) cdx_phone_error(out, stream.error ? stream.error : "connection_failed", 0);
    if (s.socket != INVALID_SOCKET) closesocket(s.socket);
    WSACleanup();
}
