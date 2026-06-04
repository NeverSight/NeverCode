/*
 * file_search.c — Fuzzy file path search tool.
 *
 * Searches workspace paths using the file index (if ready) or
 * a recursive directory walk with fuzzy matching.
 */

#include "nevercode.h"
#include "tools.h"

static int walk_dir(const char *root, const char *prefix, const char **out, int max, int *count,
                    int depth) {
    if (depth > FILE_INDEX_MAX_DEPTH || *count >= max)
        return 0;

    DIR *dp = opendir(root);
    if (!dp)
        return 0;

    struct dirent *e;
    char full[NC_PATH_MAX], rel[NC_PATH_MAX];

    while ((e = readdir(dp)) && *count < max) {
        if (e->d_name[0] == '.' &&
            (e->d_name[1] == 0 || (e->d_name[1] == '.' && e->d_name[2] == 0)))
            continue;
        if (nc_is_noise_dir(e->d_name))
            continue;

        snprintf(full, sizeof(full), "%s/%s", root, e->d_name);
        if (prefix[0])
            snprintf(rel, sizeof(rel), "%s/%s", prefix, e->d_name);
        else
            nc_strlcpy(rel, e->d_name, sizeof(rel));

        struct stat st;
        if (stat(full, &st) != 0)
            continue;

        out[*count] = nc_xstrdup(rel);
        (*count)++;

        if (S_ISDIR(st.st_mode))
            walk_dir(full, rel, out, max, count, depth + 1);
    }
    closedir(dp);
    return 0;
}

int file_search_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    if (!input || !output)
        return -1;

    const cJSON *q = cJSON_GetObjectItem(input, "query");
    if (!q || !cJSON_IsString(q)) {
        *output = nc_xstrdup("Missing required parameter: query");
        return 1;
    }

    int lim = FILE_SEARCH_DEFAULT_LIMIT;
    const cJSON *l = cJSON_GetObjectItem(input, "limit");
    if (l && cJSON_IsNumber(l)) {
        lim = l->valueint;
        if (lim < 1)
            lim = 1;
        if (lim > FILE_SEARCH_MAX_LIMIT)
            lim = FILE_SEARCH_MAX_LIMIT;
    }

    const char *root = sandbox_root();
    if (!root)
        root = ".";

    if (file_index_ready()) {
        fuzzy_result_t *r = calloc((size_t)lim, sizeof(fuzzy_result_t));
        if (!r) {
            *output = nc_xstrdup("Memory error");
            return 1;
        }

        int n = file_index_fuzzy(q->valuestring, r, lim);
        strbuf_t sb;
        strbuf_init(&sb);
        strbuf_appendf(&sb, "Found %d matches:\n\n", n);
        for (int i = 0; i < n; i++)
            strbuf_appendf(&sb, "%d. %s (score: %d)\n", i + 1, r[i].item, r[i].score);

        *output = strbuf_detach(&sb);
        free(r);
        return 0;
    }

    const char **paths = calloc(FILE_SEARCH_MAX_WALK, sizeof(char *));
    if (!paths) {
        *output = nc_xstrdup("Memory error");
        return 1;
    }

    int pc = 0;
    walk_dir(root, "", paths, FILE_SEARCH_MAX_WALK, &pc, 0);

    int cap = NC_MIN(lim, pc);
    fuzzy_result_t *r = calloc((size_t)cap, sizeof(fuzzy_result_t));
    if (!r) {
        for (int i = 0; i < pc; i++)
            free((void *)paths[i]);
        free(paths);
        *output = nc_xstrdup("Memory error");
        return 1;
    }

    int n = fuzzy_filter(paths, pc, q->valuestring, r, lim);

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Found %d matches (scanned %d):\n\n", n, pc);
    for (int i = 0; i < n; i++)
        strbuf_appendf(&sb, "%d. %s (score: %d)\n", i + 1, r[i].item, r[i].score);

    *output = strbuf_detach(&sb);
    free(r);
    for (int i = 0; i < pc; i++)
        free((void *)paths[i]);
    free(paths);
    return 0;
}

static const char *FILE_SEARCH_SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"query\":{\"type\":\"string\",\"description\":\"Fuzzy file path substring\"},"
    "\"limit\":{\"type\":\"integer\",\"description\":\"Max results (default 20)\"}"
    "},"
    "\"required\":[\"query\"]"
    "}";

static tool_def_t file_search_tool;

void tool_file_search_register(void) {
    file_search_tool.name = "FileSearch";
    file_search_tool.description =
        "Fuzzy-search file paths under the workspace (or file index when "
        "ready).";
    file_search_tool.input_schema = cJSON_Parse(FILE_SEARCH_SCHEMA);
    file_search_tool.execute = file_search_exec;
    file_search_tool.read_only = true;
    file_search_tool.concurrency_safe = true;
    file_search_tool.destructive = false;
    tools_register(&file_search_tool);
}
