#ifndef CDX_FAILURE_LOOP_H
#define CDX_FAILURE_LOOP_H

#include <stddef.h>

#define CDX_FAILURE_FINGERPRINT_CAP 96u
#define CDX_FAILURE_EVIDENCE_CAP 160u
#define CDX_FAILURE_REMINDER_CAP 192u

typedef enum cdx_failure_outcome {
    CDX_FAILURE_OUTCOME_FAILED = 0,
    CDX_FAILURE_OUTCOME_UNKNOWN = 1,
    CDX_FAILURE_OUTCOME_TIMED_OUT = 2,
    CDX_FAILURE_OUTCOME_COMPLETED = 3,
    CDX_FAILURE_OUTCOME_CANCELLED = 4
} cdx_failure_outcome;

typedef enum cdx_failure_action {
    CDX_FAILURE_ACTION_NONE = 0,
    CDX_FAILURE_ACTION_REMIND = 1,
    CDX_FAILURE_ACTION_STOP_REQUIRED = 2,
    CDX_FAILURE_ACTION_BLOCK_REPLAY = 3,
    CDX_FAILURE_ACTION_INVALID = 4
} cdx_failure_action;

typedef struct cdx_failure_observation {
    const char *fingerprint;
    const char *evidence;
    unsigned turn_id;
    unsigned progress;
    cdx_failure_outcome outcome;
} cdx_failure_observation;

typedef struct cdx_failure_loop_state {
    char fingerprint[CDX_FAILURE_FINGERPRINT_CAP];
    char evidence[CDX_FAILURE_EVIDENCE_CAP];
    unsigned turn_id;
    unsigned repeated_count;
    unsigned last_reminder_turn;
    unsigned has_observation;
    unsigned replay_blocked;
} cdx_failure_loop_state;

typedef struct cdx_failure_decision {
    cdx_failure_action action;
    unsigned repeated_count;
    unsigned replay_allowed;
    unsigned human_stop_required;
    char reminder[CDX_FAILURE_REMINDER_CAP];
} cdx_failure_decision;

/* Produces a stable ASCII fingerprint: lower-case words joined by '-'. */
int cdx_failure_fingerprint_normalize(const char *input, char *output, size_t capacity);

void cdx_failure_loop_reset(cdx_failure_loop_state *state);

/*
 * Records one host observation. The function never replays work. A timeout or
 * unknown outcome always sets replay_allowed=0 until a human supplies a new
 * observation; progress resets the repeated-failure count.
 */
int cdx_failure_loop_observe(cdx_failure_loop_state *state,
                             const cdx_failure_observation *observation,
                             cdx_failure_decision *decision);

const char *cdx_failure_action_name(cdx_failure_action action);

#endif
