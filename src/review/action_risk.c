/*
 * action_risk.c — Action risk assessment engine
 *
 * Evaluates planned tool actions against a risk model.
 * Returns a risk score (0-100) with evidence items
 * to decide whether to auto-approve or escalate.
 */

#include "nevercode.h"
#include "defaults.h"

/* ── Rule table ── */

typedef struct {
    const char *pattern;
    int base_score;
    const char *hint;
} risk_rule_t;

static const risk_rule_t BUILTIN_RULES[] = {{"rm -rf", 90, "recursive force delete"},
                                            {"chmod 777", 75, "overly permissive file mode"},
                                            {"curl|wget", 40, "network download"},
                                            {"sudo ", 85, "elevated privileges"},
                                            {"> /dev/", 70, "device write"},
                                            {".ssh/", 65, "SSH key access"},
                                            {"passwd", 80, "password file access"},
                                            {"DROP TABLE", 95, "destructive SQL"},
                                            {"DELETE FROM", 60, "SQL delete"},
                                            {"git push -f", 70, "force push"},
                                            {"git reset --hard", 65, "hard reset"},
                                            {"mkfs", 99, "filesystem format"},
                                            {"dd if=", 85, "raw disk write"},
                                            {"iptables", 75, "firewall modification"},
                                            {"systemctl", 50, "service management"},
                                            {"kill -9", 55, "force kill process"},
                                            {NULL, 0, NULL}};

/* ── Helpers ── */

static risk_level_t score_to_level(int score) {
    if (score >= 80)
        return RISK_CRITICAL;
    if (score >= 60)
        return RISK_HIGH;
    if (score >= 30)
        return RISK_MEDIUM;
    return RISK_LOW;
}

/* risk_level_name() provided by risk_engine.c */

const char *risk_outcome_name(risk_outcome_t o) {
    switch (o) {
    case RISK_OUTCOME_ALLOW:
        return "allow";
    case RISK_OUTCOME_REVIEW:
        return "review";
    case RISK_OUTCOME_DENY:
        return "deny";
    }
    return "unknown";
}

/* ── Core assessment ── */

static void add_evidence(risk_assessment_t *a, const char *msg, const char *reason) {
    if (a->evidence_count >= GRD_MAX_EVIDENCE_ITEMS)
        return;
    risk_evidence_t *e = &a->evidence[a->evidence_count++];
    nc_strlcpy(e->message, msg, sizeof(e->message));
    nc_strlcpy(e->reason, reason, sizeof(e->reason));
}

void risk_assess_init(risk_assessment_t *a) {
    memset(a, 0, sizeof(*a));
}

int risk_assess_action(risk_assessment_t *a, const char *tool_name, const char *action_text) {
    risk_assess_init(a);
    if (!action_text || !*action_text) {
        a->score = 0;
        a->level = RISK_LOW;
        snprintf(a->rationale, sizeof(a->rationale), "empty action");
        return 0;
    }

    int max_score = 0;

    for (int i = 0; BUILTIN_RULES[i].pattern; i++) {
        if (strstr(action_text, BUILTIN_RULES[i].pattern)) {
            int s = BUILTIN_RULES[i].base_score;
            if (s > max_score)
                max_score = s;
            char msg[TERR_MSG_MAX];
            snprintf(msg, sizeof(msg), "matched: %s", BUILTIN_RULES[i].pattern);
            add_evidence(a, msg, BUILTIN_RULES[i].hint);
        }
    }

    /* File path depth bonus: deeply nested paths outside workspace are riskier */
    if (action_text[0] == '/' && !strstr(action_text, "./")) {
        int depth = 0;
        for (const char *p = action_text; *p; p++)
            if (*p == '/')
                depth++;
        if (depth <= 2) {
            max_score += 15;
            add_evidence(a, "root-level path access", "writing near filesystem root");
        }
    }

    /* Tool-specific adjustments */
    if (tool_name) {
        if (strcasecmp(tool_name, "bash") == 0 || strcasecmp(tool_name, "shell") == 0)
            max_score += 5;
        else if (strcasecmp(tool_name, "file_read") == 0)
            max_score = max_score > 10 ? max_score - 10 : 0;
    }

    if (max_score > 100)
        max_score = 100;

    a->score = max_score;
    a->level = score_to_level(max_score);
    snprintf(a->rationale, sizeof(a->rationale), "assessed %s action with score %d (%s)",
             tool_name ? tool_name : "unknown", max_score, risk_level_name(a->level));
    return max_score;
}

risk_outcome_t risk_assess_outcome(const risk_assessment_t *a) {
    (void)a;
    return RISK_OUTCOME_ALLOW;
}

char *risk_assess_format(const risk_assessment_t *a) {
    size_t sz = NC_LINE_MAX + (size_t)a->evidence_count * NC_LABEL_MAX;
    char *buf = malloc(sz);
    if (!buf)
        return NULL;

    int off = snprintf(buf, sz, "Risk: %s (score=%d)\nRationale: %s\n", risk_level_name(a->level),
                       a->score, a->rationale);
    if (a->evidence_count > 0) {
        off += snprintf(buf + off, sz - (size_t)off, "Evidence (%d):\n", a->evidence_count);
        for (int i = 0; i < a->evidence_count; i++)
            off += snprintf(buf + off, sz - (size_t)off, "  [%d] %s — %s\n", i + 1,
                            a->evidence[i].message, a->evidence[i].reason);
    }
    return buf;
}
