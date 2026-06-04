/*
 * sprint_contract.c — Structured acceptance criteria for iterative builds
 *
 * Implements a contract-based evaluation protocol where tasks are decomposed
 * into sprints, each with testable acceptance criteria that the evaluator
 * grades against using an adversarial generate-evaluate-iterate loop.
 *
 * Each contract contains:
 *   - A sprint description (what to build)
 *   - A list of acceptance criteria (how to verify)
 *   - Pass/fail verdicts per criterion
 *   - Evaluator feedback text
 *   - Iteration count and timing
 */

#include "harness.h"

typedef struct {
    char text[SPRINT_CRITERION_MAX];
    bool passed;
    char feedback[SPRINT_FEEDBACK_MAX];
} criterion_t;

typedef struct {
    char description[SPRINT_DESC_MAX];
    criterion_t criteria[SPRINT_MAX_CRITERIA];
    int num_criteria;
    int iteration;
    int max_iterations;
    bool finalized;
    long created_at;
    long finalized_at;
    int pass_count;
    int fail_count;
} contract_t;

static struct {
    contract_t contracts[SPRINT_MAX_CONTRACTS];
    int count;
    int active_idx;
    int total_iterations;
    int total_criteria_tested;
    int total_criteria_passed;
} g_sc;

void sprint_contract_init(void) {
    memset(&g_sc, 0, sizeof(g_sc));
    g_sc.active_idx = -1;
}

void sprint_contract_free(void) {
    memset(&g_sc, 0, sizeof(g_sc));
    g_sc.active_idx = -1;
}

int sprint_contract_create(const char *description, int max_iterations) {
    if (!description || g_sc.count >= SPRINT_MAX_CONTRACTS)
        return -1;
    contract_t *c = &g_sc.contracts[g_sc.count];
    memset(c, 0, sizeof(*c));
    nc_strlcpy(c->description, description, sizeof(c->description));
    c->max_iterations = (max_iterations > 0) ? max_iterations : SPRINT_DEFAULT_ITERATIONS;
    c->created_at = nc_wall_sec();
    c->iteration = 0;
    g_sc.active_idx = g_sc.count;
    return g_sc.count++;
}

int sprint_contract_add_criterion(int idx, const char *text) {
    if (idx < 0 || idx >= g_sc.count || !text)
        return -1;
    contract_t *c = &g_sc.contracts[idx];
    if (c->num_criteria >= SPRINT_MAX_CRITERIA || c->finalized)
        return -1;
    criterion_t *cr = &c->criteria[c->num_criteria];
    memset(cr, 0, sizeof(*cr));
    nc_strlcpy(cr->text, text, sizeof(cr->text));
    return c->num_criteria++;
}

int sprint_contract_grade(int contract_idx, int criterion_idx, bool passed, const char *feedback) {
    if (contract_idx < 0 || contract_idx >= g_sc.count)
        return -1;
    contract_t *c = &g_sc.contracts[contract_idx];
    if (criterion_idx < 0 || criterion_idx >= c->num_criteria)
        return -1;
    criterion_t *cr = &c->criteria[criterion_idx];
    cr->passed = passed;
    if (feedback)
        nc_strlcpy(cr->feedback, feedback, sizeof(cr->feedback));
    g_sc.total_criteria_tested++;
    if (passed)
        g_sc.total_criteria_passed++;
    return 0;
}

int sprint_contract_iterate(int idx) {
    if (idx < 0 || idx >= g_sc.count)
        return -1;
    contract_t *c = &g_sc.contracts[idx];
    if (c->finalized)
        return -1;
    c->iteration++;
    g_sc.total_iterations++;

    c->pass_count = 0;
    c->fail_count = 0;
    for (int i = 0; i < c->num_criteria; i++) {
        if (c->criteria[i].passed)
            c->pass_count++;
        else
            c->fail_count++;
    }

    if (c->fail_count == 0 || c->iteration >= c->max_iterations) {
        c->finalized = true;
        c->finalized_at = nc_wall_sec();
    }
    return c->iteration;
}

bool sprint_contract_all_pass(int idx) {
    if (idx < 0 || idx >= g_sc.count)
        return false;
    contract_t *c = &g_sc.contracts[idx];
    for (int i = 0; i < c->num_criteria; i++)
        if (!c->criteria[i].passed)
            return false;
    return c->num_criteria > 0;
}

bool sprint_contract_finalized(int idx) {
    if (idx < 0 || idx >= g_sc.count)
        return false;
    return g_sc.contracts[idx].finalized;
}

int sprint_contract_count(void) {
    return g_sc.count;
}

int sprint_contract_active(void) {
    return g_sc.active_idx;
}

int sprint_contract_criteria_count(int idx) {
    if (idx < 0 || idx >= g_sc.count)
        return 0;
    return g_sc.contracts[idx].num_criteria;
}

int sprint_contract_pass_count(int idx) {
    if (idx < 0 || idx >= g_sc.count)
        return 0;
    return g_sc.contracts[idx].pass_count;
}

int sprint_contract_fail_count(int idx) {
    if (idx < 0 || idx >= g_sc.count)
        return 0;
    return g_sc.contracts[idx].fail_count;
}

int sprint_contract_iteration(int idx) {
    if (idx < 0 || idx >= g_sc.count)
        return 0;
    return g_sc.contracts[idx].iteration;
}

char *sprint_contract_build_feedback(int idx) {
    if (idx < 0 || idx >= g_sc.count)
        return NULL;
    contract_t *c = &g_sc.contracts[idx];
    if (c->num_criteria == 0)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Sprint: %s  (iteration %d/%d)\n", c->description, c->iteration,
                   c->max_iterations);

    int failing = 0;
    for (int i = 0; i < c->num_criteria; i++) {
        criterion_t *cr = &c->criteria[i];
        const char *mark = cr->passed ? "PASS" : "FAIL";
        strbuf_appendf(&sb, "  [%s] %s\n", mark, cr->text);
        if (!cr->passed) {
            failing++;
            if (cr->feedback[0])
                strbuf_appendf(&sb, "        -> %s\n", cr->feedback);
        }
    }
    strbuf_appendf(&sb, "\n%d/%d criteria passing", c->pass_count, c->num_criteria);
    if (failing > 0 && c->iteration < c->max_iterations)
        strbuf_appendf(&sb, " — fix the %d failing criteria and re-submit", failing);
    else if (failing > 0)
        strbuf_appendf(&sb, " — max iterations reached, finalizing with failures");
    strbuf_append_str(&sb, "\n");
    return strbuf_detach(&sb);
}

char *sprint_contract_build_prompt(int idx) {
    if (idx < 0 || idx >= g_sc.count)
        return NULL;
    contract_t *c = &g_sc.contracts[idx];

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "You are working on a sprint contract. ");
    strbuf_appendf(&sb, "Task: %s\n\n", c->description);
    strbuf_append_str(&sb, "Acceptance criteria that must ALL pass:\n");
    for (int i = 0; i < c->num_criteria; i++) {
        criterion_t *cr = &c->criteria[i];
        strbuf_appendf(&sb, "  %d. %s", i + 1, cr->text);
        if (cr->feedback[0])
            strbuf_appendf(&sb, " [previous feedback: %s]", cr->feedback);
        strbuf_append_str(&sb, "\n");
    }
    if (c->iteration > 0) {
        strbuf_appendf(&sb, "\nThis is iteration %d of %d. ", c->iteration, c->max_iterations);
        strbuf_appendf(&sb, "%d/%d criteria currently passing.\n", c->pass_count, c->num_criteria);
        strbuf_append_str(&sb, "Focus on fixing the failing criteria.\n");
    }
    return strbuf_detach(&sb);
}

char *sprint_contract_format_status(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Contracts: %d  Iterations: %d  Criteria: %d/%d passed\n", g_sc.count,
                   g_sc.total_iterations, g_sc.total_criteria_passed, g_sc.total_criteria_tested);
    for (int i = 0; i < g_sc.count; i++) {
        contract_t *c = &g_sc.contracts[i];
        const char *state = c->finalized ? (c->fail_count == 0 ? "done" : "capped") : "active";
        strbuf_appendf(&sb, "  [%d] %s (%s, iter %d/%d, %d/%d pass)\n", i, c->description, state,
                       c->iteration, c->max_iterations, c->pass_count, c->num_criteria);
    }
    return strbuf_detach(&sb);
}

int sprint_contract_save(int idx, const char *dir) {
    if (idx < 0 || idx >= g_sc.count || !dir)
        return -1;
    contract_t *c = &g_sc.contracts[idx];
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return -1;

    cJSON_AddStringToObject(root, "description", c->description);
    cJSON_AddNumberToObject(root, "iteration", c->iteration);
    cJSON_AddNumberToObject(root, "max_iterations", c->max_iterations);
    cJSON_AddBoolToObject(root, "finalized", c->finalized);

    cJSON *arr = cJSON_AddArrayToObject(root, "criteria");
    for (int i = 0; i < c->num_criteria; i++) {
        criterion_t *cr = &c->criteria[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "text", cr->text);
        cJSON_AddBoolToObject(item, "passed", cr->passed);
        if (cr->feedback[0])
            cJSON_AddStringToObject(item, "feedback", cr->feedback);
        cJSON_AddItemToArray(arr, item);
    }

    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/sprint_contract_%d.json", dir, idx);
    nc_mkdir_p(dir);

    int rc = nc_json_save(path, root);
    cJSON_Delete(root);
    return rc;
}
