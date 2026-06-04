/*
 * plan_mode.c — Collaboration mode tools (plan / execute / pair).
 *
 * Provides mode-switching tools that adjust the agent's behavior:
 *   - Plan mode: read-only exploration, structured plan output
 *   - Execute mode: autonomous end-to-end delivery
 *   - Pair mode: iterative co-building with user in the loop
 */

#include "tools.h"
#include "repl.h"

#define SMODE_DEFAULT 0
#define SMODE_PLAN 2
#define SMODE_EXECUTE 3
#define SMODE_PAIR 4

static int g_pre_plan_mode = SMODE_DEFAULT;

static const char *ENTER_SCHEMA = "{"
                                  "\"type\":\"object\","
                                  "\"properties\":{},"
                                  "\"additionalProperties\":false"
                                  "}";

static const char *EXIT_SCHEMA = "{"
                                 "\"type\":\"object\","
                                 "\"properties\":{"
                                 "\"plan_summary\":{\"type\":\"string\","
                                 "\"description\":\"Brief summary of the plan or work done\"}"
                                 "}"
                                 "}";

/* ── Plan mode system prompt ── */
static const char *PLAN_PROMPT =
    "# Plan Mode (Active)\n\n"
    "Work in 3 phases toward a decision-complete plan:\n\n"
    "## Phase 1 — Ground in the environment\n"
    "Explore the codebase first. Resolve unknowns by inspection, not questions.\n"
    "Read files, search code, check configs before asking the user anything.\n\n"
    "## Phase 2 — Clarify intent\n"
    "Ask until you can state: goal, success criteria, scope, constraints.\n"
    "Bias toward questions over guessing for high-impact ambiguity.\n\n"
    "## Phase 3 — Design implementation\n"
    "Produce a decision-complete spec: approach, interfaces, data flow,\n"
    "edge cases, testing strategy, assumptions.\n\n"
    "## Rules\n"
    "- Non-mutating actions ONLY (read, search, analyze)\n"
    "- No file edits, no code generation, no side effects\n"
    "- When ready, wrap final plan in <proposed_plan>...</proposed_plan>\n"
    "- Use ExitPlanMode to return to normal mode";

/* ── Execute mode prompt ── */
static const char *EXECUTE_PROMPT =
    "# Execute Mode (Active)\n\n"
    "Execute the task independently and report progress.\n\n"
    "## Principles\n"
    "- Make reasonable assumptions when info is missing; state them briefly\n"
    "- Break work into milestones; verify as you go, not all at the end\n"
    "- Keep a running checklist of done / next / blocked\n"
    "- Think ahead: what will the user need to test or understand?\n"
    "- Minimize user wait time; spend seconds per turn, max 60s on research\n\n"
    "## Reporting\n"
    "- Map updates directly to work: what changed, what was verified, what remains\n"
    "- On failure: report what failed, what was tried, what comes next\n"
    "- On completion: summarize deliverables and how to validate";

/* ── Pair mode prompt ── */
static const char *PAIR_PROMPT =
    "# Pair Programming Mode (Active)\n\n"
    "Build together with the user in real-time.\n\n"
    "## Style\n"
    "- Take small, visible steps; avoid long-running operations unless asked\n"
    "- Check for alignment before major decisions\n"
    "- Explain reasoning step by step; adjust depth based on user signals\n"
    "- When multiple paths exist, present clear options with recommendations\n"
    "- Invite the user into decisions so choices feel empowering\n\n"
    "## Debugging\n"
    "- Treat you and the user as a team\n"
    "- Ask them for info you lack (error messages, screenshots, logs)";

static int enter_plan_exec(const cJSON *input, char **output, void *ctx) {
    (void)input;
    (void)ctx;

    if (query_mode_active()) {
        *output = nc_xstrdup("Cannot enter plan mode from a sub-agent context.");
        return 0;
    }

    int cur = session_mode_get();
    if (cur == SMODE_PLAN) {
        *output = nc_xstrdup("Already in plan mode. Use ExitPlanMode when ready.");
        return 0;
    }

    g_pre_plan_mode = cur;
    g_plan_mode = true;
    session_mode_set(SMODE_PLAN);

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "Entered plan mode.\n\n");
    strbuf_append_str(&sb, PLAN_PROMPT);
    *output = strbuf_detach(&sb);
    return 0;
}

static int exit_plan_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;

    if (!g_plan_mode && session_mode_get() != SMODE_PLAN) {
        *output = nc_xstrdup("Not in plan mode.");
        return 0;
    }

    g_plan_mode = false;
    session_mode_set(g_pre_plan_mode);
    g_pre_plan_mode = SMODE_DEFAULT;

    const char *summary = nc_cjson_str((cJSON *)input, "plan_summary", NULL);

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "Exited plan mode. All tools available.\n");
    if (summary) {
        strbuf_append_str(&sb, "\nPlan summary: ");
        strbuf_append_str(&sb, summary);
        strbuf_append_str(&sb, "\n");
    }
    strbuf_append_str(&sb, "\nProceed with implementation.");
    *output = strbuf_detach(&sb);
    return 0;
}

static int enter_execute_exec(const cJSON *input, char **output, void *ctx) {
    (void)input;
    (void)ctx;

    int cur = session_mode_get();
    if (cur == SMODE_EXECUTE) {
        *output = nc_xstrdup("Already in execute mode.");
        return 0;
    }

    g_plan_mode = false;
    session_mode_set(SMODE_EXECUTE);

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "Entered execute mode.\n\n");
    strbuf_append_str(&sb, EXECUTE_PROMPT);
    *output = strbuf_detach(&sb);
    return 0;
}

static int enter_pair_exec(const cJSON *input, char **output, void *ctx) {
    (void)input;
    (void)ctx;

    int cur = session_mode_get();
    if (cur == SMODE_PAIR) {
        *output = nc_xstrdup("Already in pair mode.");
        return 0;
    }

    g_plan_mode = false;
    session_mode_set(SMODE_PAIR);

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "Entered pair programming mode.\n\n");
    strbuf_append_str(&sb, PAIR_PROMPT);
    *output = strbuf_detach(&sb);
    return 0;
}

static int reset_mode_exec(const cJSON *input, char **output, void *ctx) {
    (void)input;
    (void)ctx;

    const char *prev = session_mode_name(session_mode_get());
    g_plan_mode = false;
    session_mode_set(SMODE_DEFAULT);

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb,
                   "Returned to default mode (was: %s). "
                   "All tools available, standard collaboration style.",
                   prev);
    *output = strbuf_detach(&sb);
    return 0;
}

static tool_def_t enter_plan_tool, exit_plan_tool;
static tool_def_t enter_execute_tool, enter_pair_tool, reset_mode_tool;

void tool_plan_mode_register(void) {
    /* Plan mode */
    enter_plan_tool.name = "EnterPlanMode";
    enter_plan_tool.description = "Enter plan mode for tasks needing exploration and design. "
                                  "Read-only: no file edits allowed until you exit.";
    enter_plan_tool.input_schema = cJSON_Parse(ENTER_SCHEMA);
    enter_plan_tool.execute = enter_plan_exec;
    enter_plan_tool.read_only = true;
    enter_plan_tool.concurrency_safe = true;
    enter_plan_tool.destructive = false;
    tools_register(&enter_plan_tool);

    exit_plan_tool.name = "ExitPlanMode";
    exit_plan_tool.description = "Exit plan mode, optionally providing a plan summary.";
    exit_plan_tool.input_schema = cJSON_Parse(EXIT_SCHEMA);
    exit_plan_tool.execute = exit_plan_exec;
    exit_plan_tool.read_only = true;
    exit_plan_tool.concurrency_safe = true;
    exit_plan_tool.destructive = false;
    tools_register(&exit_plan_tool);

    /* Execute mode */
    enter_execute_tool.name = "EnterExecuteMode";
    enter_execute_tool.description = "Enter execute mode for autonomous end-to-end task delivery. "
                                     "Makes assumptions, executes independently, reports progress.";
    enter_execute_tool.input_schema = cJSON_Parse(ENTER_SCHEMA);
    enter_execute_tool.execute = enter_execute_exec;
    enter_execute_tool.read_only = false;
    enter_execute_tool.concurrency_safe = true;
    enter_execute_tool.destructive = false;
    tools_register(&enter_execute_tool);

    /* Pair mode */
    enter_pair_tool.name = "EnterPairMode";
    enter_pair_tool.description = "Enter pair programming mode: small steps, frequent check-ins, "
                                  "collaborative decision-making with the user.";
    enter_pair_tool.input_schema = cJSON_Parse(ENTER_SCHEMA);
    enter_pair_tool.execute = enter_pair_exec;
    enter_pair_tool.read_only = false;
    enter_pair_tool.concurrency_safe = true;
    enter_pair_tool.destructive = false;
    tools_register(&enter_pair_tool);

    /* Reset to default */
    reset_mode_tool.name = "ResetMode";
    reset_mode_tool.description = "Reset to default collaboration mode from any active mode.";
    reset_mode_tool.input_schema = cJSON_Parse(ENTER_SCHEMA);
    reset_mode_tool.execute = reset_mode_exec;
    reset_mode_tool.read_only = false;
    reset_mode_tool.concurrency_safe = true;
    reset_mode_tool.destructive = false;
    tools_register(&reset_mode_tool);
}
