#include "nevercode.h"
#include "permission.h"

static const security_preset_t builtin_presets[] = {
    {"read-only", "Read Only",
     "Agent can read files in the workspace. Approval required for edits, "
     "commands, and network access.",
     APPROVAL_READONLY},
    {"standard", "Standard",
     "Agent can read/edit files and run commands in the workspace. "
     "Approval required for network access and files outside the project.",
     APPROVAL_DEFAULT},
    {"full-access", "Full Access",
     "Agent can edit files outside the workspace and access the network "
     "without approval. Use with caution.",
     APPROVAL_FULL}};

#define NUM_BUILTIN_PRESETS NC_ARRAY_LEN(builtin_presets)

int security_presets_list(const security_preset_t **out, int max) {
    int n = NUM_BUILTIN_PRESETS;
    if (n > max)
        n = max;
    for (int i = 0; i < n; i++)
        out[i] = &builtin_presets[i];
    return n;
}

const security_preset_t *security_preset_find(const char *id) {
    if (!id)
        return NULL;
    for (int i = 0; i < NUM_BUILTIN_PRESETS; i++) {
        if (strcmp(builtin_presets[i].id, id) == 0)
            return &builtin_presets[i];
    }
    return NULL;
}

int security_preset_count(void) {
    return NUM_BUILTIN_PRESETS;
}

char *security_presets_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_str(&sb, "Security Presets:\n\n");

    for (int i = 0; i < NUM_BUILTIN_PRESETS; i++) {
        const security_preset_t *p = &builtin_presets[i];
        char line[NC_MSG_MAX];
        snprintf(line, sizeof(line), "  [%s] %s\n    %s\n\n", p->id, p->label, p->description);
        strbuf_append_str(&sb, line);
    }

    return strbuf_detach(&sb);
}
