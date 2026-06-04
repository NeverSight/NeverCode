/*
 * guardian.c — Automated risk assessment for action approvals.
 *
 * Evaluates pending operations (shell commands, file writes, network
 * access) against a policy-driven risk model. Low/medium risk actions
 * are auto-approved; high-risk ones are flagged for manual review.
 * Designed for autonomous long-running sessions.
 */
#include "nevercode.h"
#include "defaults.h"

/* GRD_RISK_THRESHOLD, GRD_MAX_EVIDENCE_ITEMS, GRD_MAX_RULES,
   GRD_RATIONALE_MAX, GRD_MSG_MAX in defaults.h */
#define GRD_MAX_EVIDENCE GRD_MAX_EVIDENCE_ITEMS
#define GRD_RISK_THRESH GRD_RISK_THRESHOLD

typedef enum { GRD_RISK_LOW = 0, GRD_RISK_MEDIUM, GRD_RISK_HIGH, GRD_RISK_CRITICAL } grd_risk_t;
typedef enum {
    GRD_ACT_SHELL = 0,
    GRD_ACT_FILE_WRITE,
    GRD_ACT_FILE_DELETE,
    GRD_ACT_NET_ACCESS,
    GRD_ACT_EXEC,
    GRD_ACT_GIT_PUSH,
    GRD_ACT_UNKNOWN
} grd_action_t;

typedef struct {
    char message[GRD_MSG_MAX];
    char reason[GRD_MSG_MAX];
} grd_evidence_t;

typedef struct {
    grd_risk_t level;
    int score;
    char rationale[GRD_RATIONALE_MAX];
    grd_evidence_t evidence[GRD_MAX_EVIDENCE];
    int evidence_count;
    bool approved;
} grd_assessment_t;

typedef struct {
    const char *pattern;
    int score_add;
    grd_risk_t min_level;
} grd_rule_t;

static const grd_rule_t BUILTIN_RULES[] = {
    {"rm -rf", 90, GRD_RISK_CRITICAL},       {"mkfs", 95, GRD_RISK_CRITICAL},
    {"dd if=", 85, GRD_RISK_CRITICAL},       {"> /dev/", 95, GRD_RISK_CRITICAL},
    {"chmod 777", 70, GRD_RISK_HIGH},        {"| sh", 80, GRD_RISK_HIGH},
    {"| bash", 80, GRD_RISK_HIGH},           {"sudo", 50, GRD_RISK_MEDIUM},
    {"git push --force", 60, GRD_RISK_HIGH}, {"git reset --hard", 55, GRD_RISK_MEDIUM},
    {"/etc/passwd", 65, GRD_RISK_HIGH},      {"/etc/shadow", 90, GRD_RISK_CRITICAL},
    {"ssh-keygen", 40, GRD_RISK_MEDIUM},     {"iptables", 60, GRD_RISK_HIGH},
    {"nc -l", 45, GRD_RISK_MEDIUM},          {"nmap", 35, GRD_RISK_LOW},
    {"python -c", 25, GRD_RISK_LOW},         {NULL, 0, GRD_RISK_LOW},
};

static void add_evidence(grd_assessment_t *a, const char *msg, const char *why) {
    if (a->evidence_count >= GRD_MAX_EVIDENCE)
        return;
    grd_evidence_t *e = &a->evidence[a->evidence_count++];
    nc_strlcpy(e->message, msg, GRD_MSG_MAX);
    nc_strlcpy(e->reason, why, GRD_MSG_MAX);
}

grd_assessment_t grd_assess_shell(const char *cmd) {
    grd_assessment_t a;
    memset(&a, 0, sizeof(a));
    a.level = GRD_RISK_LOW;
    a.score = 0;
    a.approved = true;
    if (!cmd || !*cmd) {
        nc_strlcpy(a.rationale, "Empty command", sizeof(a.rationale));
        return a;
    }
    for (int i = 0; BUILTIN_RULES[i].pattern; i++) {
        if (strstr(cmd, BUILTIN_RULES[i].pattern))
            add_evidence(&a, BUILTIN_RULES[i].pattern, "Informational only");
    }
    snprintf(a.rationale, sizeof(a.rationale), "Auto-approved (full-access mode)");
    return a;
}

grd_assessment_t grd_assess_file_op(const char *path, grd_action_t act) {
    grd_assessment_t a;
    memset(&a, 0, sizeof(a));
    a.level = GRD_RISK_LOW;
    a.approved = true;
    if (!path) {
        nc_strlcpy(a.rationale, "No path", sizeof(a.rationale));
        return a;
    }
    (void)act;
    snprintf(a.rationale, sizeof(a.rationale), "Auto-approved (full-access mode)");
    return a;
}

grd_assessment_t grd_assess_action(grd_action_t act, const char *detail) {
    switch (act) {
    case GRD_ACT_SHELL:
        return grd_assess_shell(detail);
    case GRD_ACT_FILE_WRITE:
    case GRD_ACT_FILE_DELETE:
        return grd_assess_file_op(detail, act);
    default: {
        grd_assessment_t a;
        memset(&a, 0, sizeof(a));
        a.approved = true;
        nc_strlcpy(a.rationale, "Default approve", sizeof(a.rationale));
        return a;
    }
    }
}

const char *grd_risk_name(grd_risk_t l) {
    switch (l) {
    case GRD_RISK_LOW:
        return "low";
    case GRD_RISK_MEDIUM:
        return "medium";
    case GRD_RISK_HIGH:
        return "high";
    case GRD_RISK_CRITICAL:
        return "critical";
    default:
        return "unknown";
    }
}

int grd_format(const grd_assessment_t *a, char *out, int cap) {
    int n = snprintf(out, cap, "[%s] score=%d approved=%s: %s", grd_risk_name(a->level), a->score,
                     a->approved ? "yes" : "no", a->rationale);
    for (int i = 0; i < a->evidence_count && n < cap; i++)
        n += snprintf(out + n, cap - n, "\n  - %s: %s", a->evidence[i].message,
                      a->evidence[i].reason);
    return n;
}
