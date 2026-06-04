/*
 * cancel_scope.c — Hierarchical cooperative cancellation.
 *
 * Scopes form a tree: cancelling a parent propagates to all children.
 * Agents check cscope_should_stop() at safe interruption points
 * (between tool calls, before retries, after IO returns).
 */

#include "harness.h"

void cscope_init(cscope_t *cs, const char *token, cscope_t *parent) {
    memset(cs, 0, sizeof(*cs));
    if (token)
        nc_strlcpy(cs->token, token, sizeof(cs->token));
    cs->state = CSCOPE_ACTIVE;
    cs->parent = parent;
    cs->depth = parent ? parent->depth + 1 : 0;
}

void cscope_request_cancel(cscope_t *cs) {
    if (!cs || cs->state != CSCOPE_ACTIVE)
        return;
    cs->state = CSCOPE_CANCEL_REQUESTED;
    cs->requested_ms = nc_mono_ms();
}

bool cscope_is_cancelled(const cscope_t *cs) {
    if (!cs)
        return false;
    if (cs->state == CSCOPE_CANCEL_REQUESTED || cs->state == CSCOPE_CANCELLED)
        return true;
    if (cs->parent)
        return cscope_is_cancelled(cs->parent);
    return false;
}

bool cscope_should_stop(const cscope_t *cs) {
    return cscope_is_cancelled(cs);
}

void cscope_reset(cscope_t *cs) {
    if (!cs)
        return;
    cs->state = CSCOPE_ACTIVE;
    cs->requested_ms = 0;
}

const char *cscope_state_name(cscope_state_t s) {
    switch (s) {
    case CSCOPE_ACTIVE:
        return "active";
    case CSCOPE_CANCEL_REQUESTED:
        return "cancel_requested";
    case CSCOPE_CANCELLED:
        return "cancelled";
    }
    return "unknown";
}

int cscope_depth(const cscope_t *cs) {
    return cs ? cs->depth : -1;
}
