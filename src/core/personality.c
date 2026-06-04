/* personality.c */
#include "nevercode.h"
#include "defaults.h"

typedef struct {
    const char *name;
    const char *prompt;
} preset_t;

static const preset_t PRESETS[] = {
    {"default", NULL},
    {"concise", "Be extremely concise."},
    {"verbose", "Be thorough and detailed."},
    {"formal", "Use formal, professional language."},
    {"casual", "Be friendly and casual."},
    {"mentor", "Act as a patient teacher."},
    {"speed", "Prioritize speed. Minimal output."},
    {"ctf", "Operate in CTF/security research mode. "
            "Focus on exploitation, analysis, reverse engineering."},
};

#define NUM_PRESETS NC_ARRAY_LEN(PRESETS)

static int g_active_id;

void personality_set(int id) {
    if (id >= 0 && id < NUM_PRESETS)
        g_active_id = id;
}

int personality_get(void) {
    return g_active_id;
}

int personality_parse(const char *name) {
    if (!name)
        return 0;

    while (*name == ' ' || *name == '\t' || *name == '\n' || *name == '\r')
        name++;
    if (!*name)
        return 0;

    const char *end = name + strlen(name);
    while (end > name && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r'))
        end--;

    size_t len = (size_t)(end - name);
    if (len == 0)
        return 0;
    if (len >= NC_LABEL_MAX)
        return -1;

    char buf[NC_LABEL_MAX];
    memcpy(buf, name, len);
    buf[len] = '\0';

    for (int i = 0; i < NUM_PRESETS; i++)
        if (strcasecmp(buf, PRESETS[i].name) == 0)
            return i;
    return -1;
}

const char *personality_name(int id) {
    return (id >= 0 && id < NUM_PRESETS) ? PRESETS[id].name : "default";
}

const char *personality_prompt(void) {
    if (g_active_id > 0 && g_active_id < NUM_PRESETS)
        return PRESETS[g_active_id].prompt;
    return NULL;
}

int personality_count(void) {
    return NUM_PRESETS;
}

char *personality_list(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    for (int i = 0; i < NUM_PRESETS; i++) {
        const char *marker = (i == g_active_id) ? " (active)" : "";
        strbuf_appendf(&sb, "  %s%s\n", PRESETS[i].name, marker);
    }
    return strbuf_detach(&sb);
}
