#include "cdx/failure_loop.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;
static void check(int condition, const char *message) {
    if (!condition) { (void)fprintf(stderr, "FAIL: %s\n", message); failures++; }
}

int main(void) {
    char normalized[CDX_FAILURE_FINGERPRINT_CAP];
    cdx_failure_loop_state state;
    cdx_failure_decision decision;
    cdx_failure_observation observation;
    check(cdx_failure_fingerprint_normalize("  Build: C6001 / warning  ", normalized, sizeof(normalized)), "normalize succeeds");
    check(strcmp(normalized, "build-c6001-warning") == 0, "normalize is stable");
    check(!cdx_failure_fingerprint_normalize("bad\ninput", normalized, sizeof(normalized)), "newline rejected");
    cdx_failure_loop_reset(&state);
    observation = (cdx_failure_observation){"Build C6001", "compiler error", 1u, 0u, CDX_FAILURE_OUTCOME_FAILED};
    check(cdx_failure_loop_observe(&state, &observation, &decision), "first observation accepted");
    check(decision.action == CDX_FAILURE_ACTION_NONE && decision.repeated_count == 1u, "first failure is quiet");
    observation.turn_id = 2u;
    check(cdx_failure_loop_observe(&state, &observation, &decision), "second observation accepted");
    check(decision.action == CDX_FAILURE_ACTION_REMIND && decision.replay_allowed, "second failure reminds");
    observation.turn_id = 3u;
    check(cdx_failure_loop_observe(&state, &observation, &decision), "third observation accepted");
    check(decision.action == CDX_FAILURE_ACTION_STOP_REQUIRED && decision.human_stop_required, "third failure stops");
    observation.turn_id = 4u;
    observation.progress = 1u;
    check(cdx_failure_loop_observe(&state, &observation, &decision), "progress accepted");
    check(decision.action == CDX_FAILURE_ACTION_NONE && decision.repeated_count == 0u, "progress resets");
    observation.progress = 0u;
    observation.turn_id = 5u;
    observation.outcome = CDX_FAILURE_OUTCOME_UNKNOWN;
    check(cdx_failure_loop_observe(&state, &observation, &decision), "unknown accepted");
    check(decision.action == CDX_FAILURE_ACTION_BLOCK_REPLAY && !decision.replay_allowed, "unknown blocks replay");
    observation.turn_id = 5u;
    check(!cdx_failure_loop_observe(&state, &observation, &decision), "duplicate turn rejected");
    observation.turn_id = 6u;
    observation.outcome = CDX_FAILURE_OUTCOME_FAILED;
    observation.evidence = "evidence retained";
    check(cdx_failure_loop_observe(&state, &observation, &decision), "new turn accepted");
    check(decision.action == CDX_FAILURE_ACTION_BLOCK_REPLAY && !decision.replay_allowed, "block persists until progress");
    check(state.evidence[0] != '\0', "evidence retained");
    (void)printf("failure-loop checks: %u\n", failures ? 0u : 10u);
    return failures ? 1 : 0;
}
