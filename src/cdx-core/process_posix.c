#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "cdx/process.h"

#ifndef _WIN32

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CDX_POSIX_PATH_CAP 4096u
#define CDX_POSIX_PIPE_CHUNK 4096u
#define CDX_POSIX_CLEANUP_MS 3000u
#define CDX_POSIX_POLL_MS 20
#define CDX_POSIX_MAX_POLLS 180000u
#define CDX_POSIX_MAX_PROC_SCAN 4096u

static uint64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static uint64_t realtime_filetime(void) {
    struct timespec now;
    uint64_t seconds;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) return 0;
    /* Keep the public field's Windows FILETIME unit while remaining portable. */
    seconds = (uint64_t)now.tv_sec + 11644473600ull;
    return seconds * 10000000ull + (uint64_t)now.tv_nsec / 100u;
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static int absolute_path(const char *path) {
    size_t length;
    if (!path) return 0;
    length = strnlen(path, CDX_POSIX_PATH_CAP);
    return length >= 1u && length < CDX_POSIX_PATH_CAP && path[0] == '/' &&
        !strchr(path, '\n') && !strchr(path, '\r');
}

static int shell_basename(const char *path) {
    const char *name = strrchr(path, '/');
    static const char *const shells[] = {
        "sh", "bash", "dash", "zsh", "fish", "cmd", "cmd.exe",
        "wsl", "wsl.exe", "powershell", "powershell.exe", "pwsh", "pwsh.exe"
    };
    size_t index;
    name = name ? name + 1 : path;
    for (index = 0; index < sizeof(shells) / sizeof(shells[0]); ++index)
        if (strcmp(name, shells[index]) == 0) return 1;
    return 0;
}

static int validate_request(const cdx_exec_request *request, int *error_code) {
    struct stat program_stat, cwd_stat;
    size_t index;
    if (!request || !request->program || !request->cwd ||
        !absolute_path(request->program) || !absolute_path(request->cwd)) {
        *error_code = EINVAL;
        return 0;
    }
    if (request->arg_count > CDX_MAX_ARGS ||
        (request->arg_count && !request->args) || request->timeout_ms == 0 ||
        request->timeout_ms > CDX_MAX_TIMEOUT_MS || request->output_limit_bytes == 0 ||
        request->output_limit_bytes > CDX_MAX_OUTPUT_BYTES ||
        request->stdin_size > CDX_MAX_OUTPUT_BYTES ||
        (request->stdin_size && !request->stdin_utf8) || request->expected_exit_count > 32u ||
        (request->expected_exit_count && !request->expected_exit_codes)) {
        *error_code = EINVAL;
        return 0;
    }
    if (shell_basename(request->program)) { *error_code = EACCES; return 0; }
    if (stat(request->program, &program_stat) != 0) { *error_code = errno; return 0; }
    if (!S_ISREG(program_stat.st_mode)) { *error_code = EACCES; return 0; }
    if (access(request->program, X_OK) != 0) { *error_code = errno; return 0; }
    if (stat(request->cwd, &cwd_stat) != 0) { *error_code = errno; return 0; }
    if (!S_ISDIR(cwd_stat.st_mode)) { *error_code = ENOTDIR; return 0; }
    for (index = 0; index < request->arg_count; ++index) {
        if (!request->args[index] || strnlen(request->args[index], CDX_POSIX_PATH_CAP) == CDX_POSIX_PATH_CAP) {
            *error_code = EINVAL;
            return 0;
        }
    }
    *error_code = 0;
    return 1;
}

static int expected_exit(uint32_t code, const cdx_exec_request *request) {
    size_t index;
    if (!request->expected_exit_count) return code == 0u;
    for (index = 0; index < request->expected_exit_count; ++index)
        if ((uint32_t)request->expected_exit_codes[index] == code) return 1;
    return 0;
}

static int count_process_group(pid_t group, uint32_t *total, uint32_t *active) {
    DIR *proc;
    struct dirent *entry;
    uint32_t scan = 0;
    int complete = 0;
    uint64_t deadline = monotonic_ms() + 100u;
    *total = 0; *active = 0;
    proc = opendir("/proc");
    if (!proc) return 0;
    while (scan++ < CDX_POSIX_MAX_PROC_SCAN && monotonic_ms() <= deadline) {
        char path[128], line[512];
        FILE *file;
        char state = 0;
        long pid_value = 0, parent = 0, pgrp = 0;
        char *end;
        errno = 0;
        entry = readdir(proc);
        if (!entry) { complete = errno == 0; break; }
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        pid_value = strtol(entry->d_name, &end, 10);
        if (!pid_value || *end != '\0') continue;
        (void)snprintf(path, sizeof(path), "/proc/%ld/stat", pid_value);
        file = fopen(path, "r");
        if (!file) { if (errno != ENOENT && errno != ESRCH) break; continue; }
        if (!fgets(line, sizeof(line), file)) { fclose(file); continue; }
        fclose(file);
        /* The command name may contain spaces; parse only fields after ')'. */
        end = strrchr(line, ')');
        if (!end || sscanf(end + 2, "%c %ld %ld", &state, &parent, &pgrp) != 3) continue;
        if ((pid_t)pgrp == group) {
            ++*total;
            if (state != 'Z' && state != 'X') ++*active;
        }
    }
    closedir(proc);
    return complete;
}

static void close_fd(int *fd) {
    if (*fd >= 0) { (void)close(*fd); *fd = -1; }
}

static void kill_group(pid_t pid) {
    if (pid <= 0) return;
    if (kill(-pid, SIGKILL) != 0 && errno != ESRCH) (void)kill(pid, SIGKILL);
}

static int append_output(cdx_byte_buffer *buffer, const unsigned char *data, size_t length,
                         size_t limit, size_t *total, int *exceeded) {
    size_t keep = *total < limit ? limit - *total : 0;
    buffer->observed += length;
    if (keep > length) keep = length;
    if (keep) memcpy(buffer->data + buffer->size, data, keep);
    buffer->size += keep;
    buffer->data[buffer->size] = 0;
    *total += keep;
    if (keep < length) *exceeded = 1;
    return keep == length;
}

static int read_output_fd(int *fd, cdx_byte_buffer *buffer, size_t limit, size_t *total, int *exceeded) {
    unsigned char chunk[CDX_POSIX_PIPE_CHUNK];
    size_t iteration;
    for (iteration = 0; iteration < 32u; ++iteration) {
        ssize_t got = read(*fd, chunk, sizeof(chunk));
        if (got > 0) {
            (void)append_output(buffer, chunk, (size_t)got, limit, total, exceeded);
            if (*exceeded) return 0;
            continue;
        }
        if (got == 0) { close_fd(fd); return 0; }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 1;
        close_fd(fd); return 0;
    }
    return 1;
}

static void child_fail(int fd, int error_code) {
    ssize_t ignored = write(fd, &error_code, sizeof(error_code));
    (void)ignored;
    _exit(127);
}

static ssize_t write_stdin(int fd, const void *data, size_t length) {
    sigset_t blocked, old_mask, pending;
    struct timespec no_wait = {0, 0};
    ssize_t written;
    int had_pending, error_code;
    sigemptyset(&blocked); sigaddset(&blocked, SIGPIPE);
    error_code = pthread_sigmask(SIG_BLOCK, &blocked, &old_mask);
    if (error_code) { errno = error_code; return -1; }
    if (sigpending(&pending) != 0) {
        error_code = errno; (void)pthread_sigmask(SIG_SETMASK, &old_mask, NULL);
        errno = error_code; return -1;
    }
    had_pending = sigismember(&pending, SIGPIPE);
    written = write(fd, data, length);
    error_code = errno;
    /* Consume only this write's SIGPIPE, preserving a caller's pending signal. */
    if (written < 0 && error_code == EPIPE && !had_pending) (void)sigtimedwait(&blocked, NULL, &no_wait);
    (void)pthread_sigmask(SIG_SETMASK, &old_mask, NULL);
    errno = error_code;
    return written;
}

cdx_status cdx_execute(const cdx_exec_request *request, cdx_exec_result *result) {
    int validation_error = 0;
    int input_pipe[2] = {-1, -1}, output_pipe[2] = {-1, -1}, error_pipe[2] = {-1, -1}, exec_pipe[2] = {-1, -1};
    int stdin_fd = -1, stdout_fd = -1, stderr_fd = -1, exec_fd = -1;
    int status = 0, child_finished = 0, child_reaped = 0, wait_failed = 0, exec_error = 0, exec_error_known = 0;
    int kill_sent = 0, output_exceeded = 0, cleanup_complete = 1, reason = 0;
    size_t input_offset = 0, output_total = 0;
    uint32_t group_total = 0, group_active = 0;
    int group_known = 0;
    pid_t child = -1;
    char **argv = NULL;
    uint64_t started, deadline, cleanup_deadline = 0;
    uint32_t polls = 0;

    if (!result) return CDX_VALIDATION_ERROR;
    cdx_result_init(result);
    result->parent_pid = (uint32_t)getpid();
    result->posix_error = 0;
    if (!validate_request(request, &validation_error)) {
        result->posix_error = validation_error;
        return result->status;
    }
    if (request->cancel_event && atomic_load_explicit(request->cancel_event, memory_order_acquire)) {
        result->status = CDX_CANCELLED;
        return result->status;
    }
    result->stdout_bytes.data = (unsigned char *)calloc(request->output_limit_bytes + 1u, 1u);
    result->stderr_bytes.data = (unsigned char *)calloc(request->output_limit_bytes + 1u, 1u);
    argv = (char **)calloc(request->arg_count + 2u, sizeof(*argv));
    if (!result->stdout_bytes.data || !result->stderr_bytes.data || !argv) {
        result->posix_error = ENOMEM;
        result->status = CDX_EXECUTION_ERROR;
        goto done;
    }
    argv[0] = (char *)request->program;
    for (size_t index = 0; index < request->arg_count; ++index) argv[index + 1u] = (char *)request->args[index];
    if (pipe2(input_pipe, O_CLOEXEC) != 0 || pipe2(output_pipe, O_CLOEXEC) != 0 ||
        pipe2(error_pipe, O_CLOEXEC) != 0 || pipe2(exec_pipe, O_CLOEXEC) != 0) {
        result->posix_error = errno;
        result->status = CDX_SPAWN_ERROR;
        goto done;
    }
    child = fork();
    if (child < 0) {
        result->posix_error = errno;
        result->status = CDX_SPAWN_ERROR;
        goto done;
    }
    if (child == 0) {
        int error_code;
        if (setpgid(0, 0) != 0) child_fail(exec_pipe[1], errno);
        if (chdir(request->cwd) != 0) child_fail(exec_pipe[1], errno);
        if (dup2(input_pipe[0], STDIN_FILENO) < 0 || dup2(output_pipe[1], STDOUT_FILENO) < 0 || dup2(error_pipe[1], STDERR_FILENO) < 0) child_fail(exec_pipe[1], errno);
        close(input_pipe[0]); close(input_pipe[1]); close(output_pipe[0]); close(output_pipe[1]); close(error_pipe[0]); close(error_pipe[1]); close(exec_pipe[0]);
        execv(request->program, argv);
        error_code = errno;
        child_fail(exec_pipe[1], error_code);
    }
    result->pid = (uint32_t)child;
    result->creation_filetime = realtime_filetime();
    if (setpgid(child, child) != 0 && errno != EACCES && errno != ESRCH) {
        result->posix_error = errno;
        reason = 3;
    }
    close_fd(&input_pipe[0]); close_fd(&output_pipe[1]); close_fd(&error_pipe[1]); close_fd(&exec_pipe[1]);
    stdin_fd = input_pipe[1]; input_pipe[1] = -1;
    stdout_fd = output_pipe[0]; output_pipe[0] = -1;
    stderr_fd = error_pipe[0]; error_pipe[0] = -1;
    exec_fd = exec_pipe[0]; exec_pipe[0] = -1;
    if (!set_nonblocking(stdout_fd) || !set_nonblocking(stderr_fd) || !set_nonblocking(exec_fd) || (request->stdin_size && !set_nonblocking(stdin_fd))) {
        result->posix_error = errno;
        reason = 3;
    }
    if (!request->stdin_size) close_fd(&stdin_fd);
    started = monotonic_ms();
    deadline = started + request->timeout_ms;
    while (polls++ < CDX_POSIX_MAX_POLLS) {
        struct pollfd descriptors[4] = {{0}};
        int descriptor_kind[4];
        nfds_t descriptor_count = 0;
        uint64_t now = monotonic_ms();
        int poll_timeout;
        if (!child_finished && !wait_failed) {
            siginfo_t info;
            memset(&info, 0, sizeof(info));
            /* Keep the root waitable until group cleanup, so its PID cannot be reused. */
            if (waitid(P_PID, (id_t)child, &info, WEXITED | WNOHANG | WNOWAIT) == 0) {
                if (info.si_pid == child) child_finished = 1;
            } else if (errno != EINTR) {
                result->posix_error = errno; cleanup_complete = 0;
                wait_failed = 1; reason = 3;
            }
        }
        if (!reason && !child_finished && now >= deadline) reason = 1;
        if (!reason && request->cancel_event && atomic_load_explicit(request->cancel_event, memory_order_acquire)) reason = 2;
        if ((reason || child_finished) && !kill_sent) {
            group_known = count_process_group(child, &group_total, &group_active);
            result->job_processes_total = group_total;
            result->job_active_before_cleanup = group_active;
            if (!group_known) cleanup_complete = 0;
            kill_group(child); kill_sent = 1; cleanup_deadline = now + CDX_POSIX_CLEANUP_MS;
            close_fd(&stdin_fd);
        }
        if (kill_sent && now >= cleanup_deadline) { cleanup_complete = 0; break; }
        if (stdout_fd >= 0) { descriptors[descriptor_count].fd = stdout_fd; descriptors[descriptor_count].events = POLLIN; descriptor_kind[descriptor_count++] = 1; }
        if (stderr_fd >= 0) { descriptors[descriptor_count].fd = stderr_fd; descriptors[descriptor_count].events = POLLIN; descriptor_kind[descriptor_count++] = 2; }
        if (stdin_fd >= 0 && !reason) { descriptors[descriptor_count].fd = stdin_fd; descriptors[descriptor_count].events = POLLOUT; descriptor_kind[descriptor_count++] = 3; }
        if (exec_fd >= 0) { descriptors[descriptor_count].fd = exec_fd; descriptors[descriptor_count].events = POLLIN; descriptor_kind[descriptor_count++] = 4; }
        if (!descriptor_count && child_finished) break;
        {
            uint64_t wait_until = kill_sent ? cleanup_deadline : deadline;
            uint64_t remaining = wait_until > now ? wait_until - now : 0;
            poll_timeout = remaining < CDX_POSIX_POLL_MS ? (int)remaining : CDX_POSIX_POLL_MS;
        }
        if (poll(descriptors, descriptor_count, poll_timeout) < 0) {
            if (errno == EINTR) continue;
            result->posix_error = errno; cleanup_complete = 0; reason = 3; continue;
        }
        for (nfds_t index = 0; index < descriptor_count; ++index) {
            short events = descriptors[index].revents;
            if (!events) continue;
            if (descriptor_kind[index] == 1) (void)read_output_fd(&stdout_fd, &result->stdout_bytes, request->output_limit_bytes, &output_total, &output_exceeded);
            else if (descriptor_kind[index] == 2) (void)read_output_fd(&stderr_fd, &result->stderr_bytes, request->output_limit_bytes, &output_total, &output_exceeded);
            else if (descriptor_kind[index] == 3) {
                ssize_t written = write_stdin(stdin_fd, request->stdin_utf8 + input_offset, request->stdin_size - input_offset);
                if (written > 0) input_offset += (size_t)written;
                else if (written < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) close_fd(&stdin_fd);
                if (input_offset >= request->stdin_size) close_fd(&stdin_fd);
            } else if (descriptor_kind[index] == 4) {
                ssize_t got = read(exec_fd, &exec_error, sizeof(exec_error));
                if (got == (ssize_t)sizeof(exec_error)) { exec_error_known = 1; result->posix_error = exec_error; }
                if (got == 0 || got == (ssize_t)sizeof(exec_error) || (got < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) close_fd(&exec_fd);
            }
        }
        if (output_exceeded && !reason) reason = 4;
        if (child_finished && stdout_fd < 0 && stderr_fd < 0 && exec_fd < 0) break;
    }
    if (!kill_sent) {
        if (!reason) reason = 1;
        kill_group(child); cleanup_deadline = monotonic_ms() + CDX_POSIX_CLEANUP_MS;
    }
    if (!wait_failed) {
        for (unsigned int wait_iteration = 0; wait_iteration < CDX_POSIX_CLEANUP_MS / 10u &&
             (wait_iteration == 0 || monotonic_ms() < cleanup_deadline); ++wait_iteration) {
            pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child) { child_reaped = 1; break; }
            if (waited < 0 && errno != EINTR) { result->posix_error = errno; reason = 3; break; }
            struct timespec pause_for = {0, 10000000L};
            (void)nanosleep(&pause_for, NULL);
        }
    }
    if (child_reaped) {
        result->exit_code_known = WIFEXITED(status) || WIFSIGNALED(status);
        result->exit_code = WIFEXITED(status) ? (uint32_t)WEXITSTATUS(status) : (uint32_t)(128 + WTERMSIG(status));
    }
    if (exec_error_known) result->status = CDX_SPAWN_ERROR;
    else if (output_exceeded) result->status = CDX_OUTPUT_LIMIT;
    else if (reason == 1) result->status = CDX_TIMED_OUT;
    else if (reason == 2) result->status = CDX_CANCELLED;
    else if (reason == 3) result->status = CDX_EXECUTION_ERROR;
    else if (!child_reaped) result->status = CDX_TIMED_OUT;
    else result->status = expected_exit(result->exit_code, request) ? CDX_COMPLETED : CDX_FAILED;
    for (unsigned int cleanup_iteration = 0; cleanup_iteration < CDX_POSIX_CLEANUP_MS / 10u; ++cleanup_iteration) {
        struct timespec pause_for = {0, 10000000L};
        group_known = count_process_group(child, &group_total, &group_active);
        if (!group_known || !group_active || monotonic_ms() >= cleanup_deadline) break;
        (void)nanosleep(&pause_for, NULL);
    }
    result->job_active_after_cleanup = group_active;
    if (!group_known || group_active != 0) cleanup_complete = 0;
    if (!child_reaped) cleanup_complete = 0;
    result->cleanup_complete = cleanup_complete;
    result->output_truncated = output_exceeded;
    result->elapsed_ms = monotonic_ms() - started;

done:
    close_fd(&input_pipe[0]); close_fd(&input_pipe[1]); close_fd(&output_pipe[0]); close_fd(&output_pipe[1]);
    close_fd(&error_pipe[0]); close_fd(&error_pipe[1]); close_fd(&exec_pipe[0]); close_fd(&exec_pipe[1]);
    close_fd(&stdin_fd); close_fd(&stdout_fd); close_fd(&stderr_fd); close_fd(&exec_fd);
    free(argv);
    return result->status;
}

#endif
