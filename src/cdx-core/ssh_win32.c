#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "cdx/ssh.h"
#include <wincred.h>
#include <bcrypt.h>
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define SSH_POLLS 60000u
#define SSH_LOGIN_BYTES 32768u

typedef struct ssh_secret {
    char user[256];
    char *password;
    char *key;
    size_t key_size;
} ssh_secret;

typedef struct ssh_context {
    const cdx_ssh_request *request;
    cdx_ssh_result *result;
    uint64_t deadline;
    unsigned int polls;
    SOCKET socket;
    LIBSSH2_SESSION *bastion, *target;
    LIBSSH2_CHANNEL *forward, *channel;
    LIBSSH2_SFTP *sftp;
    LIBSSH2_SFTP_HANDLE *sftp_handle;
    size_t allocations;
    ssh_secret bastion_secret, target_secret, su_secret;
} ssh_context;

static int validate(const cdx_ssh_request *request);
static LIBSSH2_ALLOC_FUNC(session_allocate);
static LIBSSH2_FREE_FUNC(session_release);
static LIBSSH2_REALLOC_FUNC(session_resize);
static int stopped(ssh_context *context, int pause);
static int connect_socket(ssh_context *context, const cdx_ssh_endpoint *endpoint);
static int handshake(ssh_context *context, LIBSSH2_SESSION **session, const cdx_ssh_endpoint *endpoint, char fingerprint[65], int tunneled);
static int authenticate(ssh_context *context, LIBSSH2_SESSION *session, const cdx_ssh_endpoint *endpoint, ssh_secret *secret);
static int run_command(ssh_context *context);
static int run_interaction(ssh_context *context);
static int run_sftp(ssh_context *context);
static void cleanup(ssh_context *context);

void cdx_ssh_execute(const cdx_ssh_request *request, cdx_ssh_result *result) {
    ssh_context context;
    WSADATA winsock;
    uint64_t started = GetTickCount64();
    int initialized = 0, socket_initialized = 0, rc;
    if (!result) return;
    memset(result, 0, sizeof(*result));
    memset(&context, 0, sizeof(context));
    result->status = "validation_error"; result->stage = "validation";
    result->message = "Invalid SSH request"; result->cleanup_complete = 1;
    result->proxy.socket = UINTPTR_MAX;
    if (!validate(request)) return;
    context.request = request; context.result = result;
    context.socket = INVALID_SOCKET; context.deadline = started + request->timeout_ms;
    result->status = "connection_error"; result->message = "SSH connection did not complete";
    if (stopped(&context, 0)) goto done;
    if (WSAStartup(MAKEWORD(2, 2), &winsock)) goto done;
    socket_initialized = 1;
    if (libssh2_init(0)) goto done;
    initialized = 1;
    if (!strcmp(request->route, "direct")) {
        if (!connect_socket(&context, &request->target) ||
            !handshake(&context, &context.target, &request->target, result->host_key_sha256, 0)) goto done;
    } else {
        result->stage = "bastion";
        if (!connect_socket(&context, &request->bastion) ||
            !handshake(&context, &context.bastion, &request->bastion, result->bastion_host_key_sha256, 0)) goto done;
        if (!strcmp(request->action, "probe") && !strcmp(request->route, "interactive")) goto probed;
        if (!authenticate(&context, context.bastion, &request->bastion, &context.bastion_secret)) goto done;
        if (!strcmp(request->route, "jump")) {
            result->stage = "forward";
            do {
                if (stopped(&context, 0)) goto done;
                context.forward = libssh2_channel_direct_tcpip_ex(context.bastion, request->target.host,
                    (int)request->target.port, "127.0.0.1", 0);
                rc = context.forward ? 0 : libssh2_session_last_errno(context.bastion);
            } while (rc == LIBSSH2_ERROR_EAGAIN && !stopped(&context, 1));
            if (!context.forward || !handshake(&context, &context.target, &request->target, result->host_key_sha256, 1)) goto done;
        }
    }
    if (!strcmp(request->action, "probe")) goto probed;
    result->stdout_data = (unsigned char *)calloc(request->output_limit_bytes + 1u, 1);
    result->stderr_data = (unsigned char *)calloc(request->output_limit_bytes + 1u, 1);
    if (!result->stdout_data || !result->stderr_data) { result->status = "execution_error"; result->message = "Cannot allocate bounded SSH output"; goto done; }
    if (!strcmp(request->route, "interactive")) (void)run_interaction(&context);
    else if (authenticate(&context, context.target, &request->target, &context.target_secret)) {
        if (request->sftp) (void)run_sftp(&context);
        else (void)run_command(&context);
    }
    goto done;
probed:
    result->status = "completed"; result->stage = "host_key"; result->message = "Observed host key; probe does not authenticate the target";
done:
    cleanup(&context);
    if (initialized) libssh2_exit();
    if (socket_initialized) WSACleanup();
    result->elapsed_ms = GetTickCount64() - started;
}

void cdx_ssh_result_free(cdx_ssh_result *result) {
    if (!result) return;
    if (result->stdout_data) { SecureZeroMemory(result->stdout_data, result->stdout_size); free(result->stdout_data); }
    if (result->stderr_data) { SecureZeroMemory(result->stderr_data, result->stderr_size); free(result->stderr_data); }
    memset(result, 0, sizeof(*result));
}

static int safe_user(const char *value) {
    size_t i, length = strlen(value);
    if (!length || length > 255u || value[0] == '-') return 0;
    for (i = 0; i < length; ++i)
        if (!(value[i] >= 'a' && value[i] <= 'z') && !(value[i] >= 'A' && value[i] <= 'Z') &&
            !(value[i] >= '0' && value[i] <= '9') && !strchr("._-", value[i])) return 0;
    return 1;
}

static int literal_ip(const char *host) {
    struct in6_addr address;
    return InetPtonA(AF_INET, host, &address) == 1 || InetPtonA(AF_INET6, host, &address) == 1;
}

static int valid_endpoint(const cdx_ssh_endpoint *endpoint) {
    size_t i, length;
    if (!endpoint->host || !endpoint->user || !endpoint->credential || !endpoint->private_key_path || !endpoint->host_key_sha256) return 0;
    length = strlen(endpoint->host_key_sha256);
    if (!literal_ip(endpoint->host) || !endpoint->port || endpoint->port > 65535u ||
        (*endpoint->user && !safe_user(endpoint->user)) || strlen(endpoint->credential) > 256u ||
        (*endpoint->credential && *endpoint->private_key_path)) return 0;
    if (length && length != 64u) return 0;
    for (i = 0; i < length; ++i) if (!strchr("0123456789abcdefABCDEF", endpoint->host_key_sha256[i])) return 0;
    if (*endpoint->private_key_path) {
        const char *path = endpoint->private_key_path;
        if (strlen(path) > 32760u || strpbrk(path, "\r\n|<>\"$`*?()") ||
            !((strlen(path) >= 3u && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) ||
              (path[0] == '\\' && path[1] == '\\'))) return 0;
    }
    return 1;
}

static int validate(const cdx_ssh_request *request) {
    const cdx_ssh_interaction *interaction;
    size_t i, line = 0;
    if (!request || !request->action || !request->route || !request->script ||
        (strcmp(request->action, "execute") && strcmp(request->action, "probe") && strcmp(request->action, "sftp")) ||
        (strcmp(request->route, "direct") && strcmp(request->route, "jump") && strcmp(request->route, "interactive")) ||
        !request->timeout_ms || request->timeout_ms > 300000u || !request->output_limit_bytes ||
        request->output_limit_bytes > CDX_SSH_MAX_OUTPUT || strlen(request->script) > CDX_SSH_MAX_SCRIPT ||
        !valid_endpoint(&request->target)) return 0;
    if (strcmp(request->route, "direct") && !valid_endpoint(&request->bastion)) return 0;
    if (request->sftp && strcmp(request->action, "sftp")) return 0;
    if (!strcmp(request->action, "sftp")) {
        const cdx_sftp_options *options = request->sftp;
        if (!options || !strcmp(request->route, "interactive") || !options->operation || !options->path ||
            !*options->path || strlen(options->path) > 4096u || strpbrk(options->path, "\r\n") ||
            (strcmp(options->operation, "stat") && strcmp(options->operation, "read") && strcmp(options->operation, "write")) ||
            !options->read_bytes || options->read_bytes > request->output_limit_bytes || options->data_size > CDX_SSH_MAX_SCRIPT ||
            (options->data_size && !options->data) || (!strcmp(options->operation, "write") && !options->allow_write)) return 0;
        return 1;
    }
    if (!strcmp(request->action, "execute") && !*request->script) return 0;
    if (strcmp(request->route, "interactive")) return 1;
    interaction = &request->interaction;
    if (!interaction->ready_prompt || !interaction->selection || !interaction->selection_prefix ||
        !interaction->password_prompt || !interaction->shell_prompt || !interaction->su_credential ||
        !interaction->su_password_prompt || !interaction->root_prompt) return 0;
    if (!strcmp(request->action, "probe")) return 1;
    if (*request->target.private_key_path || *request->target.host_key_sha256) return 0;
    if (!*interaction->ready_prompt || !*interaction->shell_prompt || !*interaction->selection ||
        strlen(interaction->ready_prompt) > 256u || strlen(interaction->shell_prompt) > 256u ||
        strlen(interaction->password_prompt) > 256u || strlen(interaction->su_password_prompt) > 256u ||
        strlen(interaction->root_prompt) > 256u || strlen(interaction->selection) > 2048u ||
        strlen(interaction->selection_prefix) > 16u || strlen(interaction->su_credential) > 256u ||
        strpbrk(interaction->selection, "\r\n") || strpbrk(interaction->selection_prefix, "\r\n") ||
        !strstr(interaction->selection, "{host}") || (*interaction->su_credential && !*interaction->root_prompt)) return 0;
    for (i = 0; request->script[i]; ++i) {
        line = request->script[i] == '\n' ? 0 : line + 1u;
        if (line > 2048u) return 0;
    }
    return 1;
}

static int stopped(ssh_context *context, int pause) {
    if (context->request->cancellation && WaitForSingleObject(context->request->cancellation, 0) == WAIT_OBJECT_0) {
        context->result->status = "cancelled"; context->result->message = "SSH operation cancelled"; return 1;
    }
    if (++context->polls > SSH_POLLS || GetTickCount64() >= context->deadline) {
        context->result->status = "timed_out"; context->result->message = "SSH deadline or operation budget reached"; return 1;
    }
    if (pause) Sleep(10);
    return 0;
}

static int connect_socket(ssh_context *context, const cdx_ssh_endpoint *endpoint) {
    cdx_proxy_request request;
    cdx_proxy_result *proxy = &context->result->proxy;
    memset(&request, 0, sizeof(request));
    context->result->stage = !strcmp(context->request->route, "direct") ? "connect" : "bastion_connect";
    request.host = endpoint->host; request.port = endpoint->port;
    request.proxy = context->request->proxy ? context->request->proxy : "auto";
    request.deadline_ms = context->deadline; request.cancellation = context->request->cancellation;
    cdx_proxy_connect(&request, proxy);
    if (proxy->socket == UINTPTR_MAX) {
        context->result->status = proxy->status;
        context->result->message = "Direct/proxy TCP connection did not complete";
        return 0;
    }
    context->socket = (SOCKET)proxy->socket; proxy->socket = UINTPTR_MAX;
    return 1;
}

static LIBSSH2_SEND_FUNC(tunnel_send) {
    ssh_context *context = (ssh_context *)*abstract;
    ssize_t rc;
    (void)socket; (void)flags;
    rc = libssh2_channel_write(context->forward, (const char *)buffer, length);
    /* Transport callbacks use negative errno, not the channel API's -37. */
    return rc == LIBSSH2_ERROR_EAGAIN ? -EAGAIN : rc < 0 ? -EIO : rc;
}

static LIBSSH2_RECV_FUNC(tunnel_receive) {
    ssh_context *context = (ssh_context *)*abstract;
    ssize_t rc;
    (void)socket; (void)flags;
    rc = libssh2_channel_read(context->forward, (char *)buffer, length);
    if (!rc && !libssh2_channel_eof(context->forward)) return -EAGAIN;
    return rc == LIBSSH2_ERROR_EAGAIN ? -EAGAIN : rc < 0 ? -EIO : rc;
}

static int handshake(ssh_context *context, LIBSSH2_SESSION **session, const cdx_ssh_endpoint *endpoint, char fingerprint[65], int tunneled) {
    const unsigned char *hash;
    int rc;
    const char *digits = "0123456789abcdef";
    int probe = !strcmp(context->request->action, "probe") &&
        (session == &context->target || !strcmp(context->request->route, "interactive"));
    context->result->stage = session == &context->bastion ? "bastion_handshake" : "target_handshake";
    *session = libssh2_session_init_ex(session_allocate, session_release, session_resize, context);
    if (!*session) return 0;
    libssh2_session_set_blocking(*session, 0);
    if (tunneled) {
        (void)libssh2_session_callback_set2(*session, LIBSSH2_CALLBACK_SEND, (libssh2_cb_generic *)tunnel_send);
        (void)libssh2_session_callback_set2(*session, LIBSSH2_CALLBACK_RECV, (libssh2_cb_generic *)tunnel_receive);
    }
    do {
        if (stopped(context, 0)) return 0;
        rc = libssh2_session_handshake(*session, context->socket);
    } while (rc == LIBSSH2_ERROR_EAGAIN && !stopped(context, 1));
    if (rc) return 0;
    hash = (const unsigned char *)libssh2_hostkey_hash(*session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (!hash) return 0;
    for (size_t i = 0; i < 32u; ++i) { fingerprint[2u*i] = digits[hash[i] >> 4]; fingerprint[2u*i + 1u] = digits[hash[i] & 15u]; }
    fingerprint[64] = 0;
    if (!*endpoint->host_key_sha256 && probe) return 1;
    if (!*endpoint->host_key_sha256 || _stricmp(endpoint->host_key_sha256, fingerprint)) {
        context->result->status = *endpoint->host_key_sha256 ? "host_key_mismatch" : "host_key_required";
        context->result->message = "Pin the independently verified SHA-256 host key before authentication"; return 0;
    }
    if (session == &context->target) context->result->target_host_key_verified = 1;
    else context->result->bastion_host_key_verified = 1;
    return 1;
}

static wchar_t *wide(const char *text) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    wchar_t *value = count ? (wchar_t *)calloc((size_t)count, sizeof(*value)) : NULL;
    if (value) (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, value, count);
    return value;
}

static char *utf8(const wchar_t *text, int count) {
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, count, NULL, 0, NULL, NULL);
    char *value = bytes > 0 ? (char *)calloc((size_t)bytes + 1u, 1) : NULL;
    if (value) (void)WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, count, value, bytes, NULL, NULL);
    return value;
}

static int load_secret(ssh_context *context, const cdx_ssh_endpoint *endpoint, ssh_secret *secret) {
    if (*endpoint->credential) {
        wchar_t *alias = wide(endpoint->credential);
        PCREDENTIALW credential = NULL;
        char *user = NULL;
        int count;
        if (alias && CredReadW(alias, CRED_TYPE_GENERIC, 0, &credential)) {
            if (credential->UserName) user = utf8(credential->UserName, -1);
            if (credential->CredentialBlobSize && credential->CredentialBlobSize <= 8192u && !(credential->CredentialBlobSize % sizeof(wchar_t))) {
                count = (int)(credential->CredentialBlobSize / sizeof(wchar_t));
                if (((wchar_t *)credential->CredentialBlob)[count - 1] == 0) --count;
                if (count > 0 && !wmemchr((wchar_t *)credential->CredentialBlob, 0, (size_t)count))
                    secret->password = utf8((wchar_t *)credential->CredentialBlob, count);
            }
            CredFree(credential);
        }
        free(alias);
        if (user && strlen(user) < sizeof(secret->user)) (void)strcpy_s(secret->user, sizeof(secret->user), user);
        free(user);
        if (!secret->password) {
            context->result->status = "credential_error"; context->result->message = "Credential alias is missing or is not a UTF-16 Windows Generic credential"; return 0;
        }
    }
    if (*endpoint->user) (void)strcpy_s(secret->user, sizeof(secret->user), endpoint->user);
    if (!safe_user(secret->user)) {
        context->result->status = "credential_error"; context->result->message = "Supply an SSH user or a credential with a valid user name"; return 0;
    }
    if (*endpoint->private_key_path) {
        wchar_t *path = wide(endpoint->private_key_path);
        HANDLE file = path ? CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
        LARGE_INTEGER size;
        DWORD got;
        free(path);
        if (file != INVALID_HANDLE_VALUE) {
            if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 65536) {
                secret->key_size = (size_t)size.QuadPart;
                secret->key = (char *)calloc(secret->key_size + 1u, 1);
                if (secret->key && (!ReadFile(file, secret->key, (DWORD)secret->key_size, &got, NULL) || got != secret->key_size)) {
                    SecureZeroMemory(secret->key, secret->key_size); free(secret->key); secret->key = NULL;
                }
            }
            CloseHandle(file);
        }
        if (!secret->key) { context->result->status = "credential_error"; context->result->message = "Private key could not be read within the 64 KiB limit"; return 0; }
    }
    return 1;
}

static int authenticate(ssh_context *context, LIBSSH2_SESSION *session, const cdx_ssh_endpoint *endpoint, ssh_secret *secret) {
    int rc;
    context->result->stage = session == context->bastion ? "bastion_authentication" : "target_authentication";
    if (!load_secret(context, endpoint, secret)) return 0;
    if (!secret->key && !secret->password) { context->result->status = "credential_error"; context->result->message = "Supply a credential alias or private key path"; return 0; }
    do {
        if (stopped(context, 0)) return 0;
        rc = secret->key ? libssh2_userauth_publickey_frommemory(session, secret->user, strlen(secret->user), NULL, 0,
                secret->key, secret->key_size, NULL) :
            libssh2_userauth_password_ex(session, secret->user, (unsigned int)strlen(secret->user), secret->password,
                (unsigned int)strlen(secret->password), NULL);
    } while (rc == LIBSSH2_ERROR_EAGAIN && !stopped(context, 1));
    if (rc) {
        if (strcmp(context->result->status, "cancelled") && strcmp(context->result->status, "timed_out")) {
            context->result->status = "authentication_failed"; context->result->message = "SSH authentication failed; no automatic retry";
        }
        return 0;
    }
    return 1;
}

static int open_channel(ssh_context *context, LIBSSH2_SESSION *session) {
    int rc;
    do {
        if (stopped(context, 0)) return 0;
        context->channel = libssh2_channel_open_session(session);
        rc = context->channel ? 0 : libssh2_session_last_errno(session);
    } while (rc == LIBSSH2_ERROR_EAGAIN && !stopped(context, 1));
    return context->channel != NULL;
}

static int write_channel(ssh_context *context, const char *data, size_t length) {
    size_t offset = 0;
    for (unsigned int poll = 0; poll < SSH_POLLS && offset < length && !stopped(context, 0); ++poll) {
        ssize_t rc = libssh2_channel_write(context->channel, data + offset, length - offset);
        if (rc == LIBSSH2_ERROR_EAGAIN || !rc) { if (stopped(context, 1)) return 0; }
        else if (rc < 0) return 0;
        else offset += (size_t)rc;
    }
    return offset == length;
}

static int append_output(ssh_context *context, int stream, const char *data, size_t length) {
    cdx_ssh_result *result = context->result;
    size_t remaining = context->request->output_limit_bytes - result->stdout_size - result->stderr_size;
    size_t keep = length < remaining ? length : remaining;
    size_t *size = stream ? &result->stderr_size : &result->stdout_size;
    unsigned char *target = stream ? result->stderr_data : result->stdout_data;
    memcpy(target + *size, data, keep); *size += keep; target[*size] = 0;
    if (keep != length) { result->status = "output_limit"; result->message = "Remote output exceeded the requested limit"; return 0; }
    return 1;
}

static int run_command(ssh_context *context) {
    int rc;
    char chunk[4096];
    const char *guard = "test \"$(id -u)\" -eq 0 || exit 77\n";
    cdx_ssh_result *result = context->result;
    result->stage = "execute";
    if (!open_channel(context, context->target)) return 0;
    result->remote_command_started = 1;
    do {
        if (stopped(context, 0)) return 0;
        rc = libssh2_channel_exec(context->channel, "bash -s");
    } while (rc == LIBSSH2_ERROR_EAGAIN && !stopped(context, 1));
    if (rc) return 0;
    if (context->request->require_root && !write_channel(context, guard, strlen(guard))) return 0;
    if (!write_channel(context, context->request->script, strlen(context->request->script))) return 0;
    do {
        if (stopped(context, 0)) return 0;
        rc = libssh2_channel_send_eof(context->channel);
    } while (rc == LIBSSH2_ERROR_EAGAIN && !stopped(context, 1));
    if (rc) return 0;
    for (unsigned int poll = 0; poll < SSH_POLLS && !stopped(context, 0); ++poll) {
        int progress = 0;
        for (int stream = 0; stream < 2; ++stream) {
            ssize_t got = libssh2_channel_read_ex(context->channel, stream, chunk, sizeof(chunk));
            if (got > 0) { if (!append_output(context, stream, chunk, (size_t)got)) return 0; progress = 1; }
            else if (got < 0 && got != LIBSSH2_ERROR_EAGAIN) return 0;
        }
        if (!progress && libssh2_channel_eof(context->channel)) break;
        if (!progress && stopped(context, 1)) return 0;
    }
    do {
        if (stopped(context, 0)) return 0;
        rc = libssh2_channel_wait_closed(context->channel);
    } while (rc == LIBSSH2_ERROR_EAGAIN && !stopped(context, 1));
    if (rc) return 0;
    result->exit_code = libssh2_channel_get_exit_status(context->channel); result->exit_code_known = 1;
    result->status = result->exit_code ? "failed" : "completed"; result->message = "Remote command exited";
    return 1;
}

static void terminal_text(char *text) {
    size_t read = 0, write = 0, length = strlen(text);
    for (; read < length; ++read) {
        unsigned char ch = (unsigned char)text[read];
        if (ch == 27u && read + 1u < length && text[read + 1u] == '[') {
            read += 2u;
            for (; read < length && !((unsigned char)text[read] >= 0x40u && (unsigned char)text[read] <= 0x7eu); ++read) { }
        } else if (ch == 27u && read + 1u < length && text[read + 1u] == ']') {
            read += 2u;
            for (; read < length && text[read] != 7 && !(text[read] == 27 && read + 1u < length && text[read+1u] == '\\'); ++read) { }
            if (read < length && text[read] == 27) ++read;
        } else if (ch == '\b') { if (write) --write; }
        else if (ch != '\r') text[write++] = (char)ch;
    }
    text[write] = 0;
}

static int read_terminal(ssh_context *context, char *text, size_t capacity, size_t *size) {
    int progress = 0;
    for (int stream = 0; stream < 2; ++stream) {
        char chunk[2048];
        ssize_t got = libssh2_channel_read_ex(context->channel, stream, chunk, sizeof(chunk));
        if (got > 0) {
            if ((size_t)got >= capacity - *size) { context->result->status = "output_limit"; context->result->message = "Interactive SSH transcript exceeded its bound"; return -1; }
            memcpy(text + *size, chunk, (size_t)got); *size += (size_t)got; text[*size] = 0; progress = 1;
        } else if (got < 0 && got != LIBSSH2_ERROR_EAGAIN) return -1;
    }
    if (!progress && libssh2_channel_eof(context->channel)) return -1;
    return progress;
}

static int wait_prompt(ssh_context *context, const char *prompt, const char *password_prompt, const char *password) {
    char *text = (char *)calloc(SSH_LOGIN_BYTES, 1), *plain = (char *)calloc(SSH_LOGIN_BYTES, 1);
    size_t size = 0;
    int sent_password = 0, success = 0;
    if (!text || !plain) goto done;
    for (unsigned int poll = 0; poll < SSH_POLLS && !stopped(context, 0); ++poll) {
        int progress = read_terminal(context, text, SSH_LOGIN_BYTES, &size);
        if (progress < 0) break;
        memcpy(plain, text, size + 1u); terminal_text(plain);
        if (strstr(plain, prompt)) { success = 1; break; }
        if (*password_prompt && strstr(plain, password_prompt)) {
            if (sent_password || !password || strpbrk(password, "\r\n")) {
                context->result->status = "authentication_failed"; context->result->message = "Interactive password prompt could not be satisfied"; break;
            }
            if (!write_channel(context, password, strlen(password)) || !write_channel(context, "\n", 1)) break;
            sent_password = 1; size = 0; text[0] = 0;
        } else if (strstr(plain, "yes/no") || strstr(plain, "continue connecting")) {
            context->result->status = "host_key_required"; context->result->message = "The bastion must verify the selected target's host key; confirmation is not automated"; break;
        }
        if (!progress && stopped(context, 1)) break;
    }
done:
    if (text) { SecureZeroMemory(text, SSH_LOGIN_BYTES); free(text); }
    if (plain) { SecureZeroMemory(plain, SSH_LOGIN_BYTES); free(plain); }
    return success;
}

static char *selection_text(ssh_context *context) {
    const char *source = context->request->interaction.selection;
    char *text = (char *)calloc(4096u, 1), port[8];
    size_t used = 0;
    if (!text) return NULL;
    (void)snprintf(port, sizeof(port), "%u", context->request->target.port);
    for (size_t i = 0; source[i] && i < 2048u; ) {
        const char *replacement;
        size_t skip, length;
        if (!strncmp(source + i, "{host}", 6u)) { replacement = context->request->target.host; skip = 6u; }
        else if (!strncmp(source + i, "{port}", 6u)) { replacement = port; skip = 6u; }
        else if (!strncmp(source + i, "{user}", 6u)) { replacement = context->target_secret.user; skip = 6u; }
        else { if (used + 1u >= 4096u) { free(text); return NULL; } text[used++] = source[i++]; continue; }
        length = strlen(replacement);
        if (used + length >= 4096u) { free(text); return NULL; }
        memcpy(text + used, replacement, length); used += length; i += skip;
    }
    text[used] = 0;
    return text;
}

static int run_interaction(ssh_context *context) {
    const cdx_ssh_request *request = context->request;
    const cdx_ssh_interaction *interaction = &request->interaction;
    cdx_ssh_result *result = context->result;
    char *selection = NULL, *command = NULL, *transcript = NULL;
    char token[33], ready[64], begin[64], end[64], line[256];
    unsigned char random[16];
    size_t size = 0, capacity = request->output_limit_bytes + 4096u;
    int rc, success = 0;
    cdx_ssh_endpoint su = {"127.0.0.1", 22u, "root", interaction->su_credential, "", ""};
    result->stage = "interactive_login";
    if (!load_secret(context, &request->target, &context->target_secret) || !open_channel(context, context->bastion)) goto done;
    do {
        if (stopped(context, 0)) goto done;
        rc = libssh2_channel_request_pty(context->channel, "xterm");
    } while (rc == LIBSSH2_ERROR_EAGAIN && !stopped(context, 1));
    if (rc) goto done;
    do {
        if (stopped(context, 0)) goto done;
        rc = libssh2_channel_shell(context->channel);
    } while (rc == LIBSSH2_ERROR_EAGAIN && !stopped(context, 1));
    if (rc || !wait_prompt(context, interaction->ready_prompt, "", NULL)) goto done;
    selection = selection_text(context);
    if (!selection || !write_channel(context, interaction->selection_prefix, strlen(interaction->selection_prefix))) goto done;
    if (*interaction->selection_prefix && stopped(context, 1)) goto done;
    if (!write_channel(context, selection, strlen(selection)) || !write_channel(context, "\r", 1) ||
        !wait_prompt(context, interaction->shell_prompt, interaction->password_prompt, context->target_secret.password)) goto done;
    if (*interaction->su_credential) {
        result->stage = "elevation";
        if (!load_secret(context, &su, &context->su_secret) || !write_channel(context, "su - root\n", 10u) ||
            !wait_prompt(context, interaction->root_prompt, interaction->su_password_prompt, context->su_secret.password)) goto done;
    }
    if (BCryptGenRandom(NULL, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG)) goto done;
    for (size_t i = 0; i < sizeof(random); ++i) (void)snprintf(token + 2u*i, 3u, "%02x", (unsigned int)random[i]);
    (void)snprintf(ready, sizeof(ready), "CDX_READY_%s", token);
    (void)snprintf(begin, sizeof(begin), "CDX_BEGIN_%s", token);
    (void)snprintf(end, sizeof(end), "CDX_DONE_%s:", token);
    /* Split tokens so an echoed command cannot impersonate its reply. */
    (void)snprintf(line, sizeof(line), "stty -echo; printf '\\n%%s%%s\\n' 'CDX_READY_' '%s'\n", token);
    if (!write_channel(context, line, strlen(line)) || !wait_prompt(context, ready, "", NULL)) goto done;
    command = (char *)calloc(CDX_SSH_MAX_SCRIPT + 1024u, 1);
    transcript = (char *)calloc(capacity, 1);
    if (!command || !transcript) goto done;
    (void)snprintf(command, CDX_SSH_MAX_SCRIPT + 1024u,
        "printf '%%s%%s\\n' 'CDX_BEGIN_' '%s'; bash -s <<'CDX_EOF_%s'\n%s%s\nCDX_EOF_%s\ncdx_result=$?; printf '\\n%%s%%s:%%d\\n' 'CDX_DONE_' '%s' \"$cdx_result\"\n",
        token, token, request->require_root ? "test \"$(id -u)\" -eq 0 || exit 77\n" : "", request->script, token, token);
    result->stage = "execute"; result->remote_command_started = 1;
    if (!write_channel(context, command, strlen(command))) goto done;
    for (unsigned int poll = 0; poll < SSH_POLLS && !stopped(context, 0); ++poll) {
        char *start, *finish;
        int progress = read_terminal(context, transcript, capacity, &size);
        if (progress < 0) break;
        start = strstr(transcript, begin); finish = start ? strstr(start + strlen(begin), end) : NULL;
        if (start && finish) {
            char *number_end;
            long exit_code;
            start = strchr(start, '\n');
            exit_code = strtol(finish + strlen(end), &number_end, 10);
            if (!start || !*number_end || (*number_end != '\r' && *number_end != '\n')) continue;
            if (exit_code < 0 || exit_code > 255) break;
            ++start;
            if (finish > start && finish[-1] == '\n') --finish;
            if (finish > start && finish[-1] == '\r') --finish;
            if (!append_output(context, 0, start, (size_t)(finish - start))) break;
            result->exit_code = (int)exit_code; result->exit_code_known = 1;
            result->status = exit_code ? "failed" : "completed"; result->message = "Interactive remote command exited; PTY output combines stdout and stderr";
            success = 1; break;
        }
        if (!progress && stopped(context, 1)) break;
    }
done:
    free(selection);
    if (command) { SecureZeroMemory(command, CDX_SSH_MAX_SCRIPT + 1024u); free(command); }
    if (transcript) { SecureZeroMemory(transcript, capacity); free(transcript); }
    return success;
}

static void clear_secret(ssh_secret *secret) {
    if (secret->password) { SecureZeroMemory(secret->password, strlen(secret->password)); free(secret->password); }
    if (secret->key) { SecureZeroMemory(secret->key, secret->key_size); free(secret->key); }
    SecureZeroMemory(secret, sizeof(*secret));
}

static void redact_output(cdx_ssh_result *result, const ssh_secret *secret) {
    size_t length = secret->password ? strlen(secret->password) : 0;
    unsigned char *buffers[2] = {result->stdout_data, result->stderr_data};
    size_t sizes[2] = {result->stdout_size, result->stderr_size};
    if (!length) return;
    for (size_t stream = 0; stream < 2u; ++stream)
        for (size_t i = 0; i + length <= sizes[stream]; ++i)
            if (!memcmp(buffers[stream] + i, secret->password, length)) memset(buffers[stream] + i, '*', length);
}

static void cleanup(ssh_context *context) {
    uint64_t deadline = GetTickCount64() + 2000u;
    LIBSSH2_SESSION *sessions[2] = {context->target, context->bastion};
    if (context->socket != INVALID_SOCKET) (void)shutdown(context->socket, SD_BOTH);
    if (context->sftp_handle) {
        int rc = LIBSSH2_ERROR_EAGAIN;
        for (unsigned int poll = 0; poll < 200u && GetTickCount64() < deadline; ++poll) {
            rc = libssh2_sftp_close(context->sftp_handle);
            if (rc != LIBSSH2_ERROR_EAGAIN) { context->sftp_handle = NULL; break; }
            Sleep(5);
        }
        if (context->sftp_handle) context->result->cleanup_complete = 0;
    }
    if (context->sftp) {
        int rc = LIBSSH2_ERROR_EAGAIN;
        for (unsigned int poll = 0; poll < 200u && GetTickCount64() < deadline; ++poll) {
            rc = libssh2_sftp_shutdown(context->sftp);
            if (rc != LIBSSH2_ERROR_EAGAIN) break;
            Sleep(5);
        }
        if (rc) context->result->cleanup_complete = 0;
    }
    for (size_t i = 0; i < 2u; ++i) {
        int rc = 0;
        if (!sessions[i]) continue;
        for (unsigned int poll = 0; poll < 200u && GetTickCount64() < deadline; ++poll) {
            rc = libssh2_session_free(sessions[i]);
            if (rc != LIBSSH2_ERROR_EAGAIN) break;
            Sleep(5);
        }
        if (rc) context->result->cleanup_complete = 0;
    }
    if (context->socket != INVALID_SOCKET && closesocket(context->socket)) context->result->cleanup_complete = 0;
    if (!context->request->sftp) {
        redact_output(context->result, &context->target_secret);
        redact_output(context->result, &context->bastion_secret);
        redact_output(context->result, &context->su_secret);
    }
    clear_secret(&context->target_secret); clear_secret(&context->bastion_secret); clear_secret(&context->su_secret);
    context->result->session_allocations_remaining = context->allocations;
    if (context->allocations) context->result->cleanup_complete = 0;
    if (context->result->proxy.status && !context->result->proxy.cleanup_complete) context->result->cleanup_complete = 0;
}

static LIBSSH2_ALLOC_FUNC(session_allocate) {
    ssh_context *context = (ssh_context *)*abstract;
    void *allocation = malloc(count);
    if (allocation) ++context->allocations;
    return allocation;
}
static LIBSSH2_FREE_FUNC(session_release) {
    ssh_context *context = (ssh_context *)*abstract;
    if (ptr) { free(ptr); --context->allocations; }
}
static LIBSSH2_REALLOC_FUNC(session_resize) {
    ssh_context *context = (ssh_context *)*abstract;
    void *allocation;
    if (!count) { if (ptr) { free(ptr); --context->allocations; } return NULL; }
    allocation = realloc(ptr, count);
    if (allocation && !ptr) ++context->allocations;
    return allocation;
}

static int run_sftp(ssh_context *context) {
    const cdx_sftp_options *options = context->request->sftp;
    cdx_ssh_result *result = context->result;
    LIBSSH2_SFTP_HANDLE *handle = NULL;
    LIBSSH2_SFTP_ATTRIBUTES attributes;
    int rc, success = 0;
    result->stage = "sftp"; result->message = "SFTP operation did not complete";
    do {
        if (stopped(context, 0)) return 0;
        context->sftp = libssh2_sftp_init(context->target);
        rc = context->sftp ? 0 : libssh2_session_last_errno(context->target);
    } while (rc == LIBSSH2_ERROR_EAGAIN && !stopped(context, 1));
    if (!context->sftp) return 0;
    memset(&attributes, 0, sizeof(attributes));
    result->remote_command_started = 1;
    if (!strcmp(options->operation, "stat")) {
        do {
            if (stopped(context, 0)) return 0;
            rc = libssh2_sftp_stat(context->sftp, options->path, &attributes);
        } while (rc == LIBSSH2_ERROR_EAGAIN && !stopped(context, 1));
        if (rc == LIBSSH2_ERROR_EAGAIN) return 0;
        if (rc) goto remote_error;
        result->file_size = attributes.filesize; result->file_permissions = attributes.permissions;
        result->file_attribute_flags = attributes.flags;
        success = 1;
    } else {
        unsigned long flags = !strcmp(options->operation, "write") ?
            LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_EXCL : LIBSSH2_FXF_READ;
        do {
            if (stopped(context, 0)) return 0;
            handle = libssh2_sftp_open(context->sftp, options->path, flags, 0600);
            rc = handle ? 0 : libssh2_session_last_errno(context->target);
        } while (rc == LIBSSH2_ERROR_EAGAIN && !stopped(context, 1));
        if (!handle) {
            if (rc != LIBSSH2_ERROR_EAGAIN) goto remote_error;
            return 0;
        }
        context->sftp_handle = handle;
        if (!strcmp(options->operation, "read")) {
            libssh2_sftp_seek64(handle, options->offset);
            for (unsigned int poll = 0; poll < SSH_POLLS && result->stdout_size < options->read_bytes && !stopped(context, 0); ++poll) {
                ssize_t got = libssh2_sftp_read(handle, (char *)result->stdout_data + result->stdout_size,
                    options->read_bytes - result->stdout_size);
                if (got == LIBSSH2_ERROR_EAGAIN) { if (stopped(context, 1)) goto close_handle; }
                else if (got < 0) goto close_handle;
                else if (!got) { success = 1; break; }
                else { result->stdout_size += (size_t)got; result->transferred_bytes += (size_t)got; }
            }
            if (result->stdout_size == options->read_bytes) success = 1;
        } else {
            for (unsigned int poll = 0; poll < SSH_POLLS && result->transferred_bytes < options->data_size && !stopped(context, 0); ++poll) {
                ssize_t written = libssh2_sftp_write(handle, (const char *)options->data + result->transferred_bytes,
                    options->data_size - result->transferred_bytes);
                if (written == LIBSSH2_ERROR_EAGAIN || !written) { if (stopped(context, 1)) goto close_handle; }
                else if (written < 0) goto close_handle;
                else result->transferred_bytes += (size_t)written;
            }
            success = result->transferred_bytes == options->data_size;
        }
close_handle:
        do {
            if (stopped(context, 0)) { success = 0; break; }
            rc = libssh2_sftp_close(handle);
            if (rc != LIBSSH2_ERROR_EAGAIN) context->sftp_handle = NULL;
        } while (rc == LIBSSH2_ERROR_EAGAIN && !stopped(context, 1));
        if (rc) success = 0;
    }
    if (!success) return 0;
    result->remote_result_known = 1; result->status = "completed";
    result->message = "SFTP operation completed; reads are bounded ranges, writes create exclusively";
    return 1;
remote_error:
    result->sftp_error = libssh2_sftp_last_error(context->sftp);
    if (rc == LIBSSH2_ERROR_SFTP_PROTOCOL && result->sftp_error != LIBSSH2_FX_OK) {
        result->remote_result_known = 1;
        result->status = "sftp_error"; result->message = "SFTP server rejected the operation";
    }
    return 0;
}
