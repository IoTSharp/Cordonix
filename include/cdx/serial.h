#ifndef CDX_SERIAL_H
#define CDX_SERIAL_H

#include "cdx/platform.h"
#include <stddef.h>
#include <stdint.h>

#define CDX_SERIAL_MAX_PORTS 128u
#define CDX_SERIAL_MAX_BYTES 65536u

typedef struct cdx_serial_settings {
    uint32_t baud_rate;
    unsigned int data_bits;
    unsigned int stop_bits_tenths;
    char parity;
    unsigned int dtr_control, rts_control;
    int cts_flow, dsr_flow, xon_input, xon_output;
} cdx_serial_settings;

typedef struct cdx_serial_request {
    const char *action;
    const char *port;
    uint32_t timeout_ms, read_idle_ms;
    size_t read_max_bytes, max_entries;
    const unsigned char *tx_data;
    size_t tx_size;
    int allow_send, configure, open_probe;
    cdx_serial_settings settings;
    cdx_cancel_handle cancellation;
} cdx_serial_request;

typedef struct cdx_serial_result {
    const char *status, *stop_reason, *message;
    char ports[CDX_SERIAL_MAX_PORTS][16];
    size_t count, skipped;
    int truncated, mapped, opened;
    char dos_device[1024];
    int default_settings_known, active_settings_known, modem_status_known;
    cdx_serial_settings default_settings, active_settings;
    uint32_t modem_status, communication_errors, win32_error;
    unsigned char *rx_data;
    size_t rx_size, tx_size;
    int write_attempted, write_completed, settings_changed;
    int cleanup_complete;
    uint64_t elapsed_ms;
} cdx_serial_result;

void cdx_serial_execute(const cdx_serial_request *request, cdx_serial_result *result);
void cdx_serial_result_free(cdx_serial_result *result);
#endif
