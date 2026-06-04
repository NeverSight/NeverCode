/*
 * knowledge_cutoff.c — Model knowledge cutoff date lookup.
 *
 * Returns the training data cutoff date for known model families.
 * Used in the system prompt environment section so the model
 * knows its own temporal limitations.
 */

#include "nevercode.h"

typedef struct {
    const char *pattern;
    const char *cutoff;
} cutoff_entry_t;

static const cutoff_entry_t g_cutoffs[] = {
    {"gpt-5.4", KC_GPT54_CUTOFF},
    {"gpt-5", KC_GPT5_CUTOFF},
    {"gpt-4.1", KC_GPT41_CUTOFF},
    {"gpt-4o", KC_GPT4O_CUTOFF},
    {"o3", KC_O3_CUTOFF},
    {"o4-mini", KC_O4MINI_CUTOFF},
    {"claude-opus-4-6", KC_OPUS46_CUTOFF},
    {"claude-sonnet-4-6", KC_SONNET46_CUTOFF},
    {"claude-opus-4-5", KC_OPUS45_CUTOFF},
    {"claude-sonnet-4-5", KC_SONNET45_CUTOFF},
    {"claude-3-5-sonnet", KC_SONNET35_CUTOFF},
    {"claude-3-5-haiku", KC_HAIKU35_CUTOFF},
    {"gemini-2.5", KC_GEMINI25_CUTOFF},
    {"gemini-2.0", KC_GEMINI20_CUTOFF},
    {"deepseek-r1", KC_DSR1_CUTOFF},
    {"deepseek-v3", KC_DSV3_CUTOFF},
    {"qwen3", KC_QWEN3_CUTOFF},
    {"llama-4", KC_LLAMA4_CUTOFF},
    {"llama-3", KC_LLAMA3_CUTOFF},
};

#define KC_TABLE_SIZE NC_ARRAY_LEN(g_cutoffs)

const char *kc_lookup(const char *model_id) {
    if (!model_id || !*model_id)
        return NULL;
    for (int i = 0; i < KC_TABLE_SIZE; i++) {
        if (strstr(model_id, g_cutoffs[i].pattern))
            return g_cutoffs[i].cutoff;
    }
    return NULL;
}

char *kc_build_prompt(const char *model_id) {
    const char *cutoff = kc_lookup(model_id);
    if (!cutoff)
        return NULL;
    char buf[NC_TAG_MAX];
    snprintf(buf, sizeof(buf), "Assistant knowledge cutoff is %s.", cutoff);
    return nc_xstrdup(buf);
}

int kc_table_size(void) {
    return KC_TABLE_SIZE;
}
