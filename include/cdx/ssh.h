#ifndef CDX_SSH_H
#define CDX_SSH_H

#include "cdx/platform.h"
#include "cdx/proxy.h"
#include <stddef.h>
#include <stdint.h>

#define CDX_SSH_MAX_SCRIPT 65536u
#define CDX_SSH_MAX_OUTPUT 262144u

typedef struct cdx_ssh_endpoint {
    const char *host;
    unsigned int port;
    const char *user;
    const char *credential;
    const char *private_key_path;
    const char *host_key_sha256;
} cdx_ssh_endpoint;

typedef struct cdx_ssh_interaction {
    const char *ready_prompt;
    const char *selection;
    const char *selection_prefix;
    const char *password_prompt;
    const char *shell_prompt;
    const char *su_credential;
    const char *su_password_prompt;
    const char *root_prompt;
} cdx_ssh_interaction;

typedef struct cdx_sftp_options {
    const char *operation;
    const char *path;
    const unsigned char *data;
    size_t data_size;
    uint64_t offset;
    size_t read_bytes;
    int allow_write;
} cdx_sftp_options;

typedef struct cdx_ssh_request {
    const char *action;
    const char *route;
    cdx_ssh_endpoint target;
    cdx_ssh_endpoint bastion;
    cdx_ssh_interaction interaction;
    const char *script;
    uint32_t timeout_ms;
    size_t output_limit_bytes;
    int require_root;
    cdx_cancel_handle cancellation;
    const cdx_sftp_options *sftp;
    const char *proxy;
} cdx_ssh_request;

typedef struct cdx_ssh_result {
    const char *status;
    const char *stage;
    const char *message;
    char host_key_sha256[65];
    char bastion_host_key_sha256[65];
    unsigned char *stdout_data;
    size_t stdout_size;
    unsigned char *stderr_data;
    size_t stderr_size;
    uint64_t elapsed_ms;
    int exit_code;
    int exit_code_known;
    int remote_command_started;
    int target_host_key_verified;
    int bastion_host_key_verified;
    int cleanup_complete;
    int remote_result_known;
    uint64_t file_size;
    unsigned long file_attribute_flags;
    unsigned long file_permissions;
    unsigned long sftp_error;
    size_t transferred_bytes;
    size_t session_allocations_remaining;
    cdx_proxy_result proxy;
} cdx_ssh_result;

void cdx_ssh_execute(const cdx_ssh_request *request, cdx_ssh_result *result);
void cdx_ssh_result_free(cdx_ssh_result *result);
#endif
