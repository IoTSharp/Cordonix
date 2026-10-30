#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static void write_bytes(HANDLE stream, const void *data, DWORD count) {
    DWORD written = 0;
    (void)WriteFile(stream, data, count, &written, NULL);
}

static void write_text(const wchar_t *text) {
    char converted[65536];
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, converted, sizeof(converted), NULL, NULL);
    if (count > 0) write_bytes(GetStdHandle(STD_OUTPUT_HANDLE), converted, (DWORD)count);
}

int wmain(int argc, wchar_t **argv) {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (argc < 2) return 2;
    if (wcscmp(argv[1], L"echo") == 0) {
        int index;
        for (index = 2; index < argc; ++index) write_text(argv[index]);
    } else if (wcscmp(argv[1], L"env") == 0 && argc == 3) {
        wchar_t value[32768];
        char converted[131072];
        DWORD length;
        int count;
        SetLastError(ERROR_SUCCESS);
        length = GetEnvironmentVariableW(argv[2], value, _countof(value));
        if (!length && GetLastError() == ERROR_ENVVAR_NOT_FOUND) write_bytes(output, "<absent>", 8);
        else if (length >= _countof(value)) return 3;
        else if (length) {
            count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, (int)length, converted, sizeof(converted), NULL, NULL);
            if (!count) return 3;
            write_bytes(output, converted, (DWORD)count);
        }
    } else if (wcscmp(argv[1], L"stdin") == 0) {
        unsigned char bytes[4096];
        DWORD read = 0, iteration;
        ULONGLONG deadline = GetTickCount64() + 5000;
        for (iteration = 0; iteration < 65536 && GetTickCount64() < deadline; ++iteration) {
            if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), bytes, sizeof(bytes), &read, NULL) || !read) break;
            write_bytes(output, bytes, read);
        }
    } else if (wcscmp(argv[1], L"exit23") == 0) {
        return 23;
    } else if (wcscmp(argv[1], L"sleep") == 0) {
        Sleep(8000);
    } else if (wcscmp(argv[1], L"output") == 0) {
        char chunk[4096];
        unsigned int index;
        memset(chunk, 'x', sizeof(chunk));
        for (index = 0; index < 256; ++index) write_bytes(output, chunk, sizeof(chunk));
    } else if (wcscmp(argv[1], L"streams") == 0) {
        write_bytes(output, "stdout\r\n\n", 9);
        write_bytes(GetStdHandle(STD_ERROR_HANDLE), "stderr\n", 7);
    } else if (wcscmp(argv[1], L"invalid-utf8") == 0) {
        const unsigned char bytes[] = {0x76, 0x61, 0x6c, 0x69, 0x64, 0xff, 0x0a};
        write_bytes(output, bytes, sizeof(bytes));
    } else if (wcscmp(argv[1], L"cp936") == 0) {
        const unsigned char bytes[] = {0xd6, 0xd0, 0xce, 0xc4, 0x0d, 0x0a};
        write_bytes(output, bytes, sizeof(bytes));
    } else if (wcscmp(argv[1], L"tree") == 0 || wcscmp(argv[1], L"tree-wait") == 0) {
        wchar_t module[32768], command[32768];
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        char message[64];
        ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));
        if (!GetModuleFileNameW(NULL, module, _countof(module))) return 3;
        if (swprintf_s(command, _countof(command), L"\"%ls\" sleep", module) < 0) return 3;
        if (!CreateProcessW(module, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return 4;
        sprintf_s(message, sizeof(message), "%lu", (unsigned long)pi.dwProcessId);
        write_bytes(output, message, (DWORD)strlen(message));
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        if (wcscmp(argv[1], L"tree-wait") == 0) Sleep(8000);
    } else if (wcscmp(argv[1], L"cwd") == 0) {
        wchar_t cwd[32768];
        if (!GetCurrentDirectoryW(_countof(cwd), cwd)) return 3;
        write_text(cwd);
    } else {
        return 2;
    }
    return 0;
}
