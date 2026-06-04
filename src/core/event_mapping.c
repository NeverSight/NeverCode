#include "nevercode.h"

static bool is_contextual_prefix(const char *text) {
    static const char *prefixes[] = {
        "<environment_context>", "<agents_md>",        "<skill_payload>",
        "<user_shell_command>",  "<turn_aborted>",     "<subagent_notification>",
        "<model_switch>",        "<personality_spec>", NULL};

    if (!text)
        return false;
    const char *t = text;
    while (*t == ' ' || *t == '\t' || *t == '\n')
        t++;
    for (int i = 0; prefixes[i]; i++) {
        size_t l = strlen(prefixes[i]);
        if (strncasecmp(t, prefixes[i], l) == 0)
            return true;
    }
    return false;
}

static bool msg_is_contextual(const message_t *m) {
    if (!m || m->num_blocks == 0)
        return false;
    for (int i = 0; i < m->num_blocks; i++)
        if (m->blocks[i].type == CONTENT_TEXT && m->blocks[i].text &&
            is_contextual_prefix(m->blocks[i].text))
            return true;
    return false;
}

static void extract_text(const message_t *m, strbuf_t *sb) {
    for (int i = 0; i < m->num_blocks; i++)
        if (m->blocks[i].type == CONTENT_TEXT && m->blocks[i].text &&
            !is_contextual_prefix(m->blocks[i].text))
            strbuf_append_str(sb, m->blocks[i].text);
}

int event_map_message(const message_t *m, int turn_id, turn_item_t *out) {
    if (!m || !out)
        return -1;
    memset(out, 0, sizeof(*out));
    out->turn_id = turn_id;

    if (msg_is_contextual(m)) {
        out->kind = TURN_ITEM_NONE;
        return 0;
    }

    switch (m->role) {
    case ROLE_USER: {
        strbuf_t sb;
        strbuf_init(&sb);
        extract_text(m, &sb);
        if (sb.len == 0) {
            strbuf_free(&sb);
            out->kind = TURN_ITEM_NONE;
            return 0;
        }
        out->kind = TURN_ITEM_USER_MSG;
        out->text = strbuf_detach(&sb);
        break;
    }
    case ROLE_ASSISTANT: {
        bool has_tool = false;
        for (int i = 0; i < m->num_blocks; i++)
            if (m->blocks[i].type == CONTENT_TOOL_USE) {
                has_tool = true;
                break;
            }
        if (has_tool) {
            for (int i = 0; i < m->num_blocks; i++)
                if (m->blocks[i].type == CONTENT_TOOL_USE) {
                    out->kind = TURN_ITEM_TOOL_CALL;
                    out->tool_name =
                        m->blocks[i].tool_name ? nc_xstrdup(m->blocks[i].tool_name) : NULL;
                    out->id =
                        m->blocks[i].tool_use_id ? nc_xstrdup(m->blocks[i].tool_use_id) : NULL;
                    break;
                }
        } else {
            strbuf_t sb;
            strbuf_init(&sb);
            extract_text(m, &sb);
            out->kind = TURN_ITEM_AGENT_MSG;
            out->text = strbuf_detach(&sb);
        }
        break;
    }
    case ROLE_TOOL: {
        out->kind = TURN_ITEM_TOOL_RESULT;
        strbuf_t sb;
        strbuf_init(&sb);
        extract_text(m, &sb);
        out->text = strbuf_detach(&sb);
        if (m->num_blocks > 0 && m->blocks[0].tool_use_id)
            out->id = nc_xstrdup(m->blocks[0].tool_use_id);
        break;
    }
    default:
        out->kind = TURN_ITEM_NONE;
        break;
    }
    return 0;
}

int event_map_conversation(const message_t *msgs, int n, turn_item_t *items, int max) {
    if (!msgs || !items || max <= 0)
        return 0;
    int c = 0, tid = 0;
    for (int i = 0; i < n && c < max; i++) {
        if (msgs[i].role == ROLE_USER)
            tid++;
        turn_item_t ti;
        if (event_map_message(&msgs[i], tid, &ti) == 0 && ti.kind != TURN_ITEM_NONE)
            items[c++] = ti;
    }
    return c;
}

bool event_map_is_contextual_text(const char *text) {
    return is_contextual_prefix(text);
}

bool event_map_is_user_boundary(const message_t *m) {
    return m && m->role == ROLE_USER && !msg_is_contextual(m);
}

const char *turn_item_kind_name(turn_item_kind_t k) {
    switch (k) {
    case TURN_ITEM_NONE:
        return "none";
    case TURN_ITEM_USER_MSG:
        return "user";
    case TURN_ITEM_AGENT_MSG:
        return "agent";
    case TURN_ITEM_REASONING:
        return "reasoning";
    case TURN_ITEM_TOOL_CALL:
        return "tool_call";
    case TURN_ITEM_TOOL_RESULT:
        return "tool_result";
    case TURN_ITEM_WEB_SEARCH:
        return "web_search";
    case TURN_ITEM_IMAGE_GEN:
        return "image_gen";
    case TURN_ITEM_HOOK_PROMPT:
        return "hook_prompt";
    }
    return "unknown";
}

char *event_map_format_items(const turn_item_t *items, int n) {
    strbuf_t sb;
    strbuf_init(&sb);
    for (int i = 0; i < n; i++) {
        strbuf_appendf(&sb, "[%d] %s", items[i].turn_id, turn_item_kind_name(items[i].kind));
        if (items[i].tool_name)
            strbuf_appendf(&sb, " tool=%s", items[i].tool_name);
        if (items[i].text) {
            int tl = (int)strlen(items[i].text);
            if (tl > 60)
                strbuf_appendf(&sb, " text=%.57s...", items[i].text);
            else
                strbuf_appendf(&sb, " text=%s", items[i].text);
        }
        strbuf_append_str(&sb, "\n");
    }
    return strbuf_detach(&sb);
}
