#include "cdx/project_facts.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

static void check(int condition, const char *name) {
    ++checks;
    if (!condition) ++failures;
    printf("%s %s\n", condition ? "PASS" : "FAIL", name);
}

static void facts_case(void) {
    cdx_project_facts facts;
    const cdx_project_fact *fact;
    char context[512];
    size_t written = 0u;
    cdx_project_facts_init(&facts);
    check(cdx_project_fact_upsert(&facts, "build.profile", "debug", "observed CLI", CDX_FACT_CONFIRMATION) == CDX_FACT_OK, "confirmation creates active fact");
    fact = cdx_project_fact_find(&facts, "build.profile");
    check(fact && fact->state == CDX_FACT_ACTIVE && fact->confirmations == 1u && !strcmp(fact->evidence, "observed CLI"), "active fact retains evidence");
    check(cdx_project_fact_upsert(&facts, "build.profile", "release", "contradictory log", CDX_FACT_CONFIRMATION) == CDX_FACT_AMBIGUOUS, "contradiction is surfaced");
    fact = cdx_project_fact_find(&facts, "build.profile");
    check(fact && fact->state == CDX_FACT_ACTIVE && fact->contradictions == 1u, "contradiction does not silently replace value");
    check(cdx_project_fact_revoke(&facts, "build.profile", "user correction") == CDX_FACT_OK, "revoke succeeds");
    check(cdx_project_facts_render_context(&facts, context, sizeof(context), &written) == CDX_FACT_OK && written == 0u, "revoked fact is excluded from context");
    check(cdx_project_fact_upsert(&facts, "test.command", "eng/test.cmd", "roadmap", CDX_FACT_CONFIRMATION) == CDX_FACT_OK, "second fact is accepted");
    check(cdx_project_facts_render_context(&facts, context, sizeof(context), &written) == CDX_FACT_OK && strstr(context, "test.command=eng/test.cmd [active") != NULL, "context rendering is deterministic");
    check(cdx_project_fact_upsert(&facts, "bad\nkey", "x", "e", CDX_FACT_CONFIRMATION) == CDX_FACT_INVALID, "newline key is rejected");
    check(cdx_project_fact_revoke(&facts, "missing", "e") == CDX_FACT_NOT_FOUND, "missing revoke is distinct");
}

static void fingerprint_case(void) {
    cdx_failure_fingerprint first, second;
    check(cdx_failure_fingerprint_make(" Timeout:  1200 MS ", &first) == CDX_FACT_OK &&
          first.classification == CDX_FINGERPRINT_TIMEOUT && !strcmp(first.normalized, "timeout: 1200 ms"), "fingerprint normalizes case and whitespace");
    check(cdx_failure_fingerprint_make("timeout: 1200 ms", &second) == CDX_FACT_OK && !strcmp(first.hash_hex, second.hash_hex), "equivalent fingerprints have stable hash");
    check(cdx_failure_fingerprint_make("permission denied", &first) == CDX_FACT_OK &&
          !strcmp(cdx_fingerprint_class_name(first.classification), "permission"), "fingerprint class is explicit");
    check(cdx_failure_fingerprint_make("bad\nline", &first) == CDX_FACT_INVALID, "newline evidence is rejected");
}

int main(void) {
    facts_case();
    fingerprint_case();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
