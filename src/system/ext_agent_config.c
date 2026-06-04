/*
 * ext_agent_config.c — External agent config detection and migration
 *
 * Detects configuration from other AI coding tools (by looking for
 * their config directories) and offers to import settings, skills,
 * project docs, and MCP server configurations.
 */

#include "nevercode.h"

/* EAC_MAX_ITEMS, EAC_DESC_MAX in defaults.h */

typedef enum { EAC_CONFIG, EAC_SKILLS, EAC_PROJECT_DOC, EAC_MCP_SERVER } eac_item_type_t;

typedef struct {
    eac_item_type_t type;
    char description[EAC_DESC_MAX];
    char source_path[NC_PATH_MAX];
    char dest_path[NC_PATH_MAX];
    bool imported;
} eac_item_t;

typedef struct {
    eac_item_t items[EAC_MAX_ITEMS];
    int count;
} eac_result_t;

typedef struct {
    const char *name;
    const char *home_subdir;
    const char *project_subdir;
    const char *config_file;
    const char *doc_files[4];
} eac_tool_def_t;

static const eac_tool_def_t g_tools[] = {
    {"cursor", ".cursor", ".cursor", "config.json", {"rules/", NULL}},
    {"aider", ".aider", ".aider", "aider.conf.yml", {"CONVENTIONS.md", NULL}},
    {"cline", ".cline", ".cline", "config.json", {NULL}},
    {"continue", ".continue", ".continue", "config.json", {".continuerules", NULL}},
    {NULL, NULL, NULL, NULL, {NULL}}};

static void try_detect(const eac_tool_def_t *tool, const char *base, const char *subdir,
                       eac_result_t *result) {
    char dir[NC_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/%s", base, subdir);

    if (!nc_path_exists(dir))
        return;

    /* config file */
    if (tool->config_file && result->count < EAC_MAX_ITEMS) {
        char cfg[NC_PATH_MAX];
        snprintf(cfg, sizeof(cfg), "%s/%s", dir, tool->config_file);
        if (nc_path_exists(cfg)) {
            eac_item_t *it = &result->items[result->count];
            it->type = EAC_CONFIG;
            snprintf(it->description, sizeof(it->description), "%s config: %s", tool->name,
                     tool->config_file);
            nc_strlcpy(it->source_path, cfg, sizeof(it->source_path));
            it->imported = false;
            result->count++;
        }
    }

    /* doc files */
    for (int i = 0; tool->doc_files[i] && result->count < EAC_MAX_ITEMS; i++) {
        char doc[NC_PATH_MAX];
        snprintf(doc, sizeof(doc), "%s/%s", dir, tool->doc_files[i]);
        if (nc_path_exists(doc)) {
            eac_item_t *it = &result->items[result->count];
            it->type = EAC_PROJECT_DOC;
            snprintf(it->description, sizeof(it->description), "%s doc: %s", tool->name,
                     tool->doc_files[i]);
            nc_strlcpy(it->source_path, doc, sizeof(it->source_path));
            it->imported = false;
            result->count++;
        }
    }
}

int eac_detect(eac_result_t *result, const char *project_root, bool include_home) {
    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));

    const char *home = nc_home_dir();

    for (int t = 0; g_tools[t].name; t++) {
        if (include_home && home)
            try_detect(&g_tools[t], home, g_tools[t].home_subdir, result);
        if (project_root)
            try_detect(&g_tools[t], project_root, g_tools[t].project_subdir, result);
    }
    return result->count;
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "r");
    if (!in)
        return -1;
    FILE *out = fopen(dst, "w");
    if (!out) {
        fclose(in);
        return -1;
    }
    char buf[NC_IO_BUF];
    size_t n;
    int err = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            err = -1;
            break;
        }
    }
    if (!err && fflush(out))
        err = -1;
    fclose(in);
    fclose(out);
    return err;
}

int eac_import(eac_result_t *result, const char *dest_base) {
    if (!result || !dest_base)
        return -1;

    char dest_dir[NC_PATH_MAX];
    snprintf(dest_dir, sizeof(dest_dir), "%s%s", dest_base, CONFIG_DIR_PRIMARY);
    struct stat st;
    if (stat(dest_dir, &st) != 0)
        mkdir(dest_dir, NC_DIR_MODE);

    int imported = 0;
    for (int i = 0; i < result->count; i++) {
        eac_item_t *it = &result->items[i];
        if (it->imported)
            continue;

        const char *basename = strrchr(it->source_path, '/');
        basename = basename ? basename + 1 : it->source_path;

        snprintf(it->dest_path, sizeof(it->dest_path), "%s/imported_%s", dest_dir, basename);

        if (!nc_path_exists(it->dest_path) && copy_file(it->source_path, it->dest_path) == 0) {
            it->imported = true;
            imported++;
        }
    }
    return imported;
}

char *eac_format_detected(const eac_result_t *result) {
    if (!result || result->count == 0)
        return nc_xstrdup("No external agent configurations detected.\n");

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "External configs detected (%d):\n", result->count);
    for (int i = 0; i < result->count; i++) {
        const eac_item_t *it = &result->items[i];
        const char *type_names[] = {"config", "skills", "project_doc", "mcp_server"};
        strbuf_appendf(&sb, "  [%d] [%s] %s\n    %s%s\n", i + 1, type_names[it->type],
                       it->description, it->source_path, it->imported ? " (imported)" : "");
    }
    return strbuf_detach(&sb);
}

int eac_detected_count(const eac_result_t *result) {
    return result ? result->count : 0;
}
