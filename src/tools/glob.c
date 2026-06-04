#include "tools.h"
#include "nevercode.h"
#include <fnmatch.h>

static const char *SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"pattern\":{\"type\":\"string\",\"description\":\"Glob pattern (e.g. **/*.c)\"},"
    "\"path\":{\"type\":\"string\",\"description\":\"Base directory (default: cwd)\"}"
    "},"
    "\"required\":[\"pattern\"]"
    "}";

typedef struct {
    strbuf_t *sb;
    const char *pattern;
    int count;
} glob_ctx_t;

static void glob_walk(const char *dir, const char *rel_base, const char *pattern, strbuf_t *sb,
                      int *count) {
    if (*count >= GLOB_MAX_RESULTS)
        return;

    DIR *d = opendir(dir);
    if (!d)
        return;

    struct dirent *ent;
    while ((ent = readdir(d)) && *count < GLOB_MAX_RESULTS) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (ent->d_name[0] == '.' && strcmp(ent->d_name, ".github") != 0 &&
            strcmp(ent->d_name, CONFIG_DOTDIR_PRIMARY) != 0 &&
            strcmp(ent->d_name, CONFIG_DOTDIR_SECONDARY) != 0 &&
            strcmp(ent->d_name, CONFIG_DOTDIR_COMPAT) != 0 && strcmp(ent->d_name, ".vscode") != 0 &&
            strcmp(ent->d_name, ".env") != 0 && strcmp(ent->d_name, ".gitignore") != 0 &&
            strcmp(ent->d_name, ".eslintrc") != 0)
            continue;

        char fullpath[NC_PATH_MAX], relpath[NC_PATH_MAX];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, ent->d_name);
        if (rel_base[0])
            snprintf(relpath, sizeof(relpath), "%s/%s", rel_base, ent->d_name);
        else
            nc_strlcpy(relpath, ent->d_name, sizeof(relpath));

        struct stat st;
        if (lstat(fullpath, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            if (nc_is_noise_dir(ent->d_name))
                continue;
            glob_walk(fullpath, relpath, pattern, sb, count);
        } else if (S_ISREG(st.st_mode)) {
            bool match = false;
            if (nc_starts_with(pattern, "**/")) {
                match = fnmatch(pattern + 3, ent->d_name, 0) == 0;
                if (!match)
                    match = fnmatch(pattern, relpath, FNM_PATHNAME) == 0;
            } else {
                match = fnmatch(pattern, relpath, FNM_PATHNAME) == 0;
                if (!match)
                    match = fnmatch(pattern, ent->d_name, 0) == 0;
            }
            if (match) {
                strbuf_append_str(sb, fullpath);
                strbuf_append_str(sb, "\n");
                (*count)++;
            }
        }
    }
    closedir(d);
}

static int glob_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *pattern = nc_cjson_str((cJSON *)input, "pattern", NULL);
    const char *path = nc_cjson_str((cJSON *)input, "path", NULL);
    if (!pattern) {
        *output = nc_xstrdup("error: missing 'pattern'");
        return 1;
    }

    char base[NC_PATH_MAX];
    if (path && path[0])
        nc_strlcpy(base, path, sizeof(base));
    else
        getcwd(base, sizeof(base));

    strbuf_t sb;
    strbuf_init(&sb);
    int count = 0;
    glob_walk(base, "", pattern, &sb, &count);

    if (count == 0) {
        strbuf_append_str(&sb, "(no matches)");
    } else {
        char footer[NC_FMT_MAX];
        snprintf(footer, sizeof(footer), "\n(%d files found)", count);
        strbuf_append_str(&sb, footer);
    }

    *output = strbuf_detach(&sb);
    return 0;
}

static tool_def_t glob_tool;

void tool_glob_register(void) {
    glob_tool.name = "Glob";
    glob_tool.description = "Find files matching a glob pattern. Searches recursively. "
                            "Patterns like **/*.c find all .c files.";
    glob_tool.input_schema = cJSON_Parse(SCHEMA);
    glob_tool.execute = glob_exec;
    glob_tool.read_only = true;
    glob_tool.concurrency_safe = true;
    glob_tool.destructive = false;
    tools_register(&glob_tool);
}
