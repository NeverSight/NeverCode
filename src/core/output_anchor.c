/*
 * output_anchor.c — Numeric output length anchors for system prompt.
 *
 * Appends concrete word-count limits to the system prompt to reduce
 * output verbosity. Research shows ~1.2% output token reduction
 * versus qualitative "be concise" instructions.
 */

#include "nevercode.h"

static struct {
    int inter_tool_words;
    int final_response_words;
    bool enabled;
} g_oa = {
    .inter_tool_words = OA_INTER_TOOL_WORDS,
    .final_response_words = OA_FINAL_RESPONSE_WORDS,
    .enabled = OA_DEFAULT_ENABLED,
};

void oa_init(void) {
    g_oa.inter_tool_words = OA_INTER_TOOL_WORDS;
    g_oa.final_response_words = OA_FINAL_RESPONSE_WORDS;
    g_oa.enabled = OA_DEFAULT_ENABLED;
}

void oa_set_enabled(bool on) {
    g_oa.enabled = on;
}

bool oa_enabled(void) {
    return g_oa.enabled;
}

void oa_set_limits(int inter_tool, int final_response) {
    if (inter_tool > 0)
        g_oa.inter_tool_words = inter_tool;
    if (final_response > 0)
        g_oa.final_response_words = final_response;
}

char *oa_build_section(void) {
    if (!g_oa.enabled)
        return NULL;
    char buf[NC_LABEL_MAX];
    snprintf(buf, sizeof(buf),
             "Length limits: keep text between tool calls to %d words or fewer. "
             "Keep final responses to %d words or fewer unless the task requires more detail.",
             g_oa.inter_tool_words, g_oa.final_response_words);
    return nc_xstrdup(buf);
}

int oa_inter_tool_limit(void) {
    return g_oa.inter_tool_words;
}

int oa_final_response_limit(void) {
    return g_oa.final_response_words;
}
