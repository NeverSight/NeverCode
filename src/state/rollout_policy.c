/*
 * rollout_policy.c — Rollout recording policy with two-tier persistence
 *
 * Decides which items from a session should be persisted
 * for rollout replay, memory extraction, and auditing.
 * Supports Limited and Extended fidelity modes so callers
 * can choose between compact replay files and full audit trails.
 */

#include "nevercode.h"
#include "defaults.h"

/* ── Types ── */

typedef enum { RP_PERSIST = 0, RP_SKIP = 1, RP_REDACT = 2, RP_SUMMARIZE = 3 } rollout_persist_t;

typedef enum {
    RI_USER_MSG = 0,
    RI_ASSISTANT = 1,
    RI_TOOL_CALL = 2,
    RI_TOOL_RESULT = 3,
    RI_SYSTEM_MSG = 4,
    RI_COMPACT_MARK = 5,
    RI_TURN_BOUNDARY = 6,
    RI_ASSESSMENT = 7,
    RI_COLLAB_EVENT = 8
} rollout_item_kind_t;

typedef struct {
    rollout_item_kind_t kind;
    rollout_persist_t decision;
    int min_mode;
    const char *reason;
} policy_rule_t;

/* ── Default policy rules ── */

static policy_rule_t g_rules[] = {
    {RI_USER_MSG, RP_PERSIST, RP_MODE_LIMITED, "user messages always persisted"},
    {RI_ASSISTANT, RP_PERSIST, RP_MODE_LIMITED, "assistant responses persisted"},
    {RI_TOOL_CALL, RP_PERSIST, RP_MODE_LIMITED, "tool calls persisted for replay"},
    {RI_TOOL_RESULT, RP_SUMMARIZE, RP_MODE_EXTENDED, "large tool results in extended mode"},
    {RI_SYSTEM_MSG, RP_SKIP, RP_MODE_LIMITED, "system messages excluded"},
    {RI_COMPACT_MARK, RP_PERSIST, RP_MODE_LIMITED, "compaction markers retained"},
    {RI_TURN_BOUNDARY, RP_PERSIST, RP_MODE_LIMITED, "turn boundaries for replay"},
    {RI_ASSESSMENT, RP_PERSIST, RP_MODE_EXTENDED, "quality assessments in extended mode"},
    {RI_COLLAB_EVENT, RP_PERSIST, RP_MODE_EXTENDED, "collaboration events in extended mode"},
};

#define RULE_COUNT NC_ARRAY_LEN(g_rules)

/* ── Core policy logic ── */

rollout_persist_t rollout_policy_decide(rollout_item_kind_t kind, int content_len) {
    for (int i = 0; i < RULE_COUNT; i++) {
        if (g_rules[i].kind == kind) {
            if (g_rules[i].decision == RP_SUMMARIZE && content_len <= TOOL_RESULT_TRUNCATE_TO)
                return RP_PERSIST;
            return g_rules[i].decision;
        }
    }
    return RP_PERSIST;
}

rollout_persist_t rollout_policy_decide_mode(rollout_item_kind_t kind, int content_len,
                                             int persist_mode) {
    for (int i = 0; i < RULE_COUNT; i++) {
        if (g_rules[i].kind == kind) {
            if (persist_mode < g_rules[i].min_mode)
                return RP_SKIP;
            if (g_rules[i].decision == RP_SUMMARIZE && content_len <= TOOL_RESULT_TRUNCATE_TO)
                return RP_PERSIST;
            return g_rules[i].decision;
        }
    }
    return RP_PERSIST;
}

const char *rollout_persist_name(rollout_persist_t p) {
    switch (p) {
    case RP_PERSIST:
        return "persist";
    case RP_SKIP:
        return "skip";
    case RP_REDACT:
        return "redact";
    case RP_SUMMARIZE:
        return "summarize";
    }
    return "unknown";
}

const char *rollout_item_kind_name(rollout_item_kind_t k) {
    switch (k) {
    case RI_USER_MSG:
        return "user_message";
    case RI_ASSISTANT:
        return "assistant";
    case RI_TOOL_CALL:
        return "tool_call";
    case RI_TOOL_RESULT:
        return "tool_result";
    case RI_SYSTEM_MSG:
        return "system";
    case RI_COMPACT_MARK:
        return "compact_marker";
    case RI_TURN_BOUNDARY:
        return "turn_boundary";
    case RI_ASSESSMENT:
        return "assessment";
    case RI_COLLAB_EVENT:
        return "collab_event";
    }
    return "unknown";
}

bool rollout_policy_should_persist(rollout_item_kind_t kind, int content_len) {
    rollout_persist_t d = rollout_policy_decide(kind, content_len);
    return (d == RP_PERSIST || d == RP_SUMMARIZE);
}

bool rollout_policy_should_persist_mode(rollout_item_kind_t kind, int content_len,
                                        int persist_mode) {
    rollout_persist_t d = rollout_policy_decide_mode(kind, content_len, persist_mode);
    return (d == RP_PERSIST || d == RP_SUMMARIZE);
}

bool rollout_policy_needs_summary(rollout_item_kind_t kind, int content_len) {
    return rollout_policy_decide(kind, content_len) == RP_SUMMARIZE;
}

/* ── Custom rule override ── */

int rollout_policy_set_rule(rollout_item_kind_t kind, rollout_persist_t decision) {
    for (int i = 0; i < RULE_COUNT; i++) {
        if (g_rules[i].kind == kind) {
            g_rules[i].decision = decision;
            g_rules[i].reason = "custom override";
            return 0;
        }
    }
    return -1;
}

char *rollout_policy_format(void) {
    size_t sz = 768;
    char *buf = malloc(sz);
    if (!buf)
        return NULL;

    int off = snprintf(buf, sz, "Rollout policy (%d rules):\n", RULE_COUNT);
    for (int i = 0; i < RULE_COUNT; i++)
        off += snprintf(
            buf + off, sz - (size_t)off, "  %s -> %s [%s] (%s)\n",
            rollout_item_kind_name(g_rules[i].kind), rollout_persist_name(g_rules[i].decision),
            g_rules[i].min_mode == RP_MODE_LIMITED ? "limited" : "extended", g_rules[i].reason);
    return buf;
}
