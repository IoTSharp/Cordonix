#include "cdx/project_facts.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int valid_text(const char *text, size_t maximum) {
    size_t i;
    if (!text || !text[0]) return 0;
    for (i = 0; i <= maximum && text[i]; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == 0u || c == '\r' || c == '\n') return 0;
    }
    return i <= maximum;
}

static int copy_text(char *destination, size_t capacity, const char *source) {
    size_t length;
    if (!destination || !source || !valid_text(source, capacity - 1u)) return 0;
    length = strlen(source);
    memcpy(destination, source, length + 1u);
    return 1;
}

void cdx_project_facts_init(cdx_project_facts *facts) {
    if (!facts) return;
    memset(facts, 0, sizeof(*facts));
}

static size_t find_index(const cdx_project_facts *facts, const char *key, size_t *matches) {
    size_t i, found = SIZE_MAX, count = 0u;
    if (!facts || !valid_text(key, CDX_FACT_KEY_MAX - 1u)) {
        if (matches) *matches = 0u;
        return SIZE_MAX;
    }
    for (i = 0u; i < CDX_FACT_MAX; ++i) {
        if (facts->items[i].used && !strcmp(facts->items[i].key, key)) {
            found = i;
            ++count;
        }
    }
    if (matches) *matches = count;
    return found;
}

const cdx_project_fact *cdx_project_fact_find(const cdx_project_facts *facts,
                                              const char *key) {
    size_t matches = 0u, index = find_index(facts, key, &matches);
    if (matches != 1u) return NULL;
    return &facts->items[index];
}

cdx_fact_result cdx_project_fact_upsert(cdx_project_facts *facts,
                                        const char *key, const char *value,
                                        const char *evidence,
                                        cdx_fact_evidence_kind kind) {
    size_t matches = 0u, index;
    cdx_project_fact *fact;
    if (!facts || !valid_text(key, CDX_FACT_KEY_MAX - 1u) ||
        !valid_text(value, CDX_FACT_VALUE_MAX - 1u) ||
        !valid_text(evidence, CDX_FACT_EVIDENCE_MAX - 1u) ||
        (kind != CDX_FACT_CONFIRMATION && kind != CDX_FACT_CONTRADICTION)) return CDX_FACT_INVALID;
    index = find_index(facts, key, &matches);
    if (matches > 1u) return CDX_FACT_AMBIGUOUS;
    if (index == SIZE_MAX) {
        if (facts->count >= CDX_FACT_MAX) return CDX_FACT_FULL;
        for (index = 0u; index < CDX_FACT_MAX && facts->items[index].used; ++index) { }
        if (index >= CDX_FACT_MAX) return CDX_FACT_FULL;
        fact = &facts->items[index];
        memset(fact, 0, sizeof(*fact));
        fact->used = 1;
        ++facts->count;
        if (!copy_text(fact->key, sizeof(fact->key), key) ||
            !copy_text(fact->value, sizeof(fact->value), value)) return CDX_FACT_INVALID;
        fact->state = CDX_FACT_CANDIDATE;
    } else {
        fact = &facts->items[index];
        if (strcmp(fact->value, value) != 0 && kind == CDX_FACT_CONFIRMATION) {
            ++fact->contradictions;
            (void)copy_text(fact->evidence, sizeof(fact->evidence), evidence);
            return CDX_FACT_AMBIGUOUS;
        }
    }
    if (!copy_text(fact->evidence, sizeof(fact->evidence), evidence)) return CDX_FACT_INVALID;
    if (kind == CDX_FACT_CONFIRMATION) {
        if (fact->confirmations < UINT32_MAX) ++fact->confirmations;
        if (fact->confirmations >= 1u && fact->contradictions == 0u) fact->state = CDX_FACT_ACTIVE;
    } else {
        if (fact->contradictions < UINT32_MAX) ++fact->contradictions;
        fact->state = CDX_FACT_CANDIDATE;
    }
    return CDX_FACT_OK;
}

cdx_fact_result cdx_project_fact_revoke(cdx_project_facts *facts,
                                        const char *key, const char *evidence) {
    size_t matches = 0u, index = find_index(facts, key, &matches);
    if (!facts || !valid_text(evidence, CDX_FACT_EVIDENCE_MAX - 1u)) return CDX_FACT_INVALID;
    if (matches == 0u) return CDX_FACT_NOT_FOUND;
    if (matches != 1u) return CDX_FACT_AMBIGUOUS;
    if (!copy_text(facts->items[index].evidence, sizeof(facts->items[index].evidence), evidence)) return CDX_FACT_INVALID;
    facts->items[index].state = CDX_FACT_REVOKED;
    return CDX_FACT_OK;
}

cdx_fact_result cdx_project_facts_render_context(const cdx_project_facts *facts,
                                                 char *output, size_t capacity,
                                                 size_t *written) {
    size_t i, used = 0u;
    int count;
    if (written) *written = 0u;
    if (!facts || !output || capacity == 0u) return CDX_FACT_INVALID;
    output[0] = 0;
    for (i = 0u; i < CDX_FACT_MAX; ++i) {
        const cdx_project_fact *fact = &facts->items[i];
        if (!fact->used || fact->state == CDX_FACT_REVOKED) continue;
        count = snprintf(output + used, capacity - used, "%s=%s [%s c=%u x=%u]\n",
                         fact->key, fact->value,
                         fact->state == CDX_FACT_ACTIVE ? "active" : "candidate",
                         (unsigned)fact->confirmations, (unsigned)fact->contradictions);
        if (count < 0 || (size_t)count >= capacity - used) {
            output[used] = 0;
            if (written) *written = used;
            return CDX_FACT_TOO_LARGE;
        }
        used += (size_t)count;
    }
    if (written) *written = used;
    return CDX_FACT_OK;
}

static int has_token(const char *text, const char *token) {
    size_t i, j, length = strlen(token);
    for (i = 0u; text[i]; ++i) {
        for (j = 0u; j < length && text[i + j] &&
             (char)tolower((unsigned char)text[i + j]) == token[j]; ++j) { }
        if (j == length) return 1;
    }
    return 0;
}

static cdx_fingerprint_class classify(const char *raw) {
    if (has_token(raw, "timeout") || has_token(raw, "timed_out") || has_token(raw, "deadline")) return CDX_FINGERPRINT_TIMEOUT;
    if (has_token(raw, "cancel")) return CDX_FINGERPRINT_CANCELLED;
    if (has_token(raw, "validation") || has_token(raw, "invalid") || has_token(raw, "argument")) return CDX_FINGERPRINT_VALIDATION;
    if (has_token(raw, "not_found") || has_token(raw, "not found") || has_token(raw, "missing")) return CDX_FINGERPRINT_NOT_FOUND;
    if (has_token(raw, "permission") || has_token(raw, "access_denied") || has_token(raw, "forbidden")) return CDX_FINGERPRINT_PERMISSION;
    if (has_token(raw, "network") || has_token(raw, "connection") || has_token(raw, "tls")) return CDX_FINGERPRINT_NETWORK;
    return CDX_FINGERPRINT_UNKNOWN;
}

static uint64_t fnv1a(const char *text) {
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t i;
    for (i = 0u; text[i]; ++i) hash = (hash ^ (unsigned char)text[i]) * UINT64_C(1099511628211);
    return hash;
}

cdx_fact_result cdx_failure_fingerprint_make(const char *raw,
                                             cdx_failure_fingerprint *output) {
    size_t i = 0u, used = 0u;
    int whitespace = 0;
    uint64_t hash;
    if (!output || !valid_text(raw, CDX_FINGERPRINT_INPUT_MAX)) return CDX_FACT_INVALID;
    memset(output, 0, sizeof(*output));
    output->classification = classify(raw);
    for (i = 0u; raw[i] && used + 1u < sizeof(output->normalized); ++i) {
        unsigned char c = (unsigned char)raw[i];
        if (isspace(c)) { whitespace = 1; continue; }
        if (whitespace && used > 0u) output->normalized[used++] = ' ';
        whitespace = 0;
        output->normalized[used++] = (char)tolower(c);
    }
    output->normalized[used] = 0;
    hash = fnv1a(output->normalized);
    (void)snprintf(output->hash_hex, sizeof(output->hash_hex), "%016llx", (unsigned long long)hash);
    return raw[i] ? CDX_FACT_TOO_LARGE : CDX_FACT_OK;
}

const char *cdx_fingerprint_class_name(cdx_fingerprint_class classification) {
    switch (classification) {
    case CDX_FINGERPRINT_TIMEOUT: return "timeout";
    case CDX_FINGERPRINT_CANCELLED: return "cancelled";
    case CDX_FINGERPRINT_VALIDATION: return "validation";
    case CDX_FINGERPRINT_NOT_FOUND: return "not_found";
    case CDX_FINGERPRINT_PERMISSION: return "permission";
    case CDX_FINGERPRINT_NETWORK: return "network";
    default: return "unknown";
    }
}
