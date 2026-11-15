#include "cdx/failure_loop.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int copy_bounded(char *destination, size_t capacity, const char *source) {
    size_t length;
    if (!destination || capacity == 0 || !source) return 0;
    length = strlen(source);
    if (length >= capacity) return 0;
    memcpy(destination, source, length + 1u);
    return 1;
}

int cdx_failure_fingerprint_normalize(const char *input, char *output, size_t capacity) {
    size_t input_index = 0, output_index = 0;
    int pending_separator = 0;
    unsigned word_count = 0;
    if (!input || !output || capacity < 2u || input[0] == '\0') return 0;
    while (input[input_index] != '\0') {
        unsigned char character = (unsigned char)input[input_index++];
        if (character == 0 || character > 0x7fu) return 0;
        if (isalnum(character) || character == '_' || character == '.') {
            if (pending_separator && output_index > 0u) {
                if (output_index + 1u >= capacity) return 0;
                output[output_index++] = '-';
            }
            pending_separator = 0;
            if (output_index + 1u >= capacity) return 0;
            output[output_index++] = (char)tolower(character);
            word_count++;
        } else if (character == '\r' || character == '\n') {
            return 0;
        } else if (character == '-' || isspace(character) || ispunct(character)) {
            if (output_index > 0u) pending_separator = 1;
        } else {
            return 0;
        }
    }
    while (output_index > 0u && output[output_index - 1u] == '-') output_index--;
    if (word_count == 0u || output_index == 0u) return 0;
    output[output_index] = '\0';
    return 1;
}

void cdx_failure_loop_reset(cdx_failure_loop_state *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
}

static void decision_reset(cdx_failure_decision *decision) {
    memset(decision, 0, sizeof(*decision));
    decision->action = CDX_FAILURE_ACTION_INVALID;
}

static int outcome_is_blocking(cdx_failure_outcome outcome) {
    return outcome == CDX_FAILURE_OUTCOME_UNKNOWN || outcome == CDX_FAILURE_OUTCOME_TIMED_OUT;
}

int cdx_failure_loop_observe(cdx_failure_loop_state *state,
                             const cdx_failure_observation *observation,
                             cdx_failure_decision *decision) {
    char normalized[CDX_FAILURE_FINGERPRINT_CAP];
    int same_fingerprint;
    if (!state || !observation || !decision || !observation->fingerprint ||
        !observation->evidence || observation->turn_id == 0u ||
        observation->turn_id <= state->turn_id ||
        observation->outcome < CDX_FAILURE_OUTCOME_FAILED ||
        observation->outcome > CDX_FAILURE_OUTCOME_CANCELLED ||
        !cdx_failure_fingerprint_normalize(observation->fingerprint, normalized, sizeof(normalized)) ||
        !copy_bounded(state->evidence, sizeof(state->evidence), observation->evidence)) {
        if (decision) decision_reset(decision);
        return 0;
    }
    decision_reset(decision);
    state->turn_id = observation->turn_id;
    if (observation->progress || observation->outcome == CDX_FAILURE_OUTCOME_COMPLETED) {
        copy_bounded(state->fingerprint, sizeof(state->fingerprint), normalized);
        state->repeated_count = 0u;
        state->last_reminder_turn = 0u;
        state->has_observation = 1u;
        state->replay_blocked = 0u;
        decision->action = CDX_FAILURE_ACTION_NONE;
        decision->replay_allowed = 1u;
        return 1;
    }
    same_fingerprint = state->has_observation && strcmp(state->fingerprint, normalized) == 0;
    if (!same_fingerprint) {
        copy_bounded(state->fingerprint, sizeof(state->fingerprint), normalized);
        state->repeated_count = 1u;
    } else if (state->repeated_count < 3u) {
        state->repeated_count++;
    }
    state->has_observation = 1u;
    decision->repeated_count = state->repeated_count;
    if (outcome_is_blocking(observation->outcome) || state->replay_blocked) {
        decision->action = CDX_FAILURE_ACTION_BLOCK_REPLAY;
        decision->replay_allowed = 0u;
        decision->human_stop_required = 1u;
        state->replay_blocked = 1u;
        (void)snprintf(decision->reminder, sizeof(decision->reminder),
                       "human review required: %s; do not replay automatically",
                       outcome_is_blocking(observation->outcome)
                           ? (observation->outcome == CDX_FAILURE_OUTCOME_UNKNOWN ? "unknown outcome" : "timed_out outcome")
                           : "prior unknown or timed_out outcome");
        return 1;
    }
    decision->replay_allowed = 1u;
    if (state->repeated_count >= 3u) {
        decision->action = CDX_FAILURE_ACTION_STOP_REQUIRED;
        decision->human_stop_required = 1u;
        (void)snprintf(decision->reminder, sizeof(decision->reminder),
                       "failure repeated %u times; stop and choose a new approach", state->repeated_count);
    } else if (state->repeated_count >= 2u && state->last_reminder_turn != observation->turn_id) {
        decision->action = CDX_FAILURE_ACTION_REMIND;
        state->last_reminder_turn = observation->turn_id;
        (void)snprintf(decision->reminder, sizeof(decision->reminder),
                       "failure repeated %u times; inspect evidence before continuing", state->repeated_count);
    } else {
        decision->action = CDX_FAILURE_ACTION_NONE;
    }
    return 1;
}

const char *cdx_failure_action_name(cdx_failure_action action) {
    switch (action) {
    case CDX_FAILURE_ACTION_NONE: return "none";
    case CDX_FAILURE_ACTION_REMIND: return "remind";
    case CDX_FAILURE_ACTION_STOP_REQUIRED: return "stop_required";
    case CDX_FAILURE_ACTION_BLOCK_REPLAY: return "block_replay";
    default: return "invalid";
    }
}
