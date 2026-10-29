#include "cdx/filesystem.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CDX_FS_PATH_CAP 32768u
#define CDX_FS_ENUMERATION_STEPS 100000u
#define CDX_FS_QUERY_MS 5000u

static int valid_request(const cdx_fs_request *request);
static int valid_path(const wchar_t *path);
static cdx_fs_status status_from_error(DWORD error);
static cdx_fs_status checkpoint(cdx_cancel_handle cancellation, ULONGLONG deadline);
static cdx_fs_status utf8_text(const wchar_t *text, char **output);
static cdx_fs_kind kind_from_attributes(DWORD attributes);
static cdx_fs_status list_entries(const cdx_fs_request *request, const wchar_t *path,
                                 ULONGLONG deadline, cdx_fs_result *result);

cdx_fs_status cdx_fs_query(const cdx_fs_request *request, cdx_fs_result *result) {
    ULONGLONG deadline = GetTickCount64() + CDX_FS_QUERY_MS;
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    wchar_t *full_path = NULL;
    cdx_fs_status status;
    if (!result) return CDX_FS_VALIDATION_ERROR;
    memset(result, 0, sizeof(*result));
    if (!valid_request(request)) {
        result->status = CDX_FS_VALIDATION_ERROR;
        result->native_error = ERROR_INVALID_PARAMETER;
        return result->status;
    }
    status = cdx_fs_normalize_path(request->path, request->cancellation, &full_path, &result->native_error);
    if (status != CDX_FS_OK) return result->status = status;
    if (!GetFileAttributesExW(full_path, GetFileExInfoStandard, &attributes)) {
        status = status_from_error(GetLastError()); goto done;
    }
    status = checkpoint(request->cancellation, deadline);
    if (status != CDX_FS_OK) goto done;
    result->kind = kind_from_attributes(attributes.dwFileAttributes);
    status = utf8_text(full_path, &result->path);
    if (status != CDX_FS_OK) goto done;
    if (request->list_directory) {
        if (!(attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            SetLastError(ERROR_DIRECTORY); status = CDX_FS_NOT_DIRECTORY; goto done;
        }
        status = list_entries(request, full_path, deadline, result);
    }
done:
    result->status = status;
    if (status != CDX_FS_OK) result->native_error = GetLastError();
    free(full_path);
    return status;
}

static int valid_request(const cdx_fs_request *request) {
    if (!request || (request->list_directory != 0 && request->list_directory != 1) ||
        (request->include_hidden != 0 && request->include_hidden != 1) ||
        (request->list_directory && (request->max_entries == 0 || request->max_entries > CDX_FS_MAX_ENTRIES))) return 0;
    return 1;
}

static int valid_path(const wchar_t *path) {
    size_t length;
    if (!path) return 0;
    length = wcsnlen_s(path, CDX_FS_PATH_CAP);
    if (length < 3 || length == CDX_FS_PATH_CAP || wcspbrk(path, L"\r\n`") ||
        wcsstr(path, L"$(") || wcsstr(path, L"${")) return 0;
    if (path[0] == L'\\' && path[1] == L'\\') return length > 3;
    return ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
        path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
}

cdx_fs_status cdx_fs_normalize_path(const wchar_t *path, cdx_cancel_handle cancellation,
                                    wchar_t **normalized, unsigned long *native_error) {
    ULONGLONG deadline = GetTickCount64() + CDX_FS_QUERY_MS;
    wchar_t *full_path = NULL;
    DWORD capacity, length;
    cdx_fs_status status;
    if (normalized) *normalized = NULL;
    if (native_error) *native_error = ERROR_SUCCESS;
    if (!normalized || !valid_path(path)) {
        SetLastError(ERROR_INVALID_PARAMETER); status = CDX_FS_VALIDATION_ERROR; goto done;
    }
    status = checkpoint(cancellation, deadline);
    if (status != CDX_FS_OK) goto done;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, NULL, 0, NULL, NULL)) {
        status = status_from_error(GetLastError()); goto done;
    }
    capacity = GetFullPathNameW(path, 0, NULL, NULL);
    if (!capacity) { status = status_from_error(GetLastError()); goto done; }
    if (capacity > CDX_FS_PATH_CAP) { SetLastError(ERROR_FILENAME_EXCED_RANGE); status = CDX_FS_VALIDATION_ERROR; goto done; }
    full_path = (wchar_t *)malloc((size_t)capacity * sizeof(*full_path));
    if (!full_path) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); status = CDX_FS_IO_ERROR; goto done; }
    length = GetFullPathNameW(path, capacity, full_path, NULL);
    if (!length) { status = status_from_error(GetLastError()); goto done; }
    if (length >= capacity) { SetLastError(ERROR_FILENAME_EXCED_RANGE); status = CDX_FS_VALIDATION_ERROR; goto done; }
    status = checkpoint(cancellation, deadline);
    if (status != CDX_FS_OK) goto done;
    *normalized = full_path;
    full_path = NULL;
done:
    if (status != CDX_FS_OK && native_error) *native_error = GetLastError();
    free(full_path);
    return status;
}

int cdx_fs_path_within(const wchar_t *path, const wchar_t *root) {
    size_t path_length = wcslen(path), root_length = wcslen(root), offset;
    while (root_length && (root[root_length - 1] == L'\\' || root[root_length - 1] == L'/')) --root_length;
    if (!root_length || path_length < root_length) return 0;
    for (offset = 0; offset < root_length; ) {
        size_t end = offset;
        if (root[offset] == L'\\' || root[offset] == L'/') {
            if (path[offset] != L'\\' && path[offset] != L'/') return 0;
            ++offset;
            continue;
        }
        while (end < root_length && root[end] != L'\\' && root[end] != L'/') ++end;
        if (CompareStringOrdinal(path + offset, (int)(end - offset), root + offset, (int)(end - offset), TRUE) != CSTR_EQUAL) return 0;
        offset = end;
    }
    return path_length == root_length || path[root_length] == L'\\' || path[root_length] == L'/';
}

static cdx_fs_status status_from_error(DWORD error) {
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
        return CDX_FS_NOT_FOUND;
    case ERROR_DIRECTORY:
        return CDX_FS_NOT_DIRECTORY;
    case ERROR_ACCESS_DENIED:
    case ERROR_NETWORK_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD:
        return CDX_FS_PERMISSION_DENIED;
    case ERROR_NO_UNICODE_TRANSLATION:
        return CDX_FS_ENCODING_ERROR;
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
    case ERROR_FILENAME_EXCED_RANGE:
        return CDX_FS_VALIDATION_ERROR;
    default:
        return CDX_FS_IO_ERROR;
    }
}

static cdx_fs_status checkpoint(cdx_cancel_handle cancellation, ULONGLONG deadline) {
    if (cancellation) {
        DWORD wait = WaitForSingleObject(cancellation, 0);
        if (wait == WAIT_OBJECT_0) { SetLastError(ERROR_CANCELLED); return CDX_FS_CANCELLED; }
        if (wait == WAIT_FAILED) return CDX_FS_IO_ERROR;
    }
    if (GetTickCount64() >= deadline) { SetLastError(ERROR_TIMEOUT); return CDX_FS_TIMED_OUT; }
    return CDX_FS_OK;
}

static cdx_fs_status utf8_text(const wchar_t *text, char **output) {
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, NULL, 0, NULL, NULL);
    char *buffer;
    if (!count) return status_from_error(GetLastError());
    buffer = (char *)malloc((size_t)count);
    if (!buffer) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return CDX_FS_IO_ERROR; }
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, buffer, count, NULL, NULL)) {
        DWORD error = GetLastError(); free(buffer); SetLastError(error); return status_from_error(error);
    }
    *output = buffer;
    return CDX_FS_OK;
}

static cdx_fs_kind kind_from_attributes(DWORD attributes) {
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return CDX_FS_LINK;
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) return CDX_FS_DIRECTORY;
    if (attributes & FILE_ATTRIBUTE_DEVICE) return CDX_FS_OTHER;
    return CDX_FS_FILE;
}

static cdx_fs_status list_entries(const cdx_fs_request *request, const wchar_t *path,
                                 ULONGLONG deadline, cdx_fs_result *result) {
    WIN32_FIND_DATAW data;
    size_t length = wcslen(path), step;
    wchar_t *pattern;
    HANDLE find = INVALID_HANDLE_VALUE;
    cdx_fs_status status = CDX_FS_OK;
    DWORD error = ERROR_SUCCESS;
    if (length + 3u > CDX_FS_PATH_CAP) { SetLastError(ERROR_FILENAME_EXCED_RANGE); return CDX_FS_VALIDATION_ERROR; }
    pattern = (wchar_t *)malloc((length + 3u) * sizeof(*pattern));
    result->entries = (cdx_fs_entry *)calloc(request->max_entries, sizeof(*result->entries));
    if (!pattern || !result->entries) { free(pattern); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return CDX_FS_IO_ERROR; }
    memcpy(pattern, path, length * sizeof(*pattern));
    if (path[length - 1] != L'\\' && path[length - 1] != L'/') pattern[length++] = L'\\';
    pattern[length++] = L'*'; pattern[length] = 0;
    find = FindFirstFileExW(pattern, FindExInfoBasic, &data, FindExSearchNameMatch, NULL, 0);
    free(pattern);
    if (find == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) return checkpoint(request->cancellation, deadline);
        return status_from_error(error);
    }
    for (step = 0; step < CDX_FS_ENUMERATION_STEPS; ++step) {
        cdx_fs_entry *entry;
        status = checkpoint(request->cancellation, deadline);
        if (status != CDX_FS_OK) { error = GetLastError(); break; }
        if (wcscmp(data.cFileName, L".") && wcscmp(data.cFileName, L"..") &&
            (request->include_hidden || !(data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))) {
            if (result->entry_count == request->max_entries) { result->truncated = 1; break; }
            entry = &result->entries[result->entry_count];
            status = utf8_text(data.cFileName, &entry->name);
            if (status != CDX_FS_OK) { error = GetLastError(); break; }
            entry->kind = kind_from_attributes(data.dwFileAttributes);
            entry->hidden = (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
            entry->system = (data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;
            entry->size_known = entry->kind == CDX_FS_FILE;
            if (entry->size_known) entry->size_bytes = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
            ++result->entry_count;
        }
        if (!FindNextFileW(find, &data)) {
            error = GetLastError();
            if (error == ERROR_NO_MORE_FILES) { error = ERROR_SUCCESS; status = checkpoint(request->cancellation, deadline); if (status != CDX_FS_OK) error = GetLastError(); }
            else status = status_from_error(error);
            break;
        }
    }
    if (step == CDX_FS_ENUMERATION_STEPS) { status = CDX_FS_TIMED_OUT; error = ERROR_TIMEOUT; }
    if (!FindClose(find) && status == CDX_FS_OK) { error = GetLastError(); status = CDX_FS_IO_ERROR; }
    if (status != CDX_FS_OK) SetLastError(error);
    return status;
}
