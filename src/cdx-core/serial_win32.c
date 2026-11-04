#define WIN32_LEAN_AND_MEAN
#include "cdx/serial.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define SERIAL_POLLS 12000u
#define SERIAL_QUEUE_POLLS 6000u

typedef struct serial_context {
    const cdx_serial_request *request;
    cdx_serial_result *result;
    uint64_t deadline;
    HANDLE port;
    wchar_t port_name[16], device_name[32];
} serial_context;

typedef struct serial_io {
    OVERLAPPED overlapped;
    unsigned char buffer[4096];
} serial_io;

static int prepare(serial_context *context);
static int stopped(serial_context *context);
static void list_ports(serial_context *context);
static int diagnose_mapping(serial_context *context);
static int open_port(serial_context *context);
static void inspect_port(serial_context *context);
static void exchange(serial_context *context);

void cdx_serial_execute(const cdx_serial_request *request, cdx_serial_result *result) {
    serial_context context;
    uint64_t started = GetTickCount64();
    if (!result) return;
    memset(result, 0, sizeof(*result)); memset(&context, 0, sizeof(context));
    result->status = "validation_error"; result->stop_reason = "validation";
    result->message = "Use COM1..COM65535, bounded options and an explicit allow_send flag for transmitted bytes";
    result->cleanup_complete = 1;
    context.request = request; context.result = result; context.port = INVALID_HANDLE_VALUE;
    if (!prepare(&context)) return;
    context.deadline = started + request->timeout_ms;
    if (stopped(&context)) goto done;
    if (!strcmp(request->action, "list")) list_ports(&context);
    else if (!strcmp(request->action, "diagnose")) {
        if (diagnose_mapping(&context) && request->open_probe && open_port(&context)) inspect_port(&context);
    } else if (open_port(&context)) exchange(&context);
done:
    if (context.port != INVALID_HANDLE_VALUE && !CloseHandle(context.port)) result->cleanup_complete = 0;
    result->elapsed_ms = GetTickCount64() - started;
}

void cdx_serial_result_free(cdx_serial_result *result) {
    if (!result) return;
    free(result->rx_data); memset(result, 0, sizeof(*result));
}

static int port_number(const char *text, unsigned int *number) {
    size_t length = text ? strlen(text) : 0;
    unsigned int value = 0;
    if (length < 4u || length > 8u || (text[0] != 'C' && text[0] != 'c') ||
        (text[1] != 'O' && text[1] != 'o') || (text[2] != 'M' && text[2] != 'm') || text[3] == '0') return 0;
    for (size_t i = 3u; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9') return 0;
        value = value * 10u + (unsigned int)(text[i] - '0');
    }
    if (!value || value > 65535u) return 0;
    *number = value; return 1;
}

static int prepare(serial_context *context) {
    const cdx_serial_request *request = context->request;
    unsigned int number;
    if (!request || !request->action || !request->port ||
        (strcmp(request->action, "list") && strcmp(request->action, "diagnose") && strcmp(request->action, "exchange")) ||
        !request->timeout_ms || request->timeout_ms > 60000u || !request->read_idle_ms || request->read_idle_ms > 5000u ||
        request->read_max_bytes > CDX_SERIAL_MAX_BYTES || !request->max_entries || request->max_entries > CDX_SERIAL_MAX_PORTS ||
        request->tx_size > CDX_SERIAL_MAX_BYTES || (request->tx_size && (!request->tx_data || !request->allow_send)) ||
        (request->configure && strcmp(request->action, "exchange")) ||
        (request->tx_size && strcmp(request->action, "exchange"))) return 0;
    if (!strcmp(request->action, "list")) return 1;
    if (!port_number(request->port, &number)) return 0;
    (void)swprintf_s(context->port_name, 16u, L"COM%u", number);
    (void)swprintf_s(context->device_name, 32u, L"\\\\.\\COM%u", number);
    if (request->configure && (request->settings.baud_rate < 50u || request->settings.baud_rate > 4000000u ||
        request->settings.data_bits < 5u || request->settings.data_bits > 8u ||
        !request->settings.parity || !strchr("NEOMS", request->settings.parity) ||
        (request->settings.stop_bits_tenths != 10u && request->settings.stop_bits_tenths != 15u && request->settings.stop_bits_tenths != 20u) ||
        (request->settings.stop_bits_tenths == 15u && request->settings.data_bits != 5u) ||
        (request->settings.stop_bits_tenths == 20u && request->settings.data_bits == 5u))) return 0;
    return 1;
}

static int stopped(serial_context *context) {
    cdx_serial_result *result = context->result;
    if (context->request->cancellation && WaitForSingleObject(context->request->cancellation, 0) == WAIT_OBJECT_0) {
        result->status = "cancelled"; result->stop_reason = "cancelled";
        result->message = "Serial operation cancelled; confirmed partial bytes are retained"; return 1;
    }
    if (GetTickCount64() >= context->deadline) {
        result->status = "timed_out"; result->stop_reason = "deadline";
        result->message = "Serial deadline reached; confirmed partial bytes are retained"; return 1;
    }
    return 0;
}

static void io_error(serial_context *context, DWORD error) {
    cdx_serial_result *result = context->result;
    result->win32_error = error; result->stop_reason = "io_error";
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        result->status = "not_found"; result->message = "The selected COM port is unavailable";
    } else if (error == ERROR_SHARING_VIOLATION) {
        result->status = "busy"; result->message = "The selected COM port is already open exclusively";
    } else if (error == ERROR_ACCESS_DENIED) {
        result->status = "access_denied_or_busy"; result->message = "Windows denied access; exclusive use and permissions cannot be distinguished from this error";
    } else {
        result->status = "io_error"; result->message = "Windows serial API failed";
    }
}

static void settings_from_dcb(const DCB *dcb, cdx_serial_settings *settings) {
    static const char parities[] = "NOEMS";
    settings->baud_rate = dcb->BaudRate; settings->data_bits = dcb->ByteSize;
    settings->parity = dcb->Parity <= SPACEPARITY ? parities[dcb->Parity] : '?';
    settings->stop_bits_tenths = dcb->StopBits == ONESTOPBIT ? 10u : dcb->StopBits == ONE5STOPBITS ? 15u : 20u;
    settings->dtr_control = dcb->fDtrControl; settings->rts_control = dcb->fRtsControl;
    settings->cts_flow = dcb->fOutxCtsFlow; settings->dsr_flow = dcb->fOutxDsrFlow;
    settings->xon_input = dcb->fInX; settings->xon_output = dcb->fOutX;
}

static void list_ports(serial_context *context) {
    HKEY key = NULL;
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_QUERY_VALUE, &key);
    cdx_serial_result *result = context->result;
    result->status = "completed"; result->stop_reason = "enumerated"; result->message = "Serial port names enumerated without opening devices";
    if (rc == ERROR_FILE_NOT_FOUND) return;
    if (rc != ERROR_SUCCESS) { io_error(context, (DWORD)rc); return; }
    for (DWORD index = 0; index < 1024u && !stopped(context); ++index) {
        wchar_t value_name[512], value[512];
        DWORD name_size = 512u, value_size = sizeof(value), type = 0;
        char name[32];
        unsigned int number;
        int bytes;
        rc = RegEnumValueW(key, index, value_name, &name_size, NULL, &type, (BYTE *)value, &value_size);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc != ERROR_SUCCESS || type != REG_SZ || value_size < sizeof(wchar_t) || value_size > sizeof(value) || value_size % sizeof(wchar_t)) {
            ++result->skipped; continue;
        }
        if (value[value_size / sizeof(wchar_t) - 1u] != 0) { ++result->skipped; continue; }
        bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, name, sizeof(name), NULL, NULL);
        if (!bytes || !port_number(name, &number)) { ++result->skipped; continue; }
        if (result->count == context->request->max_entries) { result->truncated = 1; break; }
        (void)snprintf(result->ports[result->count++], 16u, "COM%u", number);
        if (index == 1023u) result->truncated = 1;
    }
    RegCloseKey(key);
    if (!strcmp(result->status, "completed") && (result->truncated || result->skipped)) {
        result->status = "partial"; result->message = "Port enumeration reached a bound or could not decode every registry value";
    }
}

static int diagnose_mapping(serial_context *context) {
    wchar_t mapping[512];
    cdx_serial_result *result = context->result;
    DWORD got = QueryDosDeviceW(context->port_name, mapping, 512u);
    DWORD size = sizeof(COMMCONFIG);
    BOOL have_default = FALSE;
    COMMCONFIG *configuration = NULL;
    if (!got) { io_error(context, GetLastError()); return 0; }
    result->mapped = 1;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, mapping, -1, result->dos_device, sizeof(result->dos_device), NULL, NULL)) {
        result->status = "encoding_error"; result->stop_reason = "device_mapping";
        result->message = "DOS device mapping is not representable as bounded UTF-8"; return 0;
    }
    configuration = (COMMCONFIG *)calloc(1u, size);
    if (configuration) {
        configuration->dwSize = size;
        have_default = GetDefaultCommConfigW(context->port_name, configuration, &size);
        if (!have_default && GetLastError() == ERROR_INSUFFICIENT_BUFFER && size <= 65536u && !stopped(context)) {
            free(configuration); configuration = (COMMCONFIG *)calloc(1u, size);
            if (configuration) { configuration->dwSize = size; have_default = GetDefaultCommConfigW(context->port_name, configuration, &size); }
        }
        if (configuration && have_default) {
            settings_from_dcb(&configuration->dcb, &result->default_settings); result->default_settings_known = 1;
        }
        free(configuration);
    }
    result->status = "completed"; result->stop_reason = "passive_diagnosis";
    result->message = "Device mapping and available default settings inspected; active settings and busy state remain unknown without open_probe";
    return !stopped(context);
}

static int open_port(serial_context *context) {
    if (stopped(context)) return 0;
    context->port = CreateFileW(context->device_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (context->port == INVALID_HANDLE_VALUE) { io_error(context, GetLastError()); return 0; }
    context->result->opened = 1;
    return 1;
}

static void inspect_port(serial_context *context) {
    DCB dcb;
    DWORD modem_status;
    cdx_serial_result *result = context->result;
    memset(&dcb, 0, sizeof(dcb)); dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(context->port, &dcb)) { io_error(context, GetLastError()); return; }
    settings_from_dcb(&dcb, &result->active_settings); result->active_settings_known = 1;
    if (GetCommModemStatus(context->port, &modem_status)) { result->modem_status = modem_status; result->modem_status_known = 1; }
    result->status = "completed"; result->stop_reason = "active_diagnosis";
    result->message = "Port opened and inspected without transmitting, changing settings or issuing DTR/RTS/BREAK commands; driver open/close effects remain device-specific";
    (void)stopped(context);
}

static int transfer(serial_context *context, int writing, unsigned char *data, size_t size, size_t *confirmed) {
    serial_io *io = (serial_io *)calloc(1u, sizeof(*io));
    DWORD transferred = 0, error = ERROR_SUCCESS;
    BOOL complete;
    int finished = 0;
    if (!io) { io_error(context, ERROR_NOT_ENOUGH_MEMORY); return 0; }
    io->overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!io->overlapped.hEvent) { free(io); io_error(context, GetLastError()); return 0; }
    if (writing) memcpy(io->buffer, data, size);
    complete = writing ? WriteFile(context->port, io->buffer, (DWORD)size, NULL, &io->overlapped) :
        ReadFile(context->port, io->buffer, (DWORD)size, NULL, &io->overlapped);
    error = complete ? ERROR_SUCCESS : GetLastError();
    if (!complete && error != ERROR_IO_PENDING) { io_error(context, error); goto done; }
    for (unsigned int poll = 0; poll < SERIAL_POLLS && !stopped(context); ++poll) {
        if (GetOverlappedResult(context->port, &io->overlapped, &transferred, FALSE)) { finished = 1; break; }
        error = GetLastError();
        if (error != ERROR_IO_INCOMPLETE) { io_error(context, error); goto done; }
        (void)WaitForSingleObject(io->overlapped.hEvent, 10u);
    }
    if (!finished) {
        uint64_t deadline = GetTickCount64() + 1000u;
        (void)CancelIoEx(context->port, &io->overlapped);
        for (unsigned int poll = 0; poll < 100u && GetTickCount64() < deadline; ++poll) {
            if (WaitForSingleObject(io->overlapped.hEvent, 10u) == WAIT_OBJECT_0) {
                complete = GetOverlappedResult(context->port, &io->overlapped, &transferred, FALSE);
                error = complete ? ERROR_SUCCESS : GetLastError();
                finished = complete || error != ERROR_IO_INCOMPLETE;
                break;
            }
        }
        if (!finished) {
            /* Windows may still write to OVERLAPPED/buffer until cancellation completes. */
            context->result->cleanup_complete = 0;
            context->result->message = "The serial driver did not acknowledge cancellation; pending I/O storage is retained until process exit";
            return 0;
        }
        if (complete) {
            if (!writing) memcpy(data, io->buffer, transferred);
            *confirmed = transferred;
        }
        finished = 0;
        goto done;
    }
    if (!writing) memcpy(data, io->buffer, transferred);
    *confirmed = transferred;
done:
    CloseHandle(io->overlapped.hEvent); free(io);
    return finished;
}

static void exchange(serial_context *context) {
    const cdx_serial_request *request = context->request;
    cdx_serial_result *result = context->result;
    DCB dcb;
    uint64_t idle_started;
    memset(&dcb, 0, sizeof(dcb)); dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(context->port, &dcb)) { io_error(context, GetLastError()); return; }
    if (request->configure) {
        static const char parities[] = "NOEMS";
        const char *parity = strchr(parities, request->settings.parity);
        /* Keep the existing flow and DTR/RTS modes while applying the requested line format. */
        dcb.BaudRate = request->settings.baud_rate; dcb.ByteSize = (BYTE)request->settings.data_bits;
        dcb.Parity = (BYTE)(parity - parities); dcb.fParity = dcb.Parity != NOPARITY; dcb.fBinary = TRUE;
        dcb.StopBits = request->settings.stop_bits_tenths == 10u ? ONESTOPBIT : request->settings.stop_bits_tenths == 15u ? ONE5STOPBITS : TWOSTOPBITS;
        if (!SetCommState(context->port, &dcb)) { io_error(context, GetLastError()); return; }
        result->settings_changed = 1;
    }
    settings_from_dcb(&dcb, &result->active_settings); result->active_settings_known = 1;
    result->status = "completed"; result->stop_reason = "write_only"; result->message = "Serial exchange completed at the byte-transport boundary";
    for (unsigned int chunk = 0; chunk < 65536u && result->tx_size < request->tx_size && !stopped(context); ++chunk) {
        size_t count = request->tx_size - result->tx_size, confirmed = 0;
        if (count > 4096u) count = 4096u;
        result->write_attempted = 1;
        if (!transfer(context, 1, (unsigned char *)request->tx_data + result->tx_size, count, &confirmed)) {
            result->tx_size += confirmed; return;
        }
        result->tx_size += confirmed;
        if (!confirmed) { io_error(context, ERROR_WRITE_FAULT); return; }
    }
    result->write_completed = request->tx_size > 0 && result->tx_size == request->tx_size;
    if (!request->read_max_bytes || stopped(context)) return;
    result->rx_data = (unsigned char *)calloc(request->read_max_bytes, 1u);
    if (!result->rx_data) { io_error(context, ERROR_NOT_ENOUGH_MEMORY); return; }
    idle_started = GetTickCount64();
    for (unsigned int poll = 0; poll < SERIAL_QUEUE_POLLS && !stopped(context); ++poll) {
        COMSTAT state;
        DWORD errors = 0;
        size_t count, confirmed = 0;
        memset(&state, 0, sizeof(state));
        if (!ClearCommError(context->port, &errors, &state)) { io_error(context, GetLastError()); return; }
        result->communication_errors |= errors;
        count = request->read_max_bytes - result->rx_size;
        if (count > state.cbInQue) count = state.cbInQue;
        if (count > 4096u) count = 4096u;
        if (count) {
            if (!transfer(context, 0, result->rx_data + result->rx_size, count, &confirmed)) {
                result->rx_size += confirmed; return;
            }
            result->rx_size += confirmed;
            if (confirmed) idle_started = GetTickCount64();
            if (result->rx_size == request->read_max_bytes) { result->stop_reason = "read_limit"; return; }
        } else if (GetTickCount64() - idle_started >= request->read_idle_ms) {
            result->stop_reason = "idle";
            if (!result->rx_size) { result->status = "no_data"; result->message = "No serial bytes arrived before the idle limit"; }
            return;
        }
        if (request->cancellation) (void)WaitForSingleObject(request->cancellation, 10u); else Sleep(10u);
    }
}
