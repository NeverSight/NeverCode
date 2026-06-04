#include "nevercode.h"

static const char *HIERARCHICAL_AGENTS_MSG =
    "Files called AGENTS.md commonly appear in many places inside a project - "
    "at the project root, in subdirectories, or in any other directory; their "
    "location is not limited to version-controlled folders.\n\n"
    "Their purpose is to pass along human guidance to you, the agent. Such "
    "guidance can include coding standards, explanations of the project layout, "
    "steps for building or testing, and even wording that must accompany a "
    "GitHub pull-request description produced by the agent; all of it is to "
    "be followed.\n\n"
    "Each AGENTS.md governs the entire directory that contains it and every "
    "child directory beneath that point. Whenever you change a file, you have "
    "to comply with every AGENTS.md whose scope covers that file.\n\n"
    "When two AGENTS.md files disagree, the one located deeper in the directory "
    "structure overrides the higher-level file, while instructions given directly "
    "in the prompt by the system, developer, or user outrank any AGENTS.md content.\n\n"
    "The same hierarchy applies to every loaded project instruction path (root and "
    "dot-directories).\n";

/* Highest-precedence existing file within one filename family (array order). */
static char *try_project_doc_family(const char *dir, const char *const *suf, int n) {
    for (int i = 0; i < n; i++) {
        char path[NC_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, suf[i]);
        char *content = strbuf_read_file(path, NULL);
        if (content)
            return content;
    }
    return NULL;
}

/* Merge primary + extra filename-family instructions when both exist. */
static char *try_project_docs(const char *dir) {
    char *a = try_project_doc_family(dir, nc_project_doc_agents_paths, nc_project_doc_agents_n);
    char *b = try_project_doc_family(dir, nc_project_doc_extra_paths, nc_project_doc_extra_n);
    if (!a && !b)
        return NULL;
    if (!a)
        return b;
    if (!b)
        return a;
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, a);
    strbuf_append_str(&sb, "\n\n");
    strbuf_append_str(&sb, b);
    free(a);
    free(b);
    return strbuf_detach(&sb);
}

char *agents_md_load(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    int found_count_total = 0;
    (void)found_count_total;

    /* 1. User-level: first match wins to avoid duplicates across
     *    config directories. Priority follows directory order. */
    const char *home = nc_home_dir();
    if (home) {
        static const char *user_dirs[] = {CONFIG_DIR_PRIMARY, CONFIG_DIR_SECONDARY,
                                          CONFIG_DIR_COMPAT};
        int user_found = 0;
        for (int d = 0; d < 3 && !user_found; d++) {
            for (int f = 0; f < nc_project_doc_leaf_n && !user_found; f++) {
                char path[NC_PATH_MAX];
                snprintf(path, sizeof(path), "%s%s/%s", home, user_dirs[d],
                         nc_project_doc_leaf_names[f]);
                char *c = strbuf_read_file(path, NULL);
                if (c) {
                    strbuf_append_str(&sb, "# User-level ");
                    strbuf_append_str(&sb, nc_project_doc_leaf_names[f]);
                    strbuf_append_str(&sb, " (");
                    strbuf_append_str(&sb, home);
                    strbuf_append_str(&sb, user_dirs[d]);
                    strbuf_append_str(&sb, ")\n\n");
                    strbuf_append_str(&sb, c);
                    strbuf_append_str(&sb, "\n\n");
                    free(c);
                    found_count_total++;
                    user_found = 1;
                }
            }
        }
    }

    /* 2. Project-level: walk up from cwd, collect ALL levels (child -> root) */
    char cwd[NC_PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        const char *project_root = project_root_detect(cwd);
        bool enforce_project_boundary =
            project_root && project_root_primary_marker(project_root) != NULL;
        char *found_paths[64];
        char *found_contents[64];
        int found_count = 0;

        char dir[NC_PATH_MAX];
        nc_strlcpy(dir, cwd, sizeof(dir));
        while (found_count < 64) {
            char *c = try_project_docs(dir);
            if (c) {
                found_paths[found_count] = nc_xstrdup(dir);
                found_contents[found_count] = c;
                found_count++;
            }
            if (enforce_project_boundary && strcmp(dir, project_root) == 0)
                break;
            char *slash = strrchr(dir, '/');
            if (!slash || slash == dir)
                break;
            *slash = '\0';
        }

        for (int i = found_count - 1; i >= 0; i--) {
            if (i == found_count - 1)
                strbuf_append_str(&sb, "# Project AGENTS.md (");
            else
                strbuf_append_str(&sb, "# AGENTS.md (");
            strbuf_append_str(&sb, found_paths[i]);
            strbuf_append_str(&sb, ")\n\n");
            strbuf_append_str(&sb, found_contents[i]);
            strbuf_append_str(&sb, "\n\n");
            free(found_paths[i]);
            free(found_contents[i]);
        }

        found_count_total += found_count;

        if (found_count > 1) {
            strbuf_append_str(&sb, "# Hierarchical Project Documentation\n\n");
            strbuf_append_str(&sb, HIERARCHICAL_AGENTS_MSG);
            strbuf_append_str(&sb, "\n");
        }
    }

    /* 3. Load settings.json from config home */
    {
        char settings_path[NC_PATH_MAX];
        config_find_path(settings_path, sizeof(settings_path), NC_CFG_SETTINGS);
        char *settings = strbuf_read_file(settings_path, NULL);
        if (settings) {
            strbuf_append_str(&sb, "# User Settings\n\n```json\n");
            strbuf_append_str(&sb, settings);
            strbuf_append_str(&sb, "\n```\n\n");
            free(settings);
        }
    }

    if (sb.len == 0) {
        strbuf_free(&sb);
        return NULL;
    }
    return strbuf_detach(&sb);
}
