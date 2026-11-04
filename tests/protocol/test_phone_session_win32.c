#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include "cdx/phone.h"
#include <stdio.h>
#include <string.h>

int cdx_phone_test_attach(cdx_phone_session *, uintptr_t, unsigned);
HRESULT cdx_phone_test_notifications(const unsigned char *, unsigned, unsigned, unsigned,
                                    HANDLE, cdx_json_output *, unsigned *);
HRESULT cdx_phone_test_ancs_discovery(const HRESULT *, unsigned, unsigned, HANDLE, HANDLE, unsigned *, int *);
typedef struct peer_fixture {
    SOCKET server, client;
    HANDLE thread, stop, closed, query_received;
    LONG queries, handshakes, stall;
} peer_fixture;
static unsigned total, passed;

static void check(const char *name, int ok) {
    ++total; if (ok) ++passed;
    printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
}
static DWORD WINAPI peer_main(void *context) {
    peer_fixture *peer = context;
    char command[64];
    size_t used = 0;
    unsigned loops;
    ULONGLONG deadline = GetTickCount64() + 15000;
    for (loops = 0; loops < 10000 && GetTickCount64() < deadline && WaitForSingleObject(peer->stop, 0) != WAIT_OBJECT_0; ++loops) {
        fd_set reads;
        struct timeval timeout = {0, 50000};
        char byte;
        int result;
        const char *reply = "ERROR\r\n";
        FD_ZERO(&reads); FD_SET(peer->server, &reads);
        result = select(0, &reads, NULL, NULL, &timeout);
        if (result == SOCKET_ERROR) break;
        if (!result) continue;
        if (recv(peer->server, &byte, 1, 0) <= 0) break;
        if (byte != '\r') {
            if (used + 1 >= sizeof(command)) break;
            command[used++] = byte; continue;
        }
        command[used] = 0; used = 0;
        if (!strcmp(command, "AT+BRSF=0")) { InterlockedIncrement(&peer->handshakes); reply = "+BRSF: 0\r\nOK\r\n"; }
        else if (!strcmp(command, "AT+CIND=?")) reply = "+CIND: (\"callsetup\",(0-3)),(\"call\",(0,1))\r\nOK\r\n";
        else if (!strcmp(command, "AT+CIND?")) {
            LONG queries = InterlockedIncrement(&peer->queries);
            SetEvent(peer->query_received);
            if (InterlockedCompareExchange(&peer->stall, 0, 0)) continue;
            reply = queries == 1 ? "+CIND: 0,0\r\nOK\r\n" : "+CIND: 1,0\r\nOK\r\n";
        } else if (!strcmp(command, "AT+CMER=3,0,0,1")) reply = "OK\r\n";
        if (send(peer->server, reply, (int)strlen(reply), 0) != (int)strlen(reply)) break;
    }
    closesocket(peer->server); SetEvent(peer->closed); return 0;
}
static int peer_start(peer_fixture *peer) {
    SOCKET listener;
    struct sockaddr_in address = {0};
    int length = sizeof(address);
    memset(peer, 0, sizeof(*peer)); peer->client = peer->server = INVALID_SOCKET;
    peer->stop = CreateEventW(NULL, TRUE, FALSE, NULL); peer->closed = CreateEventW(NULL, TRUE, FALSE, NULL);
    peer->query_received = CreateEventW(NULL, TRUE, FALSE, NULL);
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (!peer->stop || !peer->closed || !peer->query_received || listener == INVALID_SOCKET) return 0;
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 1) ||
        getsockname(listener, (struct sockaddr *)&address, &length)) { closesocket(listener); return 0; }
    peer->client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connect(peer->client, (struct sockaddr *)&address, sizeof(address))) { closesocket(listener); return 0; }
    peer->server = accept(listener, NULL, NULL); closesocket(listener);
    if (peer->server == INVALID_SOCKET) return 0;
    peer->thread = CreateThread(NULL, 0, peer_main, peer, 0, NULL);
    return peer->thread != NULL;
}
static void peer_finish(peer_fixture *peer) {
    SetEvent(peer->stop);
    if (peer->thread) {
        if (WaitForSingleObject(peer->thread, 3000) != WAIT_OBJECT_0) ExitProcess(2);
        CloseHandle(peer->thread);
    }
    CloseHandle(peer->stop); CloseHandle(peer->closed); CloseHandle(peer->query_received);
}
static int call(cdx_phone_session *session, const char *input, HANDLE cancel, const char *key, const char *value) {
    cdx_json request, response;
    cdx_json_output out;
    int ok, n;
    memset(&response, 0, sizeof(response)); cdx_json_output_init(&out);
    ok = cdx_json_parse(&request, input, strlen(input));
    if (ok) cdx_phone_session_call(session, &request, 0, cancel, &out);
    ok = ok && cdx_json_parse(&response, out.data, out.size);
    n = ok ? cdx_json_get(&response, 0, key) : -1;
    ok = n >= 0 && response.nodes[n].type == CDX_JSON_STRING && !strcmp(response.nodes[n].string, value);
    cdx_json_free(&request); cdx_json_free(&response); cdx_json_output_free(&out); return ok;
}
typedef struct cancel_fixture { HANDLE received, cancel; } cancel_fixture;
static DWORD WINAPI cancel_query(void *context) {
    cancel_fixture *fixture = context;
    if (WaitForSingleObject(fixture->received, 3000) == WAIT_OBJECT_0) SetEvent(fixture->cancel);
    return 0;
}

static void notification_case(const char *name, const unsigned char *bytes, unsigned size, unsigned limit,
                              HANDLE cancel, HRESULT expected, unsigned expected_count,
                              unsigned first_uid, unsigned second_uid, int first_preexisting) {
    cdx_json_output out;
    cdx_json json = {0};
    unsigned count = 0, rows = 0;
    int ok, n;
    ULONGLONG start = GetTickCount64();
    HRESULT hr;
    cdx_json_output_init(&out);
    hr = cdx_phone_test_notifications(bytes, size, limit, 50, cancel, &out, &count);
    ok = hr == expected && count == expected_count && GetTickCount64() - start < 2000 &&
        cdx_json_parse(&json, out.data, out.size) && json.nodes[0].type == CDX_JSON_ARRAY;
    if (ok) {
        for (n = json.nodes[0].first; n >= 0 && rows < 3; n = json.nodes[n].next) {
            int uid = cdx_json_get(&json, n, "uid"), event = cdx_json_get(&json, n, "event_id");
            int preexisting = cdx_json_get(&json, n, "preexisting");
            ok = ok && uid >= 0 && json.nodes[uid].number == (rows ? second_uid : first_uid) &&
                event >= 0 && json.nodes[event].number == 2 && preexisting >= 0 &&
                json.nodes[preexisting].boolean == (rows == 0 ? first_preexisting : 0) && cdx_json_get(&json, n, "message") < 0;
            ++rows;
        }
        ok = ok && rows == expected_count && n < 0;
    }
    check(name, ok);
    cdx_json_free(&json); cdx_json_output_free(&out);
}

static void notification_tests(void) {
    unsigned char events[] = {2,4,1,2,11,0,0,0, 2,0,1,1,22,0,0,0};
    unsigned char backlog[520 * 8];
    unsigned index;
    HANDLE cancel = CreateEventW(NULL, TRUE, TRUE, NULL);
    memset(backlog, 0, sizeof(backlog));
    for (index = 0; index < 520; ++index) {
        backlog[index * 8] = 2; backlog[index * 8 + 1] = 4; backlog[index * 8 + 2] = 1;
        backlog[index * 8 + 4] = (unsigned char)index;
    }
    backlog[519 * 8 + 1] = 0; backlog[519 * 8 + 4] = 0xcd; backlog[519 * 8 + 5] = 0xab;
    notification_case("ANCS drains two queued events without stopping at the first", events, sizeof(events), 2, NULL, S_OK, 2, 11, 22, 1);
    notification_case("ANCS reserves an output slot for a fresh event", events, sizeof(events), 1, NULL, S_OK, 1, 22, 0, 0);
    notification_case("ANCS retains queued events when the listening deadline expires", events, sizeof(events), 3, NULL, S_OK, 2, 11, 22, 1);
    notification_case("ANCS empty listening window completes with an empty array", events, 0, 2, NULL, S_OK, 0, 0, 0, 0);
    if (!cancel) { check("ANCS cancellation fixture created", 0); return; }
    notification_case("ANCS cancellation interrupts queued delivery", events, sizeof(events), 2, cancel,
                      HRESULT_FROM_WIN32(ERROR_CANCELLED), 0, 0, 0, 0);
    CloseHandle(cancel);
    events[8] = 3;
    notification_case("ANCS malformed later event preserves the completed first event", events, sizeof(events), 3, NULL,
                      HRESULT_FROM_WIN32(ERROR_INVALID_DATA), 1, 11, 0, 1);
    events[8] = 2;
    notification_case("ANCS truncated later event preserves the completed first event", events, sizeof(events) - 1, 3, NULL,
                      HRESULT_FROM_WIN32(ERROR_INVALID_DATA), 1, 11, 0, 1);
    notification_case("ANCS bounds a large preexisting backlog and keeps a fresh event", backlog, sizeof(backlog), 2, NULL,
                      S_OK, 2, 8, 0xabcd, 1);
}

static void discovery_case(const char *name, const HRESULT *results, unsigned count, unsigned timeout_ms,
                           HANDLE cancel, HANDLE attempted, HRESULT expected, unsigned expected_attempts,
                           int expected_exhausted) {
    unsigned attempts = 0;
    int exhausted = 0;
    ULONGLONG start = GetTickCount64();
    HRESULT hr = cdx_phone_test_ancs_discovery(results, count, timeout_ms, cancel, attempted, &attempts, &exhausted);
    check(name, hr == expected && attempts == expected_attempts && exhausted == expected_exhausted &&
          GetTickCount64() - start < 6000);
}

static DWORD WINAPI cancel_discovery_backoff(void *context) {
    cancel_fixture *fixture = context;
    if (WaitForSingleObject(fixture->received, 3000) == WAIT_OBJECT_0 &&
        WaitForSingleObject(fixture->cancel, 50) == WAIT_TIMEOUT) SetEvent(fixture->cancel);
    return 0;
}

static void discovery_tests(void) {
    const HRESULT success[] = {S_OK};
    const HRESULT transient[] = {HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED), S_OK};
    const HRESULT unreachable[] = {
        HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED), HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED),
        HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED), HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED),
        HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED), HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED)};
    const HRESULT sixth[] = {
        HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED), HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED),
        HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED), HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED),
        HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED), S_OK};
    const HRESULT denied[] = {E_ACCESSDENIED};
    cancel_fixture cancellation;
    HANDLE thread;
    discovery_case("ANCS initial discovery succeeds without retry", success, 1, 5000,
                   NULL, NULL, S_OK, 1, 0);
    discovery_case("ANCS unreachable discovery retries and succeeds", transient, 2, 5000,
                   NULL, NULL, S_OK, 2, 0);
    discovery_case("ANCS discovery stops after five retries", unreachable, 6, 5000,
                   NULL, NULL, HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED), 6, 1);
    discovery_case("ANCS discovery can succeed on the sixth attempt", sixth, 6, 5000,
                   NULL, NULL, S_OK, 6, 0);
    discovery_case("ANCS access denial is not retried", denied, 1, 5000,
                   NULL, NULL, E_ACCESSDENIED, 1, 0);
    discovery_case("ANCS original deadline stops discovery backoff", unreachable, 6, 60,
                   NULL, NULL, HRESULT_FROM_WIN32(ERROR_TIMEOUT), 1, 0);
    discovery_case("ANCS expired deadline prevents discovery", success, 1, 0,
                   NULL, NULL, HRESULT_FROM_WIN32(ERROR_TIMEOUT), 0, 0);
    cancellation.received = CreateEventW(NULL, TRUE, FALSE, NULL);
    cancellation.cancel = CreateEventW(NULL, TRUE, TRUE, NULL);
    if (!cancellation.received || !cancellation.cancel) {
        check("ANCS discovery cancellation fixtures created", 0);
        if (cancellation.received) CloseHandle(cancellation.received);
        if (cancellation.cancel) CloseHandle(cancellation.cancel);
        return;
    }
    discovery_case("ANCS cancellation prevents the first discovery", success, 1, 5000,
                   cancellation.cancel, NULL, HRESULT_FROM_WIN32(ERROR_CANCELLED), 0, 0);
    ResetEvent(cancellation.cancel);
    thread = CreateThread(NULL, 0, cancel_discovery_backoff, &cancellation, 0, NULL);
    if (thread) {
        discovery_case("ANCS cancellation interrupts discovery backoff", unreachable, 6, 5000,
                       cancellation.cancel, cancellation.received, HRESULT_FROM_WIN32(ERROR_CANCELLED), 1, 0);
        if (WaitForSingleObject(thread, 3000) != WAIT_OBJECT_0) ExitProcess(2);
        CloseHandle(thread);
    } else check("ANCS discovery cancellation thread created", 0);
    CloseHandle(cancellation.received); CloseHandle(cancellation.cancel);
}

int main(void) {
    WSADATA data;
    unsigned scenario;
    ULONGLONG deadline = GetTickCount64() + 30000;
    if (WSAStartup(MAKEWORD(2,2), &data)) return 2;
    for (scenario = 0; scenario < 5 && GetTickCount64() < deadline; ++scenario) {
        peer_fixture peer = {0};
        cdx_phone_session *session = cdx_phone_session_create();
        int ok = session && peer_start(&peer);
        if (!ok) { cdx_phone_session_destroy(session); WSACleanup(); return 2; }
        ok = cdx_phone_test_attach(session, (uintptr_t)peer.client, scenario == 1 ? 1000 : 10000);
        check("real socket handshake establishes retained session", ok);
        if (scenario == 0) {
            check("session status retains the socket between calls", call(session, "{\"action\":\"connection_status\"}", NULL, "connection_state", "connected"));
            check("next call observes peer state without another handshake", call(session, "{\"action\":\"call_status\",\"device_address\":\"001122334455\"}", NULL, "call_state", "incoming") && InterlockedCompareExchange(&peer.handshakes,0,0) == 1);
            check("different device cannot close the owned socket", call(session, "{\"action\":\"disconnect\",\"device_address\":\"001122334466\",\"dry_run\":false}", NULL, "status", "different_device_connected"));
            check("explicit disconnect reports disconnected", call(session, "{\"action\":\"disconnect\",\"device_address\":\"001122334455\",\"dry_run\":false}", NULL, "connection_state", "disconnected"));
            check("explicit disconnect reaches peer EOF", WaitForSingleObject(peer.closed, 1000) == WAIT_OBJECT_0);
        } else if (scenario == 1) {
            (void)WaitForSingleObject(peer.stop, 1050);
            cdx_phone_session_tick(session);
            check("lease expiry closes the socket without another request", WaitForSingleObject(peer.closed, 1000) == WAIT_OBJECT_0 && call(session, "{\"action\":\"connection_status\"}", NULL, "connection_state", "expired"));
        } else if (scenario == 2) {
            cancel_fixture cancellation;
            HANDLE thread;
            ResetEvent(peer.query_received); InterlockedExchange(&peer.stall, 1);
            cancellation.received = peer.query_received; cancellation.cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
            thread = CreateThread(NULL, 0, cancel_query, &cancellation, 0, NULL);
            check("active cancellation closes an unsynchronized HFP stream", thread && call(session, "{\"action\":\"call_status\",\"device_address\":\"001122334455\",\"timeout_ms\":3000}", cancellation.cancel, "status", "cancelled") && WaitForSingleObject(peer.closed, 1000) == WAIT_OBJECT_0);
            if (thread) { WaitForSingleObject(thread, 3000); CloseHandle(thread); }
            CloseHandle(cancellation.cancel);
        } else if (scenario == 3) {
            SetEvent(peer.stop); WaitForSingleObject(peer.closed, 1000);
            check("remote closure clears retained connection", call(session, "{\"action\":\"call_status\",\"device_address\":\"001122334455\"}", NULL, "status", "connection_closed") && call(session, "{\"action\":\"connection_status\"}", NULL, "connection_state", "disconnected"));
        } else {
            InterlockedExchange(&peer.stall, 1);
            (void)WaitForSingleObject(peer.stop, 2050);
            check("status probe honors the caller deadline", call(session, "{\"action\":\"connection_status\",\"timeout_ms\":40}", NULL, "status", "timed_out") && WaitForSingleObject(peer.closed, 1000) == WAIT_OBJECT_0);
        }
        cdx_phone_session_destroy(session); peer_finish(&peer);
    }
    {
        peer_fixture peer = {0};
        cdx_phone_session *session = cdx_phone_session_create();
        if (!session || !peer_start(&peer)) return 2;
        (void)cdx_phone_test_attach(session, (uintptr_t)peer.client, 10000);
        cdx_phone_session_destroy(session);
        check("owner destruction reaches peer EOF", WaitForSingleObject(peer.closed, 1000) == WAIT_OBJECT_0);
        peer_finish(&peer);
    }
    WSACleanup();
    notification_tests();
    discovery_tests();
    printf("%u/%u phone session checks passed\n", passed, total);
    return passed == total ? 0 : 1;
}
