/*
 * output_eval.c — Post-generation output quality scoring for long runs
 */
#include "harness.h"
#define EVAL_PASS 0
#define EVAL_WEAK 1
#define EVAL_INCOMPLETE 2
#define EVAL_OFF_TOPIC 3
#define EVAL_EMPTY 4
typedef struct {
    int total_evals, pass_count, weak_count, incomplete_count, off_topic_count, empty_count,
        consecutive_weak;
} eval_stats_t;
static eval_stats_t g_eval_stats;
void output_eval_init(void) {
    memset(&g_eval_stats, 0, sizeof(g_eval_stats));
}
void output_eval_free(void) {
    memset(&g_eval_stats, 0, sizeof(g_eval_stats));
}
static int oe_wc(const char *t) {
    if (!t)
        return 0;
    int c = 0;
    bool iw = false;
    for (const char *p = t; *p; p++) {
        if (isspace((unsigned char)*p))
            iw = false;
        else if (!iw) {
            iw = true;
            c++;
        }
    }
    return c;
}
static int oe_cb(const char *t) {
    if (!t)
        return 0;
    int c = 0;
    const char *p = t;
    while ((p = strstr(p, "```")) != NULL) {
        c++;
        p += 3;
    }
    return c / 2;
}
static bool oe_apology(const char *t) {
    if (!t)
        return false;
    static const char *m[] = {"I apologize", "I'm sorry",   "unfortunately",  "I cannot",
                              "I can't",     "I'm unable",  "I must decline", "I regret",
                              "I'm afraid",  "not able to", "unable to do",   NULL};
    for (int i = 0; m[i]; i++)
        if (strcasestr(t, m[i]))
            return true;
    return false;
}
static bool oe_refusal(const char *t) {
    if (!t)
        return false;
    static const char *m[] = {
        "I won't",
        "I will not",
        "cannot assist",
        "not appropriate",
        "against my",
        "I decline",
        "can't fulfill",
        "cannot fulfill",
        "can't help with",
        "cannot help with",
        "not permitted",
        "not allowed",
        "violates my",
        "goes against",
        "outside my",
        "beyond my scope",
        "I have to decline",
        "I must refuse",
        "won't be able",
        "\xe6\x88\x91\xe6\x97\xa0\xe6\xb3\x95", /* 我无法 */
        "\xe6\x88\x91\xe4\xb8\x8d\xe8\x83\xbd", /* 我不能 */
        "\xe6\x8a\xb1\xe6\xad\x89",             /* 抱歉 */
        "\xe5\xaf\xb9\xe4\xb8\x8d\xe8\xb5\xb7", /* 对不起 */
        "\xe4\xbd\x9c\xe4\xb8\xba"
        "AI",                                                                       /* 作为AI */
        "\xe4\xbd\x9c\xe4\xb8\xba\xe8\xaf\xad\xe8\xa8\x80\xe6\xa8\xa1\xe5\x9e\x8b", /* 作为语言模型
                                                                                     */
        "\xe8\xbf\x9d\xe5\x8f\x8d\xe8\xa7\x84\xe5\xae\x9a",                         /* 违反规定 */
        "\xe4\xb8\x8d\xe5\x90\x88\xe9\x80\x82",                                     /* 不合适 */
        NULL,
    };
    for (int i = 0; m[i]; i++)
        if (strcasestr(t, m[i]))
            return true;
    return false;
}
static bool oe_goal(const char *o, const char *g) {
    if (!o || !g)
        return false;
    const char *s = g;
    while (*s && isspace((unsigned char)*s))
        s++;
    char kw[NC_LABEL_MAX];
    size_t kl = 0;
    for (; *s && kl < sizeof(kw) - 1; s++) {
        if (isalnum((unsigned char)*s))
            kw[kl++] = (char)tolower((unsigned char)*s);
        else if (kl > 3)
            break;
    }
    kw[kl] = '\0';
    if (kl < 3)
        return true;
    return strcasestr(o, kw) != NULL;
}
oeval_result_t output_eval_check_ex(const char *output, const char *goal, int tc) {
    oeval_result_t r;
    memset(&r, 0, sizeof(r));
    r.tool_calls = tc;
    if (!output || !output[0]) {
        r.verdict = EVAL_EMPTY;
        r.confidence = 1.0;
        g_eval_stats.empty_count++;
        g_eval_stats.total_evals++;
        g_eval_stats.consecutive_weak++;
        return r;
    }
    r.word_count = oe_wc(output);
    r.code_blocks = oe_cb(output);
    r.has_apology = oe_apology(output);
    r.has_refusal = oe_refusal(output);
    r.mentions_goal = oe_goal(output, goal);
    bool short_expected = goal && (strcasestr(goal, "one word") || strcasestr(goal, "just") ||
                                   strcasestr(goal, "only") || strcasestr(goal, "single") ||
                                   strcasestr(goal, "briefly") || strcasestr(goal, "concise"));
    double sc = 1.0;
    if (r.has_refusal)
        sc -= OEVAL_PENALTY_REFUSAL;
    if (r.has_apology && r.word_count < 50)
        sc -= OEVAL_PENALTY_APOLOGY;
    if (!r.mentions_goal && goal && strlen(goal) > 10 && !short_expected)
        sc -= OEVAL_PENALTY_GOAL_MISS;
    if (r.word_count < 10 && r.tool_calls == 0 && r.code_blocks == 0 && !short_expected)
        sc -= OEVAL_PENALTY_SHORT;
    if (r.tool_calls > 0)
        sc += OEVAL_BONUS_TOOL;
    if (r.code_blocks > 0)
        sc += OEVAL_BONUS_CODE;
    if (sc < 0.0)
        sc = 0.0;
    if (sc > 1.0)
        sc = 1.0;
    r.confidence = sc;
    if (sc >= OEVAL_PASS_THRESHOLD) {
        r.verdict = EVAL_PASS;
        g_eval_stats.pass_count++;
        g_eval_stats.consecutive_weak = 0;
    } else if (sc >= OEVAL_MIN_CONFIDENCE) {
        r.verdict = EVAL_WEAK;
        g_eval_stats.weak_count++;
        g_eval_stats.consecutive_weak++;
    } else if (r.has_refusal || r.has_apology) {
        r.verdict = EVAL_OFF_TOPIC;
        g_eval_stats.off_topic_count++;
        g_eval_stats.consecutive_weak++;
    } else {
        r.verdict = EVAL_INCOMPLETE;
        g_eval_stats.incomplete_count++;
        g_eval_stats.consecutive_weak++;
    }
    g_eval_stats.total_evals++;
    return r;
}
const char *output_eval_verdict_name(int v) {
    switch (v) {
    case EVAL_PASS:
        return "pass";
    case EVAL_WEAK:
        return "weak";
    case EVAL_INCOMPLETE:
        return "incomplete";
    case EVAL_OFF_TOPIC:
        return "off_topic";
    case EVAL_EMPTY:
        return "empty";
    }
    return "unknown";
}
bool output_eval_needs_retry(int v) {
    return v == EVAL_INCOMPLETE || v == EVAL_OFF_TOPIC || v == EVAL_EMPTY;
}
bool output_eval_degrading(void) {
    return g_eval_stats.consecutive_weak >= OEVAL_DEGRADE_THRESHOLD;
}
char *output_eval_retry_prompt(int v, const char *goal) {
    strbuf_t sb;
    strbuf_init(&sb);
    switch (v) {
    case EVAL_EMPTY:
        strbuf_append_str(&sb, OEVAL_RETRY_EMPTY);
        break;
    case EVAL_OFF_TOPIC:
        strbuf_append_str(&sb, OEVAL_RETRY_OFF_TOPIC);
        if (goal)
            strbuf_appendf(&sb, "Goal: %s\n", goal);
        strbuf_append_str(&sb, "Proceed directly without disclaimers.");
        break;
    case EVAL_INCOMPLETE:
        strbuf_append_str(&sb, OEVAL_RETRY_INCOMPLETE);
        break;
    case EVAL_WEAK:
        strbuf_append_str(&sb, OEVAL_RETRY_WEAK);
        break;
    default:
        break;
    }
    if (sb.len == 0) {
        strbuf_free(&sb);
        return NULL;
    }
    return strbuf_detach(&sb);
}
int output_eval_total(void) {
    return g_eval_stats.total_evals;
}
int output_eval_pass_count(void) {
    return g_eval_stats.pass_count;
}
int output_eval_weak_count(void) {
    return g_eval_stats.weak_count;
}
int output_eval_consecutive_weak(void) {
    return g_eval_stats.consecutive_weak;
}
char *output_eval_format_stats(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(
        &sb, "Evaluator: %d total | %d pass | %d weak | %d incomplete | %d off-topic | %d empty",
        g_eval_stats.total_evals, g_eval_stats.pass_count, g_eval_stats.weak_count,
        g_eval_stats.incomplete_count, g_eval_stats.off_topic_count, g_eval_stats.empty_count);
    if (g_eval_stats.consecutive_weak > 0)
        strbuf_appendf(&sb, " | consecutive_weak: %d", g_eval_stats.consecutive_weak);
    return strbuf_detach(&sb);
}

void output_eval_snapshot(output_eval_snapshot_t *out) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    out->total_evals = g_eval_stats.total_evals;
    out->pass_count = g_eval_stats.pass_count;
    out->weak_count = g_eval_stats.weak_count;
    out->incomplete_count = g_eval_stats.incomplete_count;
    out->off_topic_count = g_eval_stats.off_topic_count;
    out->empty_count = g_eval_stats.empty_count;
    out->consecutive_weak = g_eval_stats.consecutive_weak;
}

void output_eval_restore(const output_eval_snapshot_t *snap) {
    output_eval_free();
    if (!snap) {
        output_eval_init();
        return;
    }
    g_eval_stats.total_evals = snap->total_evals;
    g_eval_stats.pass_count = snap->pass_count;
    g_eval_stats.weak_count = snap->weak_count;
    g_eval_stats.incomplete_count = snap->incomplete_count;
    g_eval_stats.off_topic_count = snap->off_topic_count;
    g_eval_stats.empty_count = snap->empty_count;
    g_eval_stats.consecutive_weak = snap->consecutive_weak;
}
