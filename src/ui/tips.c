#include "nevercode.h"

typedef struct {
    const char *text;
    int cooldown;
} tip_def_t;

static const tip_def_t g_tips[] = {
    {"Use /compact to free context space when conversations get long", 5},
    {"Press TAB to auto-complete slash commands and @file paths", 3},
    {"Use /plan to enter read-only mode for safe exploration", 5},
    {"End a line with \\ to write multi-line messages", 5},
    {"Use @path/to/file to include file contents in your message", 3},
    {"Use /brief for shorter, more concise responses", 5},
    {"Use /skills to see all available built-in skills", 8},
    {"Use /doctor to check system dependencies and config", 10},
    {"Create custom agents in .agents/agents/*.md", 10},
    {"Use /memory to view stored memories, /remember to add", 8},
    {"Use /diff to see uncommitted git changes", 5},
    {"Use /init to auto-generate an AGENTS.md for your project", 10},
    {"Use /commit to create commits with AI-generated messages", 5},
    {"Use /review <PR#> for AI code review", 8},
    {"Use git worktrees to run multiple sessions in parallel", 10},
    {"Drag and drop images — use @image.png to include them", 8},
    {"Use /status to see context usage and session info", 5},
    {"Use /share to export the conversation as Markdown", 10},
    {"Use /snip to clear old tool results and free tokens", 5},
    {"Use --continue to resume your most recent session", 5},
    {"Use /rename to name sessions for easy /resume later", 8},
    {"Use /rewind to restore files to a previous checkpoint", 8},
    {"Run nevercode -p \"prompt\" for one-shot non-interactive use", 5},
    {"Use --json for structured JSON output in scripts", 8},
    {"Use /security-review for security audits of your changes", 10},
    {"Use /perf for performance analysis", 10},
    {"Use /test to auto-generate tests for recent changes", 10},
    {"Use /verify to double-check code changes", 8},
    {"Use /debug when stuck on a tricky bug", 8},
};

#define NUM_TIPS NC_ARRAY_LEN(g_tips)

static int g_shown[64];
static int g_num_shown = 0;
static int g_session_counter = 0;

void tips_init(void) {
    g_num_shown = 0;
    g_session_counter = 0;
}

static bool recently_shown(int idx) {
    for (int i = 0; i < g_num_shown; i++)
        if (g_shown[i] == idx)
            return true;
    return false;
}

const char *tips_get_random(void) {
    g_session_counter++;
    if (g_session_counter < 3 || g_session_counter % 3 != 0)
        return NULL;

    int attempts = 0;
    while (attempts < NUM_TIPS * 3) {
        int idx = (int)nc_rand_uniform(NUM_TIPS);
        if (!recently_shown(idx)) {
            if (g_num_shown < 64)
                g_shown[g_num_shown++] = idx;
            else {
                memmove(g_shown, g_shown + 1, 63 * sizeof(int));
                g_shown[63] = idx;
            }
            return g_tips[idx].text;
        }
        attempts++;
    }
    return NULL;
}

int tips_total(void) {
    return NUM_TIPS;
}

const char *tips_get_by_index(int idx) {
    if (idx < 0 || idx >= NUM_TIPS)
        return NULL;
    return g_tips[idx].text;
}
