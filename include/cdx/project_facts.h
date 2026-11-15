#ifndef CDX_PROJECT_FACTS_H
#define CDX_PROJECT_FACTS_H

#include <stddef.h>
#include <stdint.h>

#define CDX_FACT_MAX 32u
#define CDX_FACT_KEY_MAX 64u
#define CDX_FACT_VALUE_MAX 192u
#define CDX_FACT_EVIDENCE_MAX 192u
#define CDX_FINGERPRINT_INPUT_MAX 512u
#define CDX_FINGERPRINT_MAX 192u
#define CDX_CONTEXT_MAX_FACTS 32u

typedef enum cdx_fact_state {
    CDX_FACT_CANDIDATE = 0,
    CDX_FACT_ACTIVE = 1,
    CDX_FACT_REVOKED = 2
} cdx_fact_state;

typedef enum cdx_fact_evidence_kind {
    CDX_FACT_CONFIRMATION = 1,
    CDX_FACT_CONTRADICTION = 2
} cdx_fact_evidence_kind;

typedef enum cdx_fingerprint_class {
    CDX_FINGERPRINT_UNKNOWN = 0,
    CDX_FINGERPRINT_TIMEOUT,
    CDX_FINGERPRINT_CANCELLED,
    CDX_FINGERPRINT_VALIDATION,
    CDX_FINGERPRINT_NOT_FOUND,
    CDX_FINGERPRINT_PERMISSION,
    CDX_FINGERPRINT_NETWORK
} cdx_fingerprint_class;

typedef enum cdx_fact_result {
    CDX_FACT_OK = 0,
    CDX_FACT_INVALID,
    CDX_FACT_FULL,
    CDX_FACT_NOT_FOUND,
    CDX_FACT_AMBIGUOUS,
    CDX_FACT_TOO_LARGE
} cdx_fact_result;

typedef struct cdx_project_fact {
    char key[CDX_FACT_KEY_MAX];
    char value[CDX_FACT_VALUE_MAX];
    char evidence[CDX_FACT_EVIDENCE_MAX];
    cdx_fact_state state;
    uint32_t confirmations;
    uint32_t contradictions;
    int used;
} cdx_project_fact;

typedef struct cdx_project_facts {
    cdx_project_fact items[CDX_FACT_MAX];
    size_t count;
} cdx_project_facts;

typedef struct cdx_failure_fingerprint {
    cdx_fingerprint_class classification;
    char normalized[CDX_FINGERPRINT_MAX];
    char hash_hex[17];
} cdx_failure_fingerprint;

void cdx_project_facts_init(cdx_project_facts *facts);
cdx_fact_result cdx_project_fact_upsert(cdx_project_facts *facts,
                                        const char *key, const char *value,
                                        const char *evidence,
                                        cdx_fact_evidence_kind kind);
cdx_fact_result cdx_project_fact_revoke(cdx_project_facts *facts,
                                        const char *key, const char *evidence);
const cdx_project_fact *cdx_project_fact_find(const cdx_project_facts *facts,
                                              const char *key);
cdx_fact_result cdx_project_facts_render_context(const cdx_project_facts *facts,
                                                 char *output, size_t capacity,
                                                 size_t *written);

cdx_fact_result cdx_failure_fingerprint_make(const char *raw,
                                             cdx_failure_fingerprint *output);
const char *cdx_fingerprint_class_name(cdx_fingerprint_class classification);

#endif
