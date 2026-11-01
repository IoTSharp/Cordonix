#ifndef CDX_PHONE_H
#define CDX_PHONE_H

#include "cdx/operations.h"
#include <stdint.h>

typedef enum cdx_phone_action {
    CDX_PHONE_DEVICES, CDX_PHONE_SMS_LIST, CDX_PHONE_SMS_READ,
    CDX_PHONE_SMS_SEND, CDX_PHONE_CONTACTS, CDX_PHONE_CALL_STATUS,
    CDX_PHONE_DIAL, CDX_PHONE_ANSWER, CDX_PHONE_HANGUP,
    CDX_PHONE_WAIT_CALL, CDX_PHONE_WAIT_SMS, CDX_PHONE_READ_CODE,
    CDX_PHONE_ADAPTERS, CDX_PHONE_DISCOVER, CDX_PHONE_PAIR,
    CDX_PHONE_CONNECT, CDX_PHONE_DISCONNECT, CDX_PHONE_CONNECTION_STATUS, CDX_PHONE_UNPAIR,
    CDX_PHONE_CALL_HISTORY, CDX_PHONE_APP_NOTIFICATIONS
} cdx_phone_action;

typedef struct cdx_phone_request {
    cdx_phone_action action;
    uint64_t address, radio_address;
    uintptr_t parent_window;
    const char *recipient, *body, *handle, *expected_sha256;
    unsigned timeout_ms, max_entries, offset, connection_ttl_ms;
    int dry_run;
} cdx_phone_request;

/* The stream owns one deadline across connection, protocol and all pages.
   read/write return bytes transferred, zero on failure, and set error. */
typedef struct cdx_phone_stream {
    void *context;
    size_t (*read)(void *, void *, size_t);
    size_t (*write)(void *, const void *, size_t);
    int (*pause)(void *, unsigned);
    const char *error;
    int retain_connection;
} cdx_phone_stream;

typedef struct cdx_phone_hfp {
    int call_index, setup_index, call, setup, initialized;
    int battery_index, battery_level;
} cdx_phone_hfp;
typedef struct cdx_phone_session cdx_phone_session;

typedef struct cdx_phone_ancs_attributes {
    char app[256], title[256], message[512];
} cdx_phone_ancs_attributes;

void cdx_phone_ancs_command(uint32_t, unsigned char [12]);
/* Returns 1 for a complete response, 0 for a fragment, -1 for invalid data. */
int cdx_phone_ancs_parse(const unsigned char *, size_t, uint32_t, cdx_phone_ancs_attributes *);

void cdx_phone_call(const cdx_json *, int, cdx_cancel_handle, cdx_json_output *);
void cdx_phone_session_call(cdx_phone_session *, const cdx_json *, int, cdx_cancel_handle, cdx_json_output *);
cdx_phone_session *cdx_phone_session_create(void);
void cdx_phone_session_destroy(cdx_phone_session *);
void cdx_phone_session_tick(cdx_phone_session *);
void cdx_phone_session_set_window(cdx_phone_session *, uintptr_t);
void cdx_phone_session_platform(cdx_phone_session *, const cdx_phone_request *, cdx_cancel_handle, cdx_json_output *);
void cdx_phone_helper(const cdx_json *, int, cdx_json_output *);
void cdx_phone_platform_helper(const cdx_phone_request *, cdx_json_output *);
void cdx_phone_platform(const cdx_phone_request *, cdx_cancel_handle, cdx_json_output *);
void cdx_phone_protocol(const cdx_phone_request *, cdx_phone_stream *, cdx_json_output *);
void cdx_phone_protocol_connected(const cdx_phone_request *, cdx_phone_stream *, cdx_phone_hfp *, cdx_json_output *);
void cdx_phone_app_notifications_platform(const cdx_phone_request *, cdx_cancel_handle, cdx_json_output *);
void cdx_phone_error(cdx_json_output *, const char *, int);

#endif
