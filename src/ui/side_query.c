/*
 * side_query.c — Lightweight forked query without disrupting main context
 *
 * Spawns a one-shot API call sharing the current system prompt + last few
 * messages as read-only context.  The response is printed inline and
 * discarded — it never enters the main conversation history.
 */

#include "nevercode.h"
#include "conversation.h"
#include "api.h"
#include "config.h"
#include "terminal.h"

#define SQ_PREFIX "/btw"
/* NC_NC_SQ_PREFIX_LEN in defaults.h */

bool side_query_is_trigger(const char *input) {
    if (!input)
        return false;
    if (!nc_starts_with(input, SQ_PREFIX))
        return false;
    char ch = input[NC_SQ_PREFIX_LEN];
    return ch == '\0' || ch == ' ';
}

const char *side_query_extract(const char *input) {
    if (!input || !side_query_is_trigger(input))
        return NULL;
    const char *p = input + NC_SQ_PREFIX_LEN;
    while (*p == ' ')
        p++;
    return (*p) ? p : NULL;
}

static void sq_msg_set_text(message_t *m, message_role_t role, const char *text) {
    memset(m, 0, sizeof(*m));
    m->role = role;
    m->blocks = calloc(1, sizeof(content_block_t));
    if (!m->blocks)
        return;
    m->num_blocks = 1;
    m->cap_blocks = 1;
    m->blocks[0].type = CONTENT_TEXT;
    m->blocks[0].text = nc_xstrdup(text);
}

static void sq_msg_free(message_t *m) {
    for (int i = 0; i < m->num_blocks; i++) {
        free(m->blocks[i].text);
        free(m->blocks[i].tool_use_id);
        free(m->blocks[i].tool_name);
        cJSON_Delete(m->blocks[i].tool_input);
        free(m->blocks[i].image_data);
        free(m->blocks[i].image_media_type);
    }
    free(m->blocks);
}

int side_query_run(const char *question, const conversation_t *conv, const void *cfg_opaque) {
    const config_t *cfg = (const config_t *)cfg_opaque;

    if (!question || !*question) {
        term_color(T_DIM, "Usage: /btw <your question>\n");
        return -1;
    }

    term_color(T_DIM, "(side query — answering without tools)\n");

    /* Build side-query system prompt. */
    const char *prefix = "You are answering a quick side question. "
                         "Constraints:\n"
                         "- Answer concisely in one response.\n"
                         "- You have NO tools — cannot read files, run commands, or take actions.\n"
                         "- Base your answer only on the conversation context provided.\n"
                         "- If unsure, say so; do not promise follow-up actions.\n\n";

    strbuf_t sys;
    strbuf_init(&sys);
    strbuf_append_str(&sys, prefix);
    if (conv->system_prompt && conv->system_prompt[0]) {
        strbuf_append_str(&sys, "--- Project context ---\n");
        strbuf_append_str(&sys, conv->system_prompt);
    }

    /* Copy last few conversation messages + append the question. */
    int ctx_turns = SQ_CONTEXT_TURNS;
    int start = conv->len > ctx_turns ? conv->len - ctx_turns : 0;
    int nm = 0;
    for (int i = start; i < conv->len; i++) {
        if (conv->msgs[i].role == ROLE_SYSTEM)
            continue;
        nm++;
    }
    nm++; /* the question itself */

    message_t *msgs = calloc(nm, sizeof(message_t));
    if (!msgs) {
        strbuf_free(&sys);
        term_color(T_RED, "side query: allocation failed\n");
        return -1;
    }

    int idx = 0;
    for (int i = start; i < conv->len; i++) {
        const message_t *m = &conv->msgs[i];
        if (m->role == ROLE_SYSTEM)
            continue;

        /* Build a text-only copy of this message. */
        strbuf_t text;
        strbuf_init(&text);
        for (int b = 0; b < m->num_blocks; b++) {
            if (m->blocks[b].type == CONTENT_TEXT && m->blocks[b].text)
                strbuf_append_str(&text, m->blocks[b].text);
        }
        sq_msg_set_text(&msgs[idx], m->role, text.data ? text.data : "");
        strbuf_free(&text);
        idx++;
    }

    sq_msg_set_text(&msgs[idx], ROLE_USER, question);
    idx++;

    strbuf_t response;
    strbuf_init(&response);
    int in_tok = 0, out_tok = 0;

    int rc = api_stream_simple(config_base_url(cfg), config_api_key(cfg), config_model(cfg),
                               cfg->provider, sys.data, msgs, idx, &response, &in_tok, &out_tok);

    if (rc == 0 && response.data && response.len > 0) {
        printf("\n");
        term_color(T_CYAN, "[btw] ");
        printf("%s\n\n", response.data);
    } else {
        term_color(T_RED, "side query failed (rc=%d)\n", rc);
    }

    strbuf_free(&sys);
    strbuf_free(&response);
    for (int i = 0; i < idx; i++)
        sq_msg_free(&msgs[i]);
    free(msgs);
    return rc;
}
