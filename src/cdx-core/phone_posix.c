#include "cdx/phone.h"
#include <stdlib.h>

struct cdx_phone_session { int unused; };
cdx_phone_session *cdx_phone_session_create(void) { return calloc(1, sizeof(cdx_phone_session)); }
void cdx_phone_session_destroy(cdx_phone_session *session) { free(session); }
void cdx_phone_session_tick(cdx_phone_session *session) { (void)session; }
void cdx_phone_session_set_window(cdx_phone_session *session, uintptr_t window) { (void)session; (void)window; }
void cdx_phone_platform_helper(const cdx_phone_request *r, cdx_json_output *out) {
    cdx_phone_platform(r, NULL, out);
}
void cdx_phone_session_platform(cdx_phone_session *session, const cdx_phone_request *r,
                                cdx_cancel_handle cancel, cdx_json_output *out) {
    (void)session; cdx_phone_platform(r, cancel, out);
}

void cdx_phone_platform(const cdx_phone_request *request, cdx_cancel_handle cancel, cdx_json_output *out) {
    (void)request;
    cdx_phone_error(out, cancel && atomic_load(cancel) ? "cancelled" : "not_supported", 0);
}
