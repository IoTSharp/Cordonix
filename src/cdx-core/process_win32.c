#include "cdx/process.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CDX_COMMAND_CAP 32767u
#define CDX_PIPE_CHUNK 4096u
#define CDX_CLEANUP_MS 3000u
#define CDX_ABORT_CODE 0xe0000001u

typedef struct cdx_output_state {
    CRITICAL_SECTION lock;
    size_t stored;
    size_t limit;
    volatile LONG exceeded;
    volatile LONG io_error;
    ULONGLONG deadline;
} cdx_output_state;

typedef struct cdx_pipe_reader {
    HANDLE pipe;
    cdx_byte_buffer *target;
    cdx_output_state *state;
} cdx_pipe_reader;

typedef struct cdx_input_writer {
    HANDLE pipe;
    const unsigned char *data;
    size_t size;
    ULONGLONG deadline;
} cdx_input_writer;

typedef struct cdx_owned_process {
    HANDLE job;
    PROCESS_INFORMATION pi;
    int assigned;
    HANDLE parent_pipes[3];
    HANDLE child_pipes[3];
    HANDLE threads[3];
    cdx_pipe_reader readers[2];
    cdx_input_writer writer;
    cdx_output_state output;
    int lock_initialized;
    LPPROC_THREAD_ATTRIBUTE_LIST attributes;
    int attributes_initialized;
} cdx_owned_process;

static DWORD cdx_validate_request(const cdx_exec_request *request);
static wchar_t *cdx_build_command(const cdx_exec_request *request);
static int cdx_start(const cdx_exec_request *request, wchar_t *command, cdx_owned_process *owned, cdx_exec_result *result);
static cdx_status cdx_wait(const cdx_exec_request *request, cdx_owned_process *owned, cdx_exec_result *result, ULONGLONG deadline);
static void cdx_cleanup(cdx_owned_process *owned, cdx_exec_result *result);
static void cdx_close(cdx_owned_process *owned);

cdx_status cdx_execute(const cdx_exec_request *request, cdx_exec_result *result) {
    cdx_owned_process owned;
    wchar_t *command;
    ULONGLONG started = GetTickCount64();
    if (!result) return CDX_VALIDATION_ERROR;
    cdx_result_init(result);
    result->parent_pid = GetCurrentProcessId();
    result->win32_error = cdx_validate_request(request);
    if (result->win32_error) return result->status;
    if (request->cancel_event && WaitForSingleObject(request->cancel_event, 0) == WAIT_OBJECT_0) {
        result->status = CDX_CANCELLED;
        return result->status;
    }
    command = cdx_build_command(request);
    if (!command) {
        result->win32_error = GetLastError();
        return result->status;
    }
    ZeroMemory(&owned, sizeof(owned));
    if (cdx_start(request, command, &owned, result)) {
        result->status = cdx_wait(request, &owned, result, started + request->timeout_ms);
    } else {
        result->win32_error = GetLastError();
        result->status = CDX_SPAWN_ERROR;
    }
    cdx_cleanup(&owned, result);
    cdx_close(&owned);
    free(command);
    result->elapsed_ms = GetTickCount64() - started;
    return result->status;
}

static int cdx_absolute_path(const wchar_t *path) {
    size_t length = wcsnlen_s(path, CDX_COMMAND_CAP);
    if (length < 3 || length == CDX_COMMAND_CAP || wcschr(path, L'\r') || wcschr(path, L'\n')) return 0;
    if (path[0] == L'\\' && path[1] == L'\\') return length > 3;
    return ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
}

static DWORD cdx_validate_request(const cdx_exec_request *request) {
    size_t index, length;
    const wchar_t *name;
    DWORD attributes;
    if (!request || !request->program || !request->cwd || !cdx_absolute_path(request->program) || !cdx_absolute_path(request->cwd)) return ERROR_INVALID_PARAMETER;
    if (request->arg_count > CDX_MAX_ARGS || (request->arg_count && !request->args) || request->timeout_ms == 0 || request->timeout_ms > CDX_MAX_TIMEOUT_MS || request->output_limit_bytes == 0 || request->output_limit_bytes > CDX_MAX_OUTPUT_BYTES || request->stdin_size > CDX_MAX_OUTPUT_BYTES || (request->stdin_size && !request->stdin_utf8) || request->expected_exit_count > 32 || (request->expected_exit_count && !request->expected_exit_codes)) return ERROR_INVALID_PARAMETER;
    length = wcslen(request->program);
    if (length < 4 || _wcsicmp(request->program + length - 4, L".exe") != 0) return ERROR_BAD_EXE_FORMAT;
    name = wcsrchr(request->program, L'\\');
    { const wchar_t *slash = wcsrchr(request->program, L'/'); if (slash && (!name || slash > name)) name = slash; }
    name = name ? name + 1 : request->program;
    if (_wcsicmp(name, L"cmd.exe") == 0 || _wcsicmp(name, L"bash.exe") == 0 || _wcsicmp(name, L"sh.exe") == 0 || _wcsicmp(name, L"wsl.exe") == 0 || _wcsicmp(name, L"powershell.exe") == 0 || _wcsicmp(name, L"powershell_ise.exe") == 0) return ERROR_ACCESS_DENIED;
    for (index = 0; index < request->arg_count; ++index) {
        if (!request->args[index] || wcsnlen_s(request->args[index], CDX_COMMAND_CAP) == CDX_COMMAND_CAP) return ERROR_INVALID_PARAMETER;
    }
    if (_wcsicmp(name, L"pwsh.exe") == 0) {
        int found_file = 0;
        if (_wcsicmp(request->program, L"C:\\Program Files\\PowerShell\\7\\pwsh.exe") != 0) return ERROR_ACCESS_DENIED;
        for (index = 0; index < request->arg_count; ++index) {
            const wchar_t *arg = request->args[index];
            if (_wcsicmp(arg, L"-File") == 0) {
                if (index + 1 >= request->arg_count || !cdx_absolute_path(request->args[index + 1])) return ERROR_INVALID_PARAMETER;
                attributes = GetFileAttributesW(request->args[index + 1]);
                if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return ERROR_FILE_NOT_FOUND;
                found_file = 1;
                break;
            }
            if (_wcsicmp(arg, L"-NoProfile") != 0 && _wcsicmp(arg, L"-NoLogo") != 0 && _wcsicmp(arg, L"-NonInteractive") != 0) return ERROR_ACCESS_DENIED;
        }
        if (!found_file) return ERROR_ACCESS_DENIED;
    }
    attributes = GetFileAttributesW(request->program);
    if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError();
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) return ERROR_BAD_EXE_FORMAT;
    attributes = GetFileAttributesW(request->cwd);
    if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError();
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) return ERROR_DIRECTORY;
    if (request->cancel_event) {
        DWORD flags;
        if (!GetHandleInformation(request->cancel_event, &flags)) return ERROR_INVALID_HANDLE;
    }
    return ERROR_SUCCESS;
}

static int cdx_put(wchar_t *command, size_t *used, wchar_t value, size_t count) {
    size_t index;
    if (count >= CDX_COMMAND_CAP - *used) return 0;
    for (index = 0; index < count; ++index) command[(*used)++] = value;
    return 1;
}

static int cdx_quote_arg(wchar_t *command, size_t *used, const wchar_t *arg) {
    size_t index = 0, length = wcslen(arg);
    if (!cdx_put(command, used, L'"', 1)) return 0;
    for (; index < length; ++index) {
        size_t slashes = 0;
        for (; index < length && arg[index] == L'\\'; ++index) ++slashes;
        /* CRT consumes pairs of slashes only before quotes, including the closing quote. */
        if (index == length) {
            if (!cdx_put(command, used, L'\\', slashes * 2)) return 0;
            break;
        }
        if (!cdx_put(command, used, L'\\', arg[index] == L'"' ? slashes * 2 + 1 : slashes)) return 0;
        if (!cdx_put(command, used, arg[index], 1)) return 0;
    }
    return cdx_put(command, used, L'"', 1);
}

static wchar_t *cdx_build_command(const cdx_exec_request *request) {
    wchar_t *command = (wchar_t *)calloc(CDX_COMMAND_CAP, sizeof(wchar_t));
    size_t used = 0, index;
    if (!command) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    if (!cdx_quote_arg(command, &used, request->program)) goto too_long;
    for (index = 0; index < request->arg_count; ++index) {
        if (!cdx_put(command, &used, L' ', 1) || !cdx_quote_arg(command, &used, request->args[index])) goto too_long;
    }
    command[used] = L'\0';
    return command;
too_long:
    free(command); SetLastError(ERROR_FILENAME_EXCED_RANGE); return NULL;
}

static int cdx_pipe(HANDLE *parent, HANDLE *child, int parent_reads) {
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    HANDLE reader, writer;
    if (!CreatePipe(&reader, &writer, &sa, 0)) return 0;
    *parent = parent_reads ? reader : writer;
    *child = parent_reads ? writer : reader;
    return SetHandleInformation(*parent, HANDLE_FLAG_INHERIT, 0) != 0;
}

static DWORD WINAPI cdx_read_pipe(void *raw) {
    cdx_pipe_reader *reader = (cdx_pipe_reader *)raw;
    cdx_output_state *state = reader->state;
    unsigned char chunk[CDX_PIPE_CHUNK];
    size_t iteration;
    for (iteration = 0; iteration <= state->limit && GetTickCount64() <= state->deadline; ++iteration) {
        DWORD got = 0;
        size_t keep;
        if (!ReadFile(reader->pipe, chunk, sizeof(chunk), &got, NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_BROKEN_PIPE && error != ERROR_OPERATION_ABORTED) InterlockedExchange(&state->io_error, 1);
            break;
        }
        if (!got) break;
        EnterCriticalSection(&state->lock);
        reader->target->observed += got;
        keep = state->limit - state->stored;
        if (keep > got) keep = got;
        memcpy(reader->target->data + reader->target->size, chunk, keep);
        reader->target->size += keep;
        state->stored += keep;
        reader->target->data[reader->target->size] = 0;
        if (keep < got) InterlockedExchange(&state->exceeded, 1);
        LeaveCriticalSection(&state->lock);
        if (keep < got) break;
    }
    return 0;
}

static DWORD WINAPI cdx_write_stdin(void *raw) {
    cdx_input_writer *writer = (cdx_input_writer *)raw;
    size_t offset = 0, iteration;
    for (iteration = 0; iteration <= writer->size && offset < writer->size && GetTickCount64() <= writer->deadline; ++iteration) {
        DWORD written = 0;
        DWORD chunk = (DWORD)((writer->size - offset) > 16384 ? 16384 : writer->size - offset);
        if (!WriteFile(writer->pipe, writer->data + offset, chunk, &written, NULL) || !written) break;
        offset += written;
    }
    CloseHandle(writer->pipe);
    writer->pipe = NULL;
    return 0;
}

static int cdx_start(const cdx_exec_request *request, wchar_t *command, cdx_owned_process *owned, cdx_exec_result *result) {
    STARTUPINFOEXW si;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    SIZE_T attribute_size = 0;
    FILETIME created, exited, kernel, user;
    size_t index;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&limits, sizeof(limits));
    result->stdout_bytes.data = (unsigned char *)calloc(request->output_limit_bytes + 1, 1);
    result->stderr_bytes.data = (unsigned char *)calloc(request->output_limit_bytes + 1, 1);
    if (!result->stdout_bytes.data || !result->stderr_bytes.data) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return 0; }
    owned->job = CreateJobObjectW(NULL, NULL);
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!owned->job || !SetInformationJobObject(owned->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return 0;
    for (index = 0; index < 3; ++index) if (!cdx_pipe(&owned->parent_pipes[index], &owned->child_pipes[index], index != 0)) return 0;
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = owned->child_pipes[0];
    si.StartupInfo.hStdOutput = owned->child_pipes[1];
    si.StartupInfo.hStdError = owned->child_pipes[2];
    InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_size);
    owned->attributes = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attribute_size);
    if (!owned->attributes) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return 0; }
    if (!InitializeProcThreadAttributeList(owned->attributes, 1, 0, &attribute_size)) return 0;
    owned->attributes_initialized = 1;
    if (!UpdateProcThreadAttribute(owned->attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, owned->child_pipes, sizeof(owned->child_pipes), NULL, NULL)) return 0;
    si.lpAttributeList = owned->attributes;
    if (!CreateProcessW(request->program, command, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, NULL, request->cwd, &si.StartupInfo, &owned->pi)) return 0;
    result->pid = owned->pi.dwProcessId;
    result->cleanup_complete = 0;
    if (!AssignProcessToJobObject(owned->job, owned->pi.hProcess)) return 0;
    owned->assigned = 1;
    if (!GetProcessTimes(owned->pi.hProcess, &created, &exited, &kernel, &user)) return 0;
    result->creation_filetime = ((uint64_t)created.dwHighDateTime << 32) | created.dwLowDateTime;
    for (index = 0; index < 3; ++index) { CloseHandle(owned->child_pipes[index]); owned->child_pipes[index] = NULL; }
    InitializeCriticalSection(&owned->output.lock);
    owned->lock_initialized = 1;
    owned->output.limit = request->output_limit_bytes;
    owned->output.deadline = GetTickCount64() + request->timeout_ms + CDX_CLEANUP_MS;
    for (index = 0; index < 2; ++index) {
        owned->readers[index].pipe = owned->parent_pipes[index + 1];
        owned->readers[index].target = index == 0 ? &result->stdout_bytes : &result->stderr_bytes;
        owned->readers[index].state = &owned->output;
        owned->threads[index] = CreateThread(NULL, 0, cdx_read_pipe, &owned->readers[index], 0, NULL);
        if (!owned->threads[index]) return 0;
    }
    owned->writer.pipe = owned->parent_pipes[0];
    owned->writer.data = request->stdin_utf8;
    owned->writer.size = request->stdin_size;
    owned->writer.deadline = owned->output.deadline;
    owned->threads[2] = CreateThread(NULL, 0, cdx_write_stdin, &owned->writer, 0, NULL);
    if (!owned->threads[2]) return 0;
    owned->parent_pipes[0] = NULL;
    return ResumeThread(owned->pi.hThread) != (DWORD)-1;
}

static int cdx_expected(DWORD code, const cdx_exec_request *request) {
    size_t index;
    if (!request->expected_exit_count) return code == 0;
    for (index = 0; index < request->expected_exit_count; ++index) if ((DWORD)request->expected_exit_codes[index] == code) return 1;
    return 0;
}

static cdx_status cdx_wait(const cdx_exec_request *request, cdx_owned_process *owned, cdx_exec_result *result, ULONGLONG deadline) {
    size_t iteration;
    for (iteration = 0; iteration < request->timeout_ms / 20 + 2; ++iteration) {
        DWORD wait;
        if (InterlockedCompareExchange(&owned->output.exceeded, 0, 0)) return CDX_OUTPUT_LIMIT;
        if (request->cancel_event && WaitForSingleObject(request->cancel_event, 0) == WAIT_OBJECT_0) return CDX_CANCELLED;
        wait = WaitForSingleObject(owned->pi.hProcess, 20);
        if (wait == WAIT_OBJECT_0) {
            if (!GetExitCodeProcess(owned->pi.hProcess, &result->exit_code)) { result->win32_error = GetLastError(); return CDX_EXECUTION_ERROR; }
            result->exit_code_known = 1;
            return cdx_expected(result->exit_code, request) ? CDX_COMPLETED : CDX_FAILED;
        }
        if (wait == WAIT_FAILED) { result->win32_error = GetLastError(); return CDX_EXECUTION_ERROR; }
        if (GetTickCount64() >= deadline) return CDX_TIMED_OUT;
    }
    return CDX_TIMED_OUT;
}

static void cdx_cleanup(cdx_owned_process *owned, cdx_exec_result *result) {
    ULONGLONG deadline = GetTickCount64() + CDX_CLEANUP_MS;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info;
    size_t index;
    ZeroMemory(&info, sizeof(info));
    if (owned->assigned) {
        if (QueryInformationJobObject(owned->job, JobObjectBasicAccountingInformation, &info, sizeof(info), NULL)) {
            result->job_processes_total = info.TotalProcesses;
            result->job_active_before_cleanup = info.ActiveProcesses;
        }
        TerminateJobObject(owned->job, CDX_ABORT_CODE);
        for (index = 0; index < CDX_CLEANUP_MS / 10 && GetTickCount64() < deadline; ++index) {
            if (!QueryInformationJobObject(owned->job, JobObjectBasicAccountingInformation, &info, sizeof(info), NULL)) break;
            result->job_active_after_cleanup = info.ActiveProcesses;
            if (!info.ActiveProcesses) { result->cleanup_complete = 1; break; }
            Sleep(10);
        }
    } else if (owned->pi.hProcess) {
        /* Assignment can fail while the root is suspended, so that handle must be stopped directly. */
        TerminateProcess(owned->pi.hProcess, CDX_ABORT_CODE);
        result->cleanup_complete = WaitForSingleObject(owned->pi.hProcess, 2000) == WAIT_OBJECT_0;
    }
    if (owned->pi.hProcess && !result->exit_code_known && WaitForSingleObject(owned->pi.hProcess, 0) == WAIT_OBJECT_0) result->exit_code_known = GetExitCodeProcess(owned->pi.hProcess, &result->exit_code) != 0;
    for (index = 0; index < 3; ++index) {
        if (owned->threads[index]) {
            DWORD wait = WaitForSingleObject(owned->threads[index], 1000);
            if (wait == WAIT_TIMEOUT) {
                CancelSynchronousIo(owned->threads[index]);
                wait = WaitForSingleObject(owned->threads[index], 1000);
            }
            if (wait != WAIT_OBJECT_0) result->cleanup_complete = 0;
        }
    }
    result->output_truncated = InterlockedCompareExchange(&owned->output.exceeded, 0, 0) != 0;
    if (result->output_truncated && (result->status == CDX_COMPLETED || result->status == CDX_FAILED)) result->status = CDX_OUTPUT_LIMIT;
    if (InterlockedCompareExchange(&owned->output.io_error, 0, 0) && result->status == CDX_COMPLETED) result->status = CDX_EXECUTION_ERROR;
}

static void cdx_close(cdx_owned_process *owned) {
    size_t index;
    if (owned->job) CloseHandle(owned->job);
    for (index = 0; index < 3; ++index) {
        if (owned->threads[index]) CloseHandle(owned->threads[index]);
        if (owned->parent_pipes[index]) CloseHandle(owned->parent_pipes[index]);
        if (owned->child_pipes[index]) CloseHandle(owned->child_pipes[index]);
    }
    if (owned->pi.hThread) CloseHandle(owned->pi.hThread);
    if (owned->pi.hProcess) CloseHandle(owned->pi.hProcess);
    if (owned->attributes_initialized) DeleteProcThreadAttributeList(owned->attributes);
    if (owned->attributes) HeapFree(GetProcessHeap(), 0, owned->attributes);
    if (owned->lock_initialized) DeleteCriticalSection(&owned->output.lock);
}
