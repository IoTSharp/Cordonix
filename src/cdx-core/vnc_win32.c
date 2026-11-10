#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <winsock2.h>
#include <ws2tcpip.h>
#include "cdx/vnc.h"
#include "cdx/filesystem.h"
#include "cdx/process.h"
#include "cdx/proxy.h"
#include <wincred.h>
#include <bcrypt.h>
#include <objidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define VNC_PASSWORD_CHARS 257u
#define VNC_MAX_WIRE (128u * 1024u * 1024u)
#define VNC_PROMPT_MAGIC 0x56445843u

typedef struct vnc_context {
    const cdx_vnc_request *request;
    cdx_vnc_result *result;
    SOCKET socket;
    uint64_t deadline;
    size_t wire_bytes;
    unsigned steps;
    wchar_t password[VNC_PASSWORD_CHARS], alias[320];
    int new_password;
} vnc_context;

typedef struct password_packet {
    DWORD magic, error;
    wchar_t password[VNC_PASSWORD_CHARS];
} password_packet;

static int stopped(vnc_context *context);
static int prepare(vnc_context *context, wchar_t **path);
static int handshake(vnc_context *context);
static HBITMAP framebuffer(vnc_context *context);
static void publish(vnc_context *context, HBITMAP bitmap, const wchar_t *path);
static int transfer(vnc_context *context, void *data, size_t size, int writing);
static int password(vnc_context *context);
static int challenge_response(vnc_context *context, unsigned char challenge[16]);
static void save_password(vnc_context *context);
static unsigned be16(const unsigned char *data) { return ((unsigned)data[0] << 8) | data[1]; }
static uint32_t be32(const unsigned char *data) { return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3]; }

void cdx_vnc_screenshot(const cdx_vnc_request *request, cdx_vnc_result *result) {
    vnc_context context = {0};
    cdx_proxy_request route = {0};
    cdx_proxy_result connection;
    WSADATA winsock;
    wchar_t *path = NULL;
    HBITMAP bitmap = NULL;
    uint64_t started = GetTickCount64();
    int initialized = 0;
    memset(result, 0, sizeof(*result));
    result->image.status = "validation_error"; result->image.cleanup_complete = 1;
    result->stage = "validation"; result->credential_status = "not_used";
    context.request = request; context.result = result; context.socket = INVALID_SOCKET;
    if (!prepare(&context, &path)) goto done;
    context.deadline = started + request->timeout_ms;
    if (stopped(&context)) goto done;
    result->stage = "connect";
    result->image.native_error = (uint32_t)WSAStartup(MAKEWORD(2, 2), &winsock);
    if (result->image.native_error) { result->image.status = "connection_error"; goto done; }
    initialized = 1;
    route.host = request->host; route.port = request->port; route.proxy = request->proxy;
    route.deadline_ms = context.deadline; route.cancellation = request->cancellation;
    cdx_proxy_connect(&route, &connection);
    result->image.cleanup_complete = connection.cleanup_complete;
    result->image.native_error = connection.native_error;
    if (connection.socket == UINTPTR_MAX) { result->image.status = connection.status; goto done; }
    context.socket = (SOCKET)connection.socket;
    if (!handshake(&context)) goto done;
    bitmap = framebuffer(&context);
    if (bitmap && !stopped(&context)) publish(&context, bitmap, path);
done:
    if (bitmap) (void)DeleteObject(bitmap);
    if (context.socket != INVALID_SOCKET && closesocket(context.socket)) result->image.cleanup_complete = 0;
    if (initialized) WSACleanup();
    SecureZeroMemory(context.password, sizeof(context.password));
    free(path);
    result->image.elapsed_ms = GetTickCount64() - started;
}

static int prepare(vnc_context *context, wchar_t **path) {
    const cdx_vnc_request *request = context->request;
    cdx_vnc_result *result = context->result;
    struct in6_addr address;
    char canonical[46];
    int family;
    size_t length;
    unsigned long error = 0;
    cdx_fs_status status;
    if (!request || !request->host || !request->password || !request->credential || !request->path ||
        !request->proxy || !request->port || request->port > 65535 || !request->timeout_ms ||
        request->timeout_ms > 300000 || strlen(request->host) > 45 || strlen(request->password) > 512 ||
        strlen(request->credential) > 256 || strpbrk(request->credential, "\r\n") ||
        (request->prompt_for_password != 0 && request->prompt_for_password != 1)) return 0;
    family = InetPtonA(AF_INET, request->host, &address) == 1 ? AF_INET : AF_INET6;
    if (InetPtonA(family, request->host, &address) != 1 || !InetNtopA(family, &address, canonical, sizeof(canonical))) return 0;
    if (*request->credential) (void)strcpy_s(result->credential, sizeof(result->credential), request->credential);
    else (void)snprintf(result->credential, sizeof(result->credential), "Cordonix/VNC/%s:%u", canonical, request->port);
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, result->credential, -1, context->alias, 320)) return 0;
    if (*request->password) {
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, request->password, -1, context->password, VNC_PASSWORD_CHARS)) return 0;
        for (size_t i = 0; i < VNC_PASSWORD_CHARS && context->password[i]; ++i)
            if (context->password[i] > 255) return 0;
        context->new_password = 1;
    }
    status = cdx_fs_normalize_path(request->path, request->cancellation, path, &error);
    if (status != CDX_FS_OK) { result->image.status = cdx_fs_status_name(status); result->image.native_error = (uint32_t)error; return 0; }
    length = wcslen(*path);
    if (length < 4 || _wcsicmp(*path + length - 4, L".png")) return 0;
    if (GetFileAttributesW(*path) != INVALID_FILE_ATTRIBUTES) { result->image.status = "already_exists"; return 0; }
    return 1;
}

static int stopped(vnc_context *context) {
    if (context->request->cancellation && WaitForSingleObject(context->request->cancellation, 0) == WAIT_OBJECT_0) {
        context->result->image.status = "cancelled"; return 1;
    }
    if (GetTickCount64() >= context->deadline) { context->result->image.status = "timed_out"; return 1; }
    if (++context->steps > 1000000u) { context->result->image.status = "output_limit"; return 1; }
    return 0;
}

static int transfer(vnc_context *context, void *data, size_t size, int writing) {
    size_t offset = 0;
    if (size > VNC_MAX_WIRE - context->wire_bytes) { context->result->image.status = "output_limit"; return 0; }
    for (unsigned i = 0; i < 100000u && offset < size; ++i) {
        fd_set ready;
        struct timeval timeout = {0, 10000};
        int count;
        if (stopped(context)) return 0;
        FD_ZERO(&ready); FD_SET(context->socket, &ready);
        count = select(0, writing ? NULL : &ready, writing ? &ready : NULL, NULL, &timeout);
        if (!count) continue;
        if (count > 0) count = writing ? send(context->socket, (const char *)data + offset, (int)(size - offset), 0) :
                                         recv(context->socket, (char *)data + offset, (int)(size - offset), 0);
        if (count == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
        if (count <= 0) {
            context->result->image.status = "connection_error";
            context->result->image.native_error = count ? (uint32_t)WSAGetLastError() : 0;
            return 0;
        }
        offset += (size_t)count; context->wire_bytes += (size_t)count;
    }
    if (offset != size) { context->result->image.status = "output_limit"; return 0; }
    return 1;
}

static int discard(vnc_context *context, uint32_t length) {
    unsigned char buffer[1024];
    if (length > 65536u) { context->result->image.status = "output_limit"; return 0; }
    for (unsigned i = 0; i < 64u && length; ++i) {
        size_t chunk = length < sizeof(buffer) ? length : sizeof(buffer);
        if (!transfer(context, buffer, chunk, 0)) return 0;
        length -= (uint32_t)chunk;
    }
    return length == 0;
}

static int handshake(vnc_context *context) {
    unsigned char data[256], selected = 0;
    int minor, has_none = 0, has_password = 0;
    cdx_vnc_result *result = context->result;
    result->stage = "handshake"; result->image.status = "protocol_error";
    if (!transfer(context, data, 12, 0)) return 0;
    if (memcmp(data, "RFB 003.", 8) || data[11] != '\n' || data[8] < '0' || data[8] > '9' ||
        data[9] < '0' || data[9] > '9' || data[10] < '0' || data[10] > '9') return 0;
    minor = !memcmp(data + 8, "008", 3) ? 8 : !memcmp(data + 8, "007", 3) ? 7 : 3;
    memcpy(data, minor == 8 ? "RFB 003.008\n" : minor == 7 ? "RFB 003.007\n" : "RFB 003.003\n", 12);
    if (!transfer(context, data, 12, 1)) return 0;
    if (minor == 3) {
        if (!transfer(context, data, 4, 0)) return 0;
        if (be32(data) == 1) selected = 1;
        else if (be32(data) == 2) selected = 2;
    } else {
        unsigned count;
        if (!transfer(context, data, 1, 0)) return 0;
        count = data[0];
        if (!count) return 0;
        if (!transfer(context, data, count, 0)) return 0;
        for (unsigned i = 0; i < count; ++i) { if (data[i] == 1) has_none = 1; if (data[i] == 2) has_password = 1; }
        selected = (unsigned char)(has_password && (*context->request->password || *context->request->credential) ? 2 : has_none ? 1 : has_password ? 2 : 0);
        if (selected && !transfer(context, &selected, 1, 1)) return 0;
    }
    result->security_type = selected;
    if (!selected) { result->image.status = "not_supported"; return 0; }
    if (selected == 2) {
        result->stage = "authentication";
        if (!password(context) || !transfer(context, data, 16, 0) || !challenge_response(context, data)) return 0;
        if (!transfer(context, data, 16, 1)) { SecureZeroMemory(data, sizeof(data)); return 0; }
        SecureZeroMemory(data, sizeof(data));
    }
    /* None omits SecurityResult in 3.3 and 3.7; VNC auth always sends it. */
    if (selected == 2 || minor == 8) {
        if (!transfer(context, data, 4, 0)) return 0;
        if (be32(data)) { result->image.status = "authentication_failed"; return 0; }
    }
    if (selected == 2 && context->new_password && !stopped(context)) save_password(context);
    SecureZeroMemory(context->password, sizeof(context->password));
    return !stopped(context);
}

static int prompt_password(vnc_context *context) {
    wchar_t *program = (wchar_t *)calloc(65536u, sizeof(wchar_t));
    wchar_t *cwd, *slash;
    const wchar_t *args[2] = {L"--vnc-password-prompt", context->alias};
    cdx_exec_request request = {0};
    cdx_exec_result process;
    password_packet packet = {0};
    int success = 0;
    DWORD length;
    if (!program) { context->result->image.status = "io_error"; return 0; }
    cwd = program + 32768;
    length = GetModuleFileNameW(NULL, program, 32768);
    if (!length || length >= 32768) goto done;
    (void)wcscpy_s(cwd, 32768, program); slash = wcsrchr(cwd, L'\\');
    if (!slash || stopped(context)) goto done;
    slash[1] = 0;
    request.program = program; request.cwd = cwd; request.args = args; request.arg_count = 2;
    {
        uint64_t now = GetTickCount64();
        if (now >= context->deadline) { context->result->image.status = "timed_out"; goto done; }
        request.timeout_ms = (uint32_t)(context->deadline - now);
    }
    request.cancel_event = context->request->cancellation; request.output_limit_bytes = sizeof(packet);
    cdx_execute(&request, &process);
    context->result->image.cleanup_complete = process.cleanup_complete;
    if (process.status == CDX_COMPLETED && process.stdout_bytes.size == sizeof(packet)) {
        memcpy(&packet, process.stdout_bytes.data, sizeof(packet));
        if (packet.magic == VNC_PROMPT_MAGIC && !packet.error && packet.password[VNC_PASSWORD_CHARS - 1] == 0) {
            memcpy(context->password, packet.password, sizeof(packet.password)); success = 1;
        } else {
            context->result->image.status = packet.error == ERROR_CANCELLED ? "cancelled" : "credential_error";
            context->result->credential_error = packet.error;
        }
    } else context->result->image.status = process.status == CDX_COMPLETED ? "credential_error" : cdx_status_name(process.status);
    if (process.stdout_bytes.data) SecureZeroMemory(process.stdout_bytes.data, process.stdout_bytes.size);
    SecureZeroMemory(&packet, sizeof(packet));
    cdx_result_free(&process);
done:
    free(program); return success;
}

static int password(vnc_context *context) {
    PCREDENTIALW stored = NULL;
    cdx_vnc_result *result = context->result;
    if (!context->new_password && CredReadW(context->alias, CRED_TYPE_GENERIC, 0, &stored)) {
        size_t size = stored->CredentialBlobSize;
        if (size && !(size % sizeof(wchar_t)) && size < sizeof(context->password)) {
            memcpy(context->password, stored->CredentialBlob, size);
            result->credential_status = "reused";
        } else result->credential_status = "invalid";
        if (size) SecureZeroMemory(stored->CredentialBlob, size);
        CredFree(stored);
    } else if (!context->new_password) {
        DWORD error = GetLastError();
        if (error != ERROR_NOT_FOUND) {
            result->image.status = "credential_error"; result->credential_error = error; return 0;
        }
    }
    if (!context->password[0]) {
        result->image.status = "credential_required";
        if (!context->request->prompt_for_password || !prompt_password(context)) return 0;
        context->new_password = 1;
    }
    for (size_t i = 0; i < VNC_PASSWORD_CHARS && context->password[i]; ++i)
        if (context->password[i] > 255) { result->image.status = "credential_error"; return 0; }
    if (!context->password[0]) { result->image.status = "credential_required"; return 0; }
    result->image.status = "protocol_error";
    return 1;
}

static int challenge_response(vnc_context *context, unsigned char challenge[16]) {
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    unsigned char secret[8] = {0}, encrypted[16] = {0};
    DWORD produced = 0;
    NTSTATUS status;
    for (size_t i = 0; i < 8 && context->password[i]; ++i) {
        unsigned char c = (unsigned char)context->password[i];
        c = (unsigned char)(((c & 0x55u) << 1) | ((c & 0xaau) >> 1));
        c = (unsigned char)(((c & 0x33u) << 2) | ((c & 0xccu) >> 2));
        secret[i] = (unsigned char)((c << 4) | (c >> 4));
    }
    status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_DES_ALGORITHM, NULL, 0);
    if (status >= 0) status = BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
    if (status >= 0) status = BCryptGenerateSymmetricKey(algorithm, &key, NULL, 0, secret, sizeof(secret), 0);
    if (status >= 0) status = BCryptEncrypt(key, challenge, 16, NULL, NULL, 0, encrypted, sizeof(encrypted), &produced, 0);
    if (status >= 0 && produced == 16) memcpy(challenge, encrypted, 16);
    else { context->result->image.status = "authentication_error"; context->result->image.native_error = (uint32_t)status; }
    if (key) BCryptDestroyKey(key);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    SecureZeroMemory(secret, sizeof(secret)); SecureZeroMemory(encrypted, sizeof(encrypted));
    return status >= 0 && produced == 16;
}

static void save_password(vnc_context *context) {
    CREDENTIALW entry = {0};
    entry.Type = CRED_TYPE_GENERIC; entry.TargetName = context->alias;
    entry.CredentialBlob = (LPBYTE)context->password;
    entry.CredentialBlobSize = (DWORD)(wcslen(context->password) * sizeof(wchar_t));
    entry.Persist = CRED_PERSIST_LOCAL_MACHINE; entry.UserName = L"VNC";
    if (CredWriteW(&entry, 0)) context->result->credential_status = "saved";
    else { context->result->credential_status = "save_failed"; context->result->credential_error = GetLastError(); }
}

static int read_updates(vnc_context *context, unsigned char *pixels, unsigned width, unsigned height) {
    size_t count = (size_t)width * height, covered = 0;
    unsigned char *coverage = (unsigned char *)calloc((count + 7u) / 8u, 1);
    unsigned rectangles = 0;
    int success = 0;
    if (!coverage) { context->result->image.status = "io_error"; return 0; }
    for (unsigned message = 0; message < 256u && !stopped(context); ++message) {
        unsigned char header[12];
        unsigned n;
        if (!transfer(context, header, 1, 0)) goto done;
        if (header[0] == 2) continue;
        if (header[0] == 3) {
            if (!transfer(context, header, 7, 0) || !discard(context, be32(header + 3))) goto done;
            continue;
        }
        if (header[0] != 0) { context->result->image.status = "not_supported"; goto done; }
        if (!transfer(context, header, 3, 0)) goto done;
        n = be16(header + 1);
        if (n > 4096u - rectangles) { context->result->image.status = "output_limit"; goto done; }
        rectangles += n;
        for (unsigned i = 0; i < n; ++i) {
            unsigned x, y, w, h;
            if (!transfer(context, header, 12, 0)) goto done;
            x = be16(header); y = be16(header + 2); w = be16(header + 4); h = be16(header + 6);
            if (be32(header + 8)) { context->result->image.status = "not_supported"; goto done; }
            if (!w || !h || x + w > width || y + h > height) { context->result->image.status = "protocol_error"; goto done; }
            for (unsigned row = 0; row < h; ++row) {
                size_t offset = (size_t)(y + row) * width + x;
                if (!transfer(context, pixels + offset * 4u, (size_t)w * 4u, 0)) goto done;
                for (unsigned column = 0; column < w; ++column) {
                    size_t position = offset + column;
                    unsigned char mask = (unsigned char)(1u << (position % 8u));
                    if (!(coverage[position / 8u] & mask)) { coverage[position / 8u] |= mask; ++covered; }
                }
            }
        }
        if (covered == count) { success = 1; goto done; }
    }
    if (!stopped(context)) context->result->image.status = "incomplete_frame";
done:
    free(coverage); return success;
}

static HBITMAP framebuffer(vnc_context *context) {
    unsigned char header[24], shared = 1;
    unsigned char format[20] = {0,0,0,0,32,24,0,1,0,255,0,255,0,255,16,8,0,0,0,0};
    unsigned char encodings[8] = {2,0,0,1,0,0,0,0};
    unsigned char update[10] = {3,0,0,0,0,0,0,0,0,0};
    unsigned width, height;
    BITMAPINFO info = {0};
    void *pixels = NULL;
    HBITMAP bitmap;
    context->result->stage = "framebuffer"; context->result->image.status = "protocol_error";
    if (!transfer(context, &shared, 1, 1) || !transfer(context, header, sizeof(header), 0)) return NULL;
    width = be16(header); height = be16(header + 2);
    if (!width || !height) return NULL;
    if ((uint64_t)width * height > CDX_SCREENSHOT_MAX_PIXELS) { context->result->image.status = "too_large"; return NULL; }
    if (!discard(context, be32(header + 20))) return NULL;
    context->result->image.width = (int)width; context->result->image.height = (int)height;
    update[6] = (unsigned char)(width >> 8); update[7] = (unsigned char)width;
    update[8] = (unsigned char)(height >> 8); update[9] = (unsigned char)height;
    if (!transfer(context, format, sizeof(format), 1) || !transfer(context, encodings, sizeof(encodings), 1) ||
        !transfer(context, update, sizeof(update), 1)) return NULL;
    info.bmiHeader.biSize = sizeof(info.bmiHeader); info.bmiHeader.biWidth = (LONG)width;
    info.bmiHeader.biHeight = -(LONG)height; info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    if (!bitmap) { context->result->image.status = "io_error"; context->result->image.native_error = GetLastError(); return NULL; }
    if (!read_updates(context, (unsigned char *)pixels, width, height)) { (void)DeleteObject(bitmap); return NULL; }
    return bitmap;
}

static void publish(vnc_context *context, HBITMAP bitmap, const wchar_t *path) {
    IStream *stream = NULL;
    HGLOBAL global = NULL;
    STATSTG stat = {0};
    HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED), hr = initialized;
    context->result->stage = "encode";
    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) hr = cdx_screenshot_encode_png(bitmap, &stream);
    if (SUCCEEDED(hr)) hr = IStream_Stat(stream, &stat, STATFLAG_NONAME);
    if (SUCCEEDED(hr)) hr = GetHGlobalFromStream(stream, &global);
    if (FAILED(hr)) { context->result->image.status = "encode_failed"; context->result->image.native_error = (uint32_t)hr; }
    else if (stat.cbSize.QuadPart > CDX_SCREENSHOT_MAX_PNG) context->result->image.status = "too_large";
    else if (!stopped(context)) {
        void *data = GlobalLock(global);
        if (!data) context->result->image.status = "encode_failed";
        else {
            context->result->stage = "write";
            cdx_screenshot_publish_png(path, (const unsigned char *)data, (size_t)stat.cbSize.QuadPart,
                context->deadline, context->request->cancellation, &context->result->image);
            (void)GlobalUnlock(global);
        }
    }
    if (stream) IStream_Release(stream);
    if (SUCCEEDED(initialized)) CoUninitialize();
}

int cdx_vnc_password_prompt(int argc, wchar_t **argv) {
    password_packet packet = {0};
    CREDUI_INFOW info = {0};
    wchar_t user[16] = L"VNC";
    BOOL save = FALSE;
    DWORD written = 0;
    int success;
    if (argc != 3) return 2;
    info.cbSize = sizeof(info); info.pszCaptionText = L"Cordonix VNC 截图";
    info.pszMessageText = L"远程 VNC 要求密码。认证成功后将保存到当前用户的 Windows 凭据管理器。";
    packet.magic = VNC_PROMPT_MAGIC;
    packet.error = CredUIPromptForCredentialsW(&info, argv[2], NULL, 0, user, 16, packet.password, VNC_PASSWORD_CHARS,
        &save, CREDUI_FLAGS_GENERIC_CREDENTIALS | CREDUI_FLAGS_ALWAYS_SHOW_UI | CREDUI_FLAGS_DO_NOT_PERSIST | CREDUI_FLAGS_KEEP_USERNAME);
    success = WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), &packet, sizeof(packet), &written, NULL) && written == sizeof(packet);
    SecureZeroMemory(&packet, sizeof(packet));
    return success ? 0 : 1;
}
