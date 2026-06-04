#include "nevercode.h"
typedef struct {
    const char *open_tag;
    const char *close_tag;
    ctx_frag_kind_t kind;
    bool exclude_memory;
} frag_def_t;

static const frag_def_t FRAG_DEFS[] = {
    {"<environment_context>", "</environment_context>", CTX_FRAG_ENV, false},
    {"<agents_md>", "</agents_md>", CTX_FRAG_INSTRUCTIONS, true},
    {"<skill_payload>", "</skill_payload>", CTX_FRAG_SKILL, true},
    {"<user_shell_command>", "</user_shell_command>", CTX_FRAG_SHELL_CMD, false},
    {"<turn_aborted>", "</turn_aborted>", CTX_FRAG_TURN_ABORT, false},
    {"<subagent_notification>", "</subagent_notification>", CTX_FRAG_SUBAGENT, false},
    {"<model_switch>", NULL, CTX_FRAG_MODEL_SWITCH, true},
    {"<personality_spec>", NULL, CTX_FRAG_PERSONALITY, true},
    {NULL, NULL, CTX_FRAG_NONE, false},
};

ctx_frag_kind_t ctx_frag_classify(const char *text) {
    if (!text)
        return CTX_FRAG_NONE;
    const char *t = nc_skip_ws(text);
    for (int i = 0; FRAG_DEFS[i].open_tag; i++) {
        size_t l = strlen(FRAG_DEFS[i].open_tag);
        if (strncasecmp(t, FRAG_DEFS[i].open_tag, l) == 0)
            return FRAG_DEFS[i].kind;
    }
    return CTX_FRAG_NONE;
}

bool ctx_frag_is_contextual(const char *text) {
    return ctx_frag_classify(text) != CTX_FRAG_NONE;
}

bool ctx_frag_exclude_memory(const char *text) {
    if (!text)
        return false;
    const char *t = nc_skip_ws(text);
    for (int i = 0; FRAG_DEFS[i].open_tag; i++) {
        size_t l = strlen(FRAG_DEFS[i].open_tag);
        if (strncasecmp(t, FRAG_DEFS[i].open_tag, l) == 0)
            return FRAG_DEFS[i].exclude_memory;
    }
    return false;
}

bool ctx_msg_is_contextual(const message_t *m) {
    if (!m || m->num_blocks == 0)
        return false;
    for (int i = 0; i < m->num_blocks; i++)
        if (m->blocks[i].type == CONTENT_TEXT && m->blocks[i].text &&
            ctx_frag_is_contextual(m->blocks[i].text))
            return true;
    return false;
}

bool ctx_msg_is_user_boundary(const message_t *m) {
    return m && m->role == ROLE_USER && !ctx_msg_is_contextual(m);
}

int ctx_msg_filter_content(const message_t *m, strbuf_t *out) {
    if (!m || !out)
        return 0;
    int kept = 0;
    for (int i = 0; i < m->num_blocks; i++) {
        if (m->blocks[i].type != CONTENT_TEXT || !m->blocks[i].text)
            continue;
        if (!ctx_frag_is_contextual(m->blocks[i].text)) {
            strbuf_append_str(out, m->blocks[i].text);
            kept++;
        }
    }
    return kept;
}

const char *ctx_frag_kind_name(ctx_frag_kind_t k) {
    switch (k) {
    case CTX_FRAG_NONE:
        return "none";
    case CTX_FRAG_ENV:
        return "environment";
    case CTX_FRAG_INSTRUCTIONS:
        return "instructions";
    case CTX_FRAG_SKILL:
        return "skill";
    case CTX_FRAG_SHELL_CMD:
        return "shell_command";
    case CTX_FRAG_TURN_ABORT:
        return "turn_abort";
    case CTX_FRAG_SUBAGENT:
        return "subagent";
    case CTX_FRAG_MODEL_SWITCH:
        return "model_switch";
    case CTX_FRAG_PERSONALITY:
        return "personality";
    case CTX_FRAG_HOOK_PROMPT:
        return "hook_prompt";
    }
    return "unknown";
}

int ctx_frag_count_in_conversation(const message_t *msgs, int n) {
    int c = 0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < msgs[i].num_blocks; j++)
            if (msgs[i].blocks[j].type == CONTENT_TEXT && msgs[i].blocks[j].text &&
                ctx_frag_is_contextual(msgs[i].blocks[j].text))
                c++;
    return c;
}

int ctx_frag_user_turn_count(const message_t *msgs, int n) {
    int c = 0;
    for (int i = 0; i < n; i++)
        if (ctx_msg_is_user_boundary(&msgs[i]))
            c++;
    return c;
}
