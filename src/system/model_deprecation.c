/*
 * model_deprecation.c — Deprecated model warning system.
 *
 * Matches a model ID against a table of deprecated models and
 * returns the human-readable name and retirement date. Used to
 * warn users before an API call that their model will stop working.
 */

#include "nevercode.h"

typedef struct {
    const char *pattern;
    const char *display_name;
    const char *retirement_date;
} depr_entry_t;

static const depr_entry_t g_deprecated[] = {
    {"claude-3-opus", "Claude 3 Opus", DEPR_CLAUDE3_OPUS_DATE},
    {"claude-3-7-sonnet", "Claude 3.7 Sonnet", DEPR_CLAUDE37_SONNET_DATE},
    {"claude-3-5-haiku", "Claude 3.5 Haiku", DEPR_CLAUDE35_HAIKU_DATE},
    {"gpt-4-turbo", "GPT-4 Turbo", DEPR_GPT4_TURBO_DATE},
    {"gpt-4o-2024", "GPT-4o (2024)", DEPR_GPT4O_2024_DATE},
};

#define DEPR_TABLE_SIZE NC_ARRAY_LEN(g_deprecated)

bool depr_is_deprecated(const char *model_id) {
    if (!model_id || !*model_id)
        return false;
    for (int i = 0; i < DEPR_TABLE_SIZE; i++) {
        if (strcasestr(model_id, g_deprecated[i].pattern))
            return true;
    }
    return false;
}

const char *depr_display_name(const char *model_id) {
    if (!model_id)
        return NULL;
    for (int i = 0; i < DEPR_TABLE_SIZE; i++) {
        if (strcasestr(model_id, g_deprecated[i].pattern))
            return g_deprecated[i].display_name;
    }
    return NULL;
}

const char *depr_retirement_date(const char *model_id) {
    if (!model_id)
        return NULL;
    for (int i = 0; i < DEPR_TABLE_SIZE; i++) {
        if (strcasestr(model_id, g_deprecated[i].pattern))
            return g_deprecated[i].retirement_date;
    }
    return NULL;
}

char *depr_warning_message(const char *model_id) {
    if (!model_id)
        return NULL;
    const char *name = depr_display_name(model_id);
    const char *date = depr_retirement_date(model_id);
    if (!name || !date)
        return NULL;
    char buf[NC_LABEL_MAX];
    snprintf(buf, sizeof(buf),
             "%s is deprecated and will be retired on %s. Consider switching models.", name, date);
    return nc_xstrdup(buf);
}

int depr_table_size(void) {
    return DEPR_TABLE_SIZE;
}
