/*
 * prompt_suggest.c — Next-prompt suggestion engine
 *
 * After each assistant turn completes, predicts what the user would
 * naturally type next. Uses a lightweight forked API call with the
 * conversation context. Filters out evaluative, AI-voice, and meta
 * responses before presenting to the user.
 */

#include "nevercode.h"
#include "api.h"
#include "conversation.h"

/* Uses SUGGEST_MAX_WORDS, SUGGEST_MAX_CHARS, SUGGEST_MIN_TURNS,
   SUGGEST_MSG_WINDOW from defaults.h (via nevercode.h) */

static bool s_ps_enabled = false;
static char *s_current_suggestion = NULL;

static const char SUGGESTION_PROMPT[] =
    "[SUGGESTION MODE: Suggest what the user might naturally type next.]\n"
    "\n"
    "FIRST: Look at the user's recent messages and original request.\n"
    "\n"
    "Your job is to predict what THEY would type - not what you think they should do.\n"
    "\n"
    "THE TEST: Would they think \"I was just about to type that\"?\n"
    "\n"
    "EXAMPLES:\n"
    "User asked \"fix the bug and run tests\", bug is fixed -> \"run the tests\"\n"
    "After code written -> \"try it out\"\n"
    "Assistant offers options -> suggest the one the user would likely pick\n"
    "Assistant asks to continue -> \"yes\" or \"go ahead\"\n"
    "Task complete, obvious follow-up -> \"commit this\" or \"push it\"\n"
    "After error or misunderstanding -> silence (let them assess/correct)\n"
    "\n"
    "Be specific: \"run the tests\" beats \"continue\".\n"
    "\n"
    "NEVER SUGGEST:\n"
    "- Evaluative (\"looks good\", \"thanks\")\n"
    "- Questions (\"what about...?\")\n"
    "- AI-voice (\"Let me...\", \"I'll...\", \"Here's...\")\n"
    "- New ideas they didn't ask about\n"
    "- Multiple sentences\n"
    "\n"
    "Stay silent if the next step isn't obvious from what the user said.\n"
    "\n"
    "Format: 2-12 words, match the user's style. Or nothing.\n"
    "\n"
    "Reply with ONLY the suggestion, no quotes or explanation.";

void prompt_suggest_init(bool enabled) {
    s_ps_enabled = enabled;
    free(s_current_suggestion);
    s_current_suggestion = NULL;
}

void prompt_suggest_set_enabled(bool enabled) {
    s_ps_enabled = enabled;
    if (!enabled) {
        free(s_current_suggestion);
        s_current_suggestion = NULL;
    }
}

bool prompt_suggest_enabled(void) {
    return s_ps_enabled;
}

void prompt_suggest_clear(void) {
    free(s_current_suggestion);
    s_current_suggestion = NULL;
}

const char *prompt_suggest_current(void) {
    return s_current_suggestion;
}

static int count_words(const char *s) {
    int n = 0;
    bool in_word = false;
    for (; *s; s++) {
        if (isspace((unsigned char)*s)) {
            in_word = false;
        } else if (!in_word) {
            n++;
            in_word = true;
        }
    }
    return n;
}

static const char *ALLOWED_SINGLE[] = {"yes",      "yeah",  "yep",  "yea",    "yup",    "sure",
                                       "ok",       "okay",  "push", "commit", "deploy", "stop",
                                       "continue", "check", "exit", "quit",   "no",     NULL};

prompt_filter_t prompt_suggest_filter(const char *suggestion) {
    if (!suggestion || !suggestion[0])
        return FILTER_EMPTY;

    size_t len = strlen(suggestion);
    int words = count_words(suggestion);

    if (strcmp(suggestion, "done") == 0)
        return FILTER_META;

    /* Meta-text: model tries to "stay silent" */
    if (nc_starts_with_ci(suggestion, "nothing to suggest") ||
        nc_starts_with_ci(suggestion, "no suggestion") ||
        nc_contains_ci(suggestion, "stay silent") || nc_contains_ci(suggestion, "silence is"))
        return FILTER_META;

    /* Wrapped in parens/brackets: (no suggestion), [silence] */
    if ((suggestion[0] == '(' && suggestion[len - 1] == ')') ||
        (suggestion[0] == '[' && suggestion[len - 1] == ']'))
        return FILTER_META;

    /* Error messages leaked through */
    if (nc_starts_with_ci(suggestion, "api error:") ||
        nc_starts_with_ci(suggestion, "prompt is too long") ||
        nc_starts_with_ci(suggestion, "request timed out"))
        return FILTER_ERROR;

    /* Prefixed label: "Suggestion: ..." */
    {
        const char *p = suggestion;
        while (*p && isalpha((unsigned char)*p))
            p++;
        if (*p == ':' && *(p + 1) == ' ')
            return FILTER_PREFIX;
    }

    /* Too few words (unless slash command or allowed single word) */
    if (words < 2 && suggestion[0] != '/') {
        bool allowed = false;
        char lower[NC_FMT_MAX];
        size_t i;
        for (i = 0; i < len && i < sizeof(lower) - 1; i++)
            lower[i] = (char)tolower((unsigned char)suggestion[i]);
        lower[i] = '\0';

        for (int k = 0; ALLOWED_SINGLE[k]; k++) {
            if (strcmp(lower, ALLOWED_SINGLE[k]) == 0) {
                allowed = true;
                break;
            }
        }
        if (!allowed)
            return FILTER_TOO_SHORT;
    }

    if (words > SUGGEST_MAX_WORDS)
        return FILTER_TOO_LONG;
    if (len >= SUGGEST_MAX_CHARS)
        return FILTER_TOO_LONG;

    /* Multiple sentences */
    for (size_t i = 0; i + 1 < len; i++) {
        if ((suggestion[i] == '.' || suggestion[i] == '!' || suggestion[i] == '?') &&
            suggestion[i + 1] == ' ' && i + 2 < len && isupper((unsigned char)suggestion[i + 2]))
            return FILTER_MULTI_SENTENCE;
    }

    /* Formatting (newlines, markdown bold) */
    if (strchr(suggestion, '\n') || strstr(suggestion, "**"))
        return FILTER_FORMATTING;

    /* Evaluative */
    if (nc_contains_ci(suggestion, "thanks") || nc_contains_ci(suggestion, "thank you") ||
        nc_contains_ci(suggestion, "looks good") || nc_contains_ci(suggestion, "sounds good") ||
        nc_contains_ci(suggestion, "that works") || nc_contains_ci(suggestion, "that's all") ||
        nc_contains_ci(suggestion, "perfect") || nc_contains_ci(suggestion, "awesome") ||
        nc_contains_ci(suggestion, "excellent"))
        return FILTER_EVALUATIVE;

    /* AI voice patterns */
    if (nc_starts_with_ci(suggestion, "let me") || nc_starts_with_ci(suggestion, "i'll") ||
        nc_starts_with_ci(suggestion, "i've") || nc_starts_with_ci(suggestion, "i'm") ||
        nc_starts_with_ci(suggestion, "i can") || nc_starts_with_ci(suggestion, "here's") ||
        nc_starts_with_ci(suggestion, "here is") || nc_starts_with_ci(suggestion, "you can") ||
        nc_starts_with_ci(suggestion, "you should") || nc_starts_with_ci(suggestion, "sure,") ||
        nc_starts_with_ci(suggestion, "of course") || nc_starts_with_ci(suggestion, "certainly"))
        return FILTER_AI_VOICE;

    return FILTER_PASS;
}

const char *prompt_filter_name(prompt_filter_t f) {
    switch (f) {
    case FILTER_PASS:
        return "pass";
    case FILTER_EMPTY:
        return "empty";
    case FILTER_META:
        return "meta_text";
    case FILTER_ERROR:
        return "error_message";
    case FILTER_PREFIX:
        return "prefixed_label";
    case FILTER_TOO_SHORT:
        return "too_few_words";
    case FILTER_TOO_LONG:
        return "too_many_words";
    case FILTER_MULTI_SENTENCE:
        return "multiple_sentences";
    case FILTER_FORMATTING:
        return "has_formatting";
    case FILTER_EVALUATIVE:
        return "evaluative";
    case FILTER_AI_VOICE:
        return "ai_voice";
    }
    return "unknown";
}

static int count_assistant_turns_internal(const message_t *msgs, int n) {
    int cnt = 0;
    for (int i = 0; i < n; i++) {
        if (msgs[i].role == ROLE_ASSISTANT)
            cnt++;
    }
    return cnt;
}

char *prompt_suggest_generate(const void *conv_ptr, const char *base_url, const char *api_key,
                              const char *model, provider_type_t provider) {
    const conversation_t *conv = (const conversation_t *)conv_ptr;
    if (!conv || !s_ps_enabled)
        return NULL;
    if (count_assistant_turns_internal(conv->msgs, conv->len) < SUGGEST_MIN_TURNS)
        return NULL;

    int start = conv->len > SUGGEST_MSG_WINDOW ? conv->len - SUGGEST_MSG_WINDOW : 0;
    int window_n = conv->len - start;

    message_t suggest_msgs[SUGGEST_MSG_WINDOW + 1];
    int cnt = 0;
    for (int i = start; i < conv->len && cnt < SUGGEST_MSG_WINDOW; i++)
        suggest_msgs[cnt++] = conv->msgs[i];

    content_block_t prompt_block = {0};
    prompt_block.type = CONTENT_TEXT;
    prompt_block.text = (char *)SUGGESTION_PROMPT;

    message_t prompt_msg = {0};
    prompt_msg.role = ROLE_USER;
    prompt_msg.blocks = &prompt_block;
    prompt_msg.num_blocks = 1;
    prompt_msg.cap_blocks = 1;
    suggest_msgs[cnt++] = prompt_msg;

    strbuf_t response;
    strbuf_init(&response);

    int in_tok = 0, out_tok = 0;
    int rc = api_stream_simple(base_url, api_key, model, provider, NULL, suggest_msgs, cnt,
                               &response, &in_tok, &out_tok);
    (void)window_n;

    if (rc != 0 || response.len == 0) {
        strbuf_free(&response);
        return NULL;
    }

    /* Trim whitespace */
    char *raw = strbuf_detach(&response);
    char *s = raw;
    while (*s && isspace((unsigned char)*s))
        s++;
    size_t slen = strlen(s);
    while (slen > 0 && isspace((unsigned char)s[slen - 1]))
        slen--;
    s[slen] = '\0';

    /* Filter */
    prompt_filter_t f = prompt_suggest_filter(s);
    if (f != FILTER_PASS) {
        free(raw);
        return NULL;
    }

    char *result = nc_xstrdup(s);
    free(raw);

    free(s_current_suggestion);
    s_current_suggestion = nc_xstrdup(result);

    return result;
}

const char *prompt_suggest_prompt_text(void) {
    return SUGGESTION_PROMPT;
}

void prompt_suggest_free(void) {
    free(s_current_suggestion);
    s_current_suggestion = NULL;
    s_ps_enabled = false;
}
