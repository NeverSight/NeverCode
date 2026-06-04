#include "nevercode.h"
#include "harness.h"

typedef struct {
    char name[COMPACT_TPL_NAME_MAX];
    char body[COMPACT_TPL_BODY_MAX];
} compact_tpl_entry_t;

static struct {
    compact_tpl_entry_t entries[COMPACT_TPL_MAX];
    int count;
    bool initialized;
} g_ctpl;

static const char *DEFAULT_TPL =
    "You are performing a CONTEXT CHECKPOINT. Create a structured handoff "
    "summary so another instance seamlessly resumes this task.\n\n"
    "Respond in the same language as the user's messages in the conversation.\n\n"
    "Use the following sections (skip empty ones):\n\n"
    "## Goal\n"
    "The primary objective the user requested.\n\n"
    "## Constraints & Preferences\n"
    "User preferences, coding style, invariants, or constraints discovered.\n\n"
    "## Progress\n"
    "### Done\n"
    "Completed work with specific outcomes, file paths, and commands run.\n"
    "### In Progress\n"
    "Work currently underway.\n"
    "### Blocked\n"
    "Any blockers or issues encountered.\n\n"
    "## Decisions\n"
    "Key choices made and their rationale.\n\n"
    "## Files Modified\n"
    "Paths of files created, edited, or deleted — with brief note on each.\n\n"
    "## Pending\n"
    "Prioritized list of remaining work.\n\n"
    "## Critical Context\n"
    "Specific values, error messages, configuration details, or data that "
    "would be lost without explicit preservation.\n\n"
    "Be specific — include file paths, command outputs, error messages, and "
    "concrete values rather than vague descriptions.\n\n"
    "Conversation has %d messages using approximately %d tokens.\n"
    "Session: %s";

static const char *SUMMARY_PREFIX =
    "Another agent started this task and produced the summary below. "
    "You also have access to the current state of all tools and files. "
    "Use this to build on existing work and avoid duplicating effort. "
    "Here is the summary -- use it to continue seamlessly:\n\n";

static const char *MID_TURN_PREFIX =
    "Context was compacted while processing your current request. "
    "The summary below captures all prior work. Your current task "
    "is still in progress — resume from where things left off:\n\n";

void compact_tpl_init(void) {
    memset(&g_ctpl, 0, sizeof(g_ctpl));
    g_ctpl.initialized = true;
}

void compact_tpl_free(void) {
    memset(&g_ctpl, 0, sizeof(g_ctpl));
}

int compact_tpl_add(const char *name, const char *body) {
    if (!g_ctpl.initialized || !name || !body || g_ctpl.count >= COMPACT_TPL_MAX)
        return -1;
    compact_tpl_entry_t *e = &g_ctpl.entries[g_ctpl.count];
    nc_strlcpy(e->name, name, sizeof(e->name));
    nc_strlcpy(e->body, body, sizeof(e->body));
    g_ctpl.count++;
    return 0;
}

const char *compact_tpl_get(const char *name) {
    if (!name)
        return NULL;
    for (int i = 0; i < g_ctpl.count; i++)
        if (strcmp(g_ctpl.entries[i].name, name) == 0)
            return g_ctpl.entries[i].body;
    return NULL;
}

const char *compact_tpl_default(void) {
    const char *custom = compact_tpl_get("default");
    return custom ? custom : DEFAULT_TPL;
}

const char *compact_tpl_summary_prefix(void) {
    const char *custom = compact_tpl_get("summary_prefix");
    return custom ? custom : SUMMARY_PREFIX;
}

const char *compact_tpl_mid_turn_prefix(void) {
    const char *custom = compact_tpl_get("mid_turn_prefix");
    return custom ? custom : MID_TURN_PREFIX;
}

int compact_tpl_count(void) {
    return g_ctpl.count;
}

char *compact_tpl_render(const char *name, int msg_count, int token_count, const char *session_id) {
    const char *tpl = name ? compact_tpl_get(name) : compact_tpl_default();
    if (!tpl)
        tpl = DEFAULT_TPL;
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, tpl, msg_count, token_count, session_id ? session_id : "unknown");

    char *goal_status =
        goal_tracker_format_status(harness_global() ? &harness_global()->daemon.goals : NULL);
    if (goal_status && goal_status[0]) {
        strbuf_append_str(&sb, "\n\n## Active Goals\n");
        strbuf_append_str(&sb, goal_status);
    }
    free(goal_status);

    return strbuf_detach(&sb);
}

char *compact_tpl_format_list(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Compact Templates: %d loaded\n", g_ctpl.count);
    for (int i = 0; i < g_ctpl.count; i++)
        strbuf_appendf(&sb, "  [%d] %s (%.40s...)\n", i, g_ctpl.entries[i].name,
                       g_ctpl.entries[i].body);
    return strbuf_detach(&sb);
}

int compact_tpl_summary_budget(int compressed_tokens) {
    int budget = (int)(compressed_tokens * COMPACT_SUMMARY_RATIO);
    if (budget < COMPACT_SUMMARY_MIN_TOKENS)
        budget = COMPACT_SUMMARY_MIN_TOKENS;
    if (budget > COMPACT_SUMMARY_MAX_TOKENS)
        budget = COMPACT_SUMMARY_MAX_TOKENS;
    return budget;
}

bool compact_tpl_has_prior_summary(const char *text) {
    if (!text)
        return false;
    return strstr(text, COMPACT_PREV_SUMMARY_MARKER) != NULL;
}

char *compact_tpl_build_iterative_prompt(const char *prior_summary, int msg_count, int token_count,
                                         const char *session_id) {
    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_str(&sb, "You are updating an existing context summary. A previous compaction "
                           "produced the summary below. Merge the NEW conversation turns into "
                           "this summary — do NOT discard information from the prior summary.\n\n"
                           "## Prior Summary\n\n");
    if (prior_summary)
        strbuf_append_str(&sb, prior_summary);
    strbuf_append_str(&sb, "\n\n## Instructions\n\n"
                           "1. Keep all sections from the prior summary.\n"
                           "2. Append new progress, decisions, files, and errors.\n"
                           "3. Move completed pending items to Progress Done.\n"
                           "4. Update the Pending section with current remaining work.\n"
                           "5. Preserve file paths, commands, and data verbatim.\n"
                           "6. Accumulate Critical Context across compactions.\n"
                           "7. Respond in the same language as the user.\n\n");
    strbuf_appendf(&sb,
                   "Conversation now has %d messages using ~%d tokens.\n"
                   "Session: %s",
                   msg_count, token_count, session_id ? session_id : "unknown");
    return strbuf_detach(&sb);
}
