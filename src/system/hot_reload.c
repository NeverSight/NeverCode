/*
 * hot_reload.c — Config hot-reload via file watcher callbacks
 *
 * When the file watcher detects changes to project docs, hooks.json,
 * memory.json, or skills directories, sets a flag that the main REPL loop
 * checks between turns to reload affected subsystems without restart.
 */

#include "nevercode.h"
#include "repl.h"
#include "conversation.h"

static volatile int g_reload_flags = 0;
static pthread_mutex_t g_reload_mutex = PTHREAD_MUTEX_INITIALIZER;

void hot_reload_flag_set(hot_reload_kind_t kind) {
    pthread_mutex_lock(&g_reload_mutex);
    g_reload_flags |= (1 << kind);
    pthread_mutex_unlock(&g_reload_mutex);
}

int hot_reload_flags_consume(void) {
    pthread_mutex_lock(&g_reload_mutex);
    int flags = g_reload_flags;
    g_reload_flags = 0;
    pthread_mutex_unlock(&g_reload_mutex);
    return flags;
}

bool hot_reload_pending(void) {
    return g_reload_flags != 0;
}

const char *hot_reload_kind_name(hot_reload_kind_t kind) {
    switch (kind) {
    case HOT_RELOAD_AGENTS_MD:
        return "project-docs";
    case HOT_RELOAD_HOOKS:
        return NC_CFG_HOOKS;
    case HOT_RELOAD_MEMORY:
        return NC_CFG_MEMORY;
    case HOT_RELOAD_SKILLS:
        return NC_CFG_SKILLS_DIR;
    case HOT_RELOAD_RULES:
        return "exec rules";
    case HOT_RELOAD_MCP:
        return NC_CFG_MCP;
    case HOT_RELOAD_SETTINGS:
        return NC_CFG_SETTINGS;
    }
    return "unknown";
}

static hot_reload_kind_t classify_path(const char *path) {
    if (!path)
        return HOT_RELOAD_AGENTS_MD;

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    if (nc_project_doc_is_filename(base))
        return HOT_RELOAD_AGENTS_MD;
    if (strcmp(base, NC_CFG_HOOKS) == 0)
        return HOT_RELOAD_HOOKS;
    if (strcmp(base, NC_CFG_MEMORY) == 0)
        return HOT_RELOAD_MEMORY;
    if (strcmp(base, NC_CFG_MCP) == 0)
        return HOT_RELOAD_MCP;
    if (strcmp(base, NC_CFG_SETTINGS) == 0)
        return HOT_RELOAD_SETTINGS;

    if (strstr(path, "/rules") || strstr(path, ".rules"))
        return HOT_RELOAD_RULES;
    if (strstr(path, "/skills"))
        return HOT_RELOAD_SKILLS;

    return HOT_RELOAD_AGENTS_MD;
}

void hot_reload_watcher_cb(const char *path, file_watch_type_t type, void *ctx) {
    (void)type;
    (void)ctx;
    hot_reload_kind_t kind = classify_path(path);
    hot_reload_flag_set(kind);
}

int hot_reload_process(int flags, hook_ctx_t *hooks, memory_ctx_t *memory, void *conv_ptr) {
    conversation_t *conv = conv_ptr;
    int reloaded = 0;

    if (flags & (1 << HOT_RELOAD_AGENTS_MD)) {
        if (conv && !conv->system_prompt) {
            /* system_prompt_override active; skip */
        } else if (conv) {
            strbuf_t sp;
            strbuf_init(&sp);
            char *base = build_system_prompt();
            strbuf_append_str(&sp, base);
            free(base);
            char *amd = agents_md_load();
            if (amd) {
                strbuf_append_str(&sp, "\n\n---\n\n");
                strbuf_append_str(&sp, amd);
                free(amd);
            }
            if (memory) {
                char *mp = memory_format_for_prompt(memory);
                if (mp) {
                    strbuf_append_str(&sp, "\n\n---\n\n");
                    strbuf_append_str(&sp, mp);
                    free(mp);
                }
            }
            if (g_cfg_ptr)
                prompt_append_model_extras(&sp, g_cfg_ptr);
            free(conv->system_prompt);
            conv->system_prompt = strbuf_detach(&sp);
            reloaded++;
        }
    }

    if (flags & (1 << HOT_RELOAD_HOOKS)) {
        if (hooks) {
            hooks_free(hooks);
            hooks_init(hooks);
            char path[NC_PATH_MAX];
            config_find_path(path, sizeof(path), NC_CFG_HOOKS);
            hooks_load(hooks, path);
            reloaded++;
        }
    }

    if (flags & (1 << HOT_RELOAD_MEMORY)) {
        if (memory) {
            memory_free(memory);
            memory_init(memory);
            char path[NC_PATH_MAX];
            config_find_path(path, sizeof(path), NC_CFG_MEMORY);
            memory_load(memory, path);
            reloaded++;
        }
    }

    if (flags & (1 << HOT_RELOAD_SKILLS)) {
        custom_skills_free();
        char path[NC_PATH_MAX];
        config_find_path(path, sizeof(path), "skills");
        custom_skills_load(path);
        reloaded++;
    }

    if (flags & (1 << HOT_RELOAD_RULES)) {
        exec_rules_ctx_t *er = exec_rules_global();
        exec_rules_free(er);
        exec_rules_init(er);
        exec_rules_load_from_config(er);
        reloaded++;
    }

    return reloaded;
}
