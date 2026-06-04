#include "nevercode.h"
#include "config.h"
#include "conversation.h"
#include "api.h"
#include "terminal.h"

#define SM_EXTRACT_INTERVAL_TURNS SM_INTERVAL_TURNS
#define SM_MIN_TOOL_CALLS SM_MIN_CALLS
#define SM_MAX_CONTENT_TOKENS SM_MAX_TOKENS
/* SESSION_MEMORY_FILENAME in defaults.h */
#define SM_FILE SESSION_MEMORY_FILENAME

typedef struct {
    char *content;
    char *session_id;
    int last_extracted_turn;
    int extraction_count;
    int tool_calls_since;
    bool initialized;
    bool enabled;
    time_t last_extract_time;
} session_mem_ctx_t;

static session_mem_ctx_t g_sm = {0};

static const char *SM_EXTRACT_PROMPT =
    "You are a session memory extractor. Maintain a concise markdown file "
    "that captures the most important context from this conversation. "
    "This file survives conversation compactions and helps preserve "
    "critical context across turns.\n\n"
    "## What counts as high-signal memory\n"
    "- User operating preferences (what they repeatedly ask for or correct)\n"
    "- High-leverage procedural knowledge (shortcuts, paths, commands)\n"
    "- Decision triggers (signals that cause a pivot)\n"
    "- Durable evidence about the environment and workflow\n"
    "- Task outcomes (success / partial / fail) with specific evidence\n"
    "- Failure shields: symptom -> cause -> fix\n\n"
    "## What to skip\n"
    "- Generic advice, temporary values, one-off status updates\n"
    "- Secrets or credentials (replace with [REDACTED])\n"
    "- Large raw tool outputs — use compact summaries + key snippets\n\n"
    "## Format\n"
    "- Maximum 60 lines, bullet points and short phrases\n"
    "- Structure: ## Goal, ## Preferences, ## Changes, ## Decisions, "
    "## Pending, ## Failures\n"
    "- Update existing content — do not just append\n"
    "- Remove stale information\n"
    "- Preserve exact file paths, commands, and error messages\n"
    "- Do NOT call any tools\n\n"
    "Respond with ONLY the updated session memory markdown content.";

void session_memory_init(void) {
    memset(&g_sm, 0, sizeof(g_sm));
    g_sm.enabled = true;
    g_sm.initialized = true;
}

void session_memory_free(void) {
    free(g_sm.content);
    free(g_sm.session_id);
    memset(&g_sm, 0, sizeof(g_sm));
}

void session_memory_set_enabled(bool enabled) {
    g_sm.enabled = enabled;
}

bool session_memory_enabled(void) {
    return g_sm.initialized && g_sm.enabled;
}

void session_memory_set_session(const char *session_id) {
    free(g_sm.session_id);
    g_sm.session_id = session_id ? nc_xstrdup(session_id) : NULL;
}

static char *session_memory_path(void) {
    char path[NC_PATH_MAX];
    if (g_sm.session_id) {
        char sub[NC_MSG_MAX];
        snprintf(sub, sizeof(sub), "sessions/%s/%s", g_sm.session_id, SM_FILE);
        config_home_path(path, sizeof(path), sub);
    } else {
        config_home_path(path, sizeof(path), SM_FILE);
    }
    return nc_xstrdup(path);
}

int session_memory_load(void) {
    char *path = session_memory_path();
    char *buf = strbuf_read_file(path, NULL);
    free(path);
    if (!buf)
        return -1;

    free(g_sm.content);
    g_sm.content = buf;
    return 0;
}

int session_memory_save(void) {
    if (!g_sm.content || !g_sm.content[0])
        return -1;
    char *path = session_memory_path();
    nc_mkdir_p_for_file(path);
    FILE *f = fopen(path, "w");
    free(path);
    if (!f)
        return -1;
    int rc = (fputs(g_sm.content, f) == EOF || fflush(f)) ? -1 : 0;
    fclose(f);
    return rc;
}

const char *session_memory_content(void) {
    return g_sm.content;
}

int session_memory_extraction_count(void) {
    return g_sm.extraction_count;
}

void session_memory_record_tool_call(void) {
    g_sm.tool_calls_since++;
}

bool session_memory_should_extract(int current_turn) {
    if (!g_sm.enabled)
        return false;
    if (current_turn - g_sm.last_extracted_turn < SM_EXTRACT_INTERVAL_TURNS)
        return false;
    if (g_sm.tool_calls_since < SM_MIN_TOOL_CALLS)
        return false;
    time_t now = nc_time_now();
    if (g_sm.last_extract_time > 0 && (now - g_sm.last_extract_time) < 30)
        return false;
    return true;
}

static void sm_stream_cb(const stream_event_t *ev, void *ud) {
    strbuf_t *sb = ud;
    if (ev->type == STREAM_EVENT_TEXT_DELTA && ev->text)
        strbuf_append_str(sb, ev->text);
}

char *session_memory_extract(const void *conv_ptr, const char *base_url, const char *api_key,
                             const char *model, provider_type_t provider) {
    const conversation_t *conv = conv_ptr;
    if (!conv || conv->len < 4)
        return NULL;

    strbuf_t user_msg;
    strbuf_init(&user_msg);

    if (g_sm.content && g_sm.content[0]) {
        strbuf_append_str(&user_msg, "## Current Session Memory\n\n");
        strbuf_append_str(&user_msg, g_sm.content);
        strbuf_append_str(&user_msg, "\n\n---\n\n");
    }

    strbuf_append_str(&user_msg, "## Recent Conversation (extract key info)\n\n");

    int start = conv->len > 20 ? conv->len - 20 : 0;
    int tok_budget = SM_MAX_CONTENT_TOKENS * 4;

    for (int i = start; i < conv->len && tok_budget > 0; i++) {
        const message_t *m = &conv->msgs[i];
        const char *role = m->role == ROLE_USER        ? "User"
                           : m->role == ROLE_ASSISTANT ? "Assistant"
                                                       : "Tool";

        for (int j = 0; j < m->num_blocks; j++) {
            const content_block_t *b = &m->blocks[j];
            if (b->type == CONTENT_TEXT && b->text) {
                strbuf_appendf(&user_msg, "**%s**: ", role);
                size_t tlen = strlen(b->text);
                if ((int)tlen > tok_budget)
                    tlen = (size_t)tok_budget;
                if (tlen > NC_PREVIEW_SPLIT_THRESHOLD) {
                    strbuf_append(&user_msg, b->text, NC_PREVIEW_HALF);
                    strbuf_append_str(&user_msg, " [...] ");
                    strbuf_append(&user_msg, b->text + tlen - NC_PREVIEW_HALF, NC_PREVIEW_HALF);
                } else {
                    strbuf_append(&user_msg, b->text, tlen);
                }
                strbuf_append_str(&user_msg, "\n\n");
                tok_budget -= (int)tlen;
            } else if (b->type == CONTENT_TOOL_USE && b->tool_name) {
                strbuf_appendf(&user_msg, "[Tool: %s]\n", b->tool_name);
                tok_budget -= 20;
            }
        }
    }

    strbuf_append_str(&user_msg, "\n---\n\nUpdate the session memory based on the above. "
                                 "Output ONLY the updated markdown.");

    message_t msg;
    message_init(&msg, ROLE_USER);
    message_add_text(&msg, user_msg.data ? user_msg.data : "");
    strbuf_free(&user_msg);

    config_t tmp_cfg;
    config_init(&tmp_cfg);
    tmp_cfg.provider = provider;
    if (provider == PROVIDER_COMPAT) {
        tmp_cfg.compat_base_url = base_url ? nc_xstrdup(base_url) : NULL;
        tmp_cfg.compat_api_key = api_key ? nc_xstrdup(api_key) : NULL;
    } else {
        tmp_cfg.native_base_url = base_url ? nc_xstrdup(base_url) : NULL;
        tmp_cfg.native_api_key = api_key ? nc_xstrdup(api_key) : NULL;
    }
    tmp_cfg.model = model ? nc_xstrdup(model) : nc_xstrdup(nc_provider_default_model(provider));
    tmp_cfg.max_tokens = SM_EXTRACT_MAX_TOKENS;

    strbuf_t result;
    strbuf_init(&result);

    int rc =
        api_stream_request(&msg, 1, SM_EXTRACT_PROMPT, &tmp_cfg, NULL, 0, sm_stream_cb, &result);

    message_free(&msg);
    config_free(&tmp_cfg);

    if (rc != 0 || result.len == 0) {
        strbuf_free(&result);
        return NULL;
    }

    free(g_sm.content);
    g_sm.content = strbuf_detach(&result);
    g_sm.extraction_count++;
    g_sm.tool_calls_since = 0;
    g_sm.last_extract_time = nc_time_now();

    session_memory_save();

    return g_sm.content ? nc_xstrdup(g_sm.content) : NULL;
}

char *session_memory_for_compact(void) {
    if (!g_sm.content || !g_sm.content[0])
        return NULL;
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "\n\n## Session Memory (preserved across compaction)\n\n");
    strbuf_append_str(&sb, g_sm.content);
    return strbuf_detach(&sb);
}

char *session_memory_for_prompt(void) {
    if (!g_sm.content || !g_sm.content[0])
        return NULL;
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "# Session Memory\n"
                           "The following notes were extracted from this conversation "
                           "and may help you maintain context:\n\n");
    strbuf_append_str(&sb, g_sm.content);
    return strbuf_detach(&sb);
}

void session_memory_mark_extracted(int turn) {
    g_sm.last_extracted_turn = turn;
}
