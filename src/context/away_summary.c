/*
 * away_summary.c — "While you were away" session recap
 *
 * Detects terminal idle (blur or no input for N minutes), then
 * generates a short 1-3 sentence recap via a lightweight API call
 * using the last ~30 messages of conversation context.
 */

#include "nevercode.h"
#include "api.h"
#include "conversation.h"

/* AWAY_IDLE_SECONDS .. AWAY_MAX_PROMPT_LEN in defaults.h */

static time_t s_last_activity;
static bool s_away_generated;
static bool s_enabled = true;

void away_summary_init(void) {
    s_last_activity = nc_time_now();
    s_away_generated = false;
    s_enabled = true;
}

void away_summary_touch(void) {
    s_last_activity = nc_time_now();
    s_away_generated = false;
}

void away_summary_set_enabled(bool enabled) {
    s_enabled = enabled;
}

bool away_summary_enabled(void) {
    return s_enabled;
}

long away_summary_idle_seconds(void) {
    return (long)(nc_time_now() - s_last_activity);
}

bool away_summary_should_generate(void) {
    if (!s_enabled)
        return false;
    if (s_away_generated)
        return false;
    return away_summary_idle_seconds() >= AWAY_IDLE_SECONDS;
}

static char *build_away_prompt(const char *memory_text) {
    char prompt[AWAY_MAX_PROMPT_LEN];
    int off = 0;

    if (memory_text && memory_text[0]) {
        off += snprintf(prompt + off, sizeof(prompt) - off,
                        "Session memory (broader context):\n%s\n\n", memory_text);
    }
    snprintf(prompt + off, sizeof(prompt) - off,
             "The user stepped away and is coming back. "
             "Write exactly 1-3 short sentences. "
             "Start by stating the high-level task — what they are building or debugging, "
             "not implementation details. "
             "Next: the concrete next step. "
             "Skip status reports and commit recaps.");

    return nc_xstrdup(prompt);
}

char *away_summary_generate(const void *conv_ptr, const char *memory_text, const char *base_url,
                            const char *api_key, const char *model, provider_type_t provider) {
    const conversation_t *conv = (const conversation_t *)conv_ptr;
    if (!conv || conv->len == 0)
        return NULL;

    int start = conv->len > AWAY_MSG_WINDOW ? conv->len - AWAY_MSG_WINDOW : 0;

    char *prompt = build_away_prompt(memory_text);
    if (!prompt)
        return NULL;

    message_t recap_msgs[AWAY_MSG_WINDOW + 1];
    int cnt = 0;
    for (int i = start; i < conv->len && cnt < AWAY_MSG_WINDOW; i++) {
        recap_msgs[cnt++] = conv->msgs[i];
    }

    recap_msgs[cnt].role = ROLE_USER;
    recap_msgs[cnt].blocks = calloc(1, sizeof(content_block_t));
    if (!recap_msgs[cnt].blocks)
        return NULL;
    recap_msgs[cnt].blocks[0].type = CONTENT_TEXT;
    recap_msgs[cnt].blocks[0].text = prompt;
    recap_msgs[cnt].num_blocks = 1;
    recap_msgs[cnt].cap_blocks = 1;
    cnt++;

    strbuf_t response;
    strbuf_init(&response);

    int in_tok = 0, out_tok = 0;
    int rc = api_stream_simple(base_url, api_key, model, provider, NULL, recap_msgs, cnt, &response,
                               &in_tok, &out_tok);

    free(recap_msgs[cnt - 1].blocks);
    free(prompt);

    if (rc != 0 || response.len == 0) {
        strbuf_free(&response);
        return NULL;
    }

    s_away_generated = true;

    if (response.len > AWAY_MAX_RESPONSE_LEN) {
        response.data[AWAY_MAX_RESPONSE_LEN] = '\0';
        response.len = AWAY_MAX_RESPONSE_LEN;
    }

    return strbuf_detach(&response);
}

char *away_summary_format(const char *summary) {
    if (!summary || !summary[0])
        return NULL;

    size_t len = strlen(summary) + 64;
    char *out = malloc(len);
    if (!out)
        return NULL;
    snprintf(out, len, "[While you were away] %s", summary);
    return out;
}

const char *away_summary_prompt_text(void) {
    return "The user stepped away and is coming back. "
           "Write exactly 1-3 short sentences. "
           "Start by stating the high-level task — what they are building or debugging, "
           "not implementation details. "
           "Next: the concrete next step. "
           "Skip status reports and commit recaps.";
}
