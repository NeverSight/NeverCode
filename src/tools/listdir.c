#include "tools.h"
#include "nevercode.h"

static const char *SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"Directory path to list\"},"
    "\"recursive\":{\"type\":\"boolean\",\"description\":\"List recursively (default false)\"},"
    "\"max_depth\":{\"type\":\"integer\",\"description\":\"Max depth for recursive listing "
    "(default 3)\"}"
    "},"
    "\"required\":[\"path\"]"
    "}";

/* nc_is_noise_dir() in nevercode.h */

static void format_perms(mode_t m, char *out) {
    out[0] = (m & S_IRUSR) ? 'r' : '-';
    out[1] = (m & S_IWUSR) ? 'w' : '-';
    out[2] = (m & S_ISUID) ? ((m & S_IXUSR) ? 's' : 'S') : ((m & S_IXUSR) ? 'x' : '-');
    out[3] = (m & S_IRGRP) ? 'r' : '-';
    out[4] = (m & S_IWGRP) ? 'w' : '-';
    out[5] = (m & S_ISGID) ? ((m & S_IXGRP) ? 's' : 'S') : ((m & S_IXGRP) ? 'x' : '-');
    out[6] = (m & S_IROTH) ? 'r' : '-';
    out[7] = (m & S_IWOTH) ? 'w' : '-';
    out[8] = (m & S_ISVTX) ? ((m & S_IXOTH) ? 't' : 'T') : ((m & S_IXOTH) ? 'x' : '-');
    out[9] = '\0';
}

static void list_dir(const char *dir, const char *rel, int depth, int max_depth, bool recursive,
                     strbuf_t *sb, int *count) {
    if (*count >= LISTDIR_MAX_ENTRIES)
        return;
    DIR *d = opendir(dir);
    if (!d)
        return;

    struct dirent *ent;
    while ((ent = readdir(d)) && *count < LISTDIR_MAX_ENTRIES) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char fullpath[NC_PATH_MAX], relpath[NC_PATH_MAX];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, ent->d_name);
        if (rel[0])
            snprintf(relpath, sizeof(relpath), "%s/%s", rel, ent->d_name);
        else
            nc_strlcpy(relpath, ent->d_name, sizeof(relpath));

        struct stat st;
        if (lstat(fullpath, &st) != 0)
            continue;

        char perms[10];
        format_perms(st.st_mode, perms);

        for (int i = 0; i < depth; i++)
            strbuf_append_str(sb, "  ");

        if (S_ISDIR(st.st_mode)) {
            char line[NC_PATH_MAX + NC_TAG_MAX];
            snprintf(line, sizeof(line), "%s d %s/", perms, relpath);
            strbuf_append_str(sb, line);
            strbuf_append_str(sb, "\n");
            (*count)++;

            if (recursive && depth < max_depth && !nc_is_noise_dir(ent->d_name))
                list_dir(fullpath, relpath, depth + 1, max_depth, recursive, sb, count);
        } else if (S_ISLNK(st.st_mode)) {
            char target[NC_LINE_MAX] = {0};
            readlink(fullpath, target, sizeof(target) - 1);
            char line[NC_PATH_MAX * 2 + NC_TAG_MAX];
            snprintf(line, sizeof(line), "%s l %s -> %s", perms, relpath, target);
            strbuf_append_str(sb, line);
            strbuf_append_str(sb, "\n");
            (*count)++;
        } else {
            char meta[NC_TAG_MAX] = "";
            if (st.st_size > 1024 * 1024)
                snprintf(meta, sizeof(meta), " %.1fM", st.st_size / (1024.0 * 1024.0));
            else if (st.st_size > 1024)
                snprintf(meta, sizeof(meta), " %.1fK", st.st_size / 1024.0);
            else
                snprintf(meta, sizeof(meta), " %lldB", (long long)st.st_size);

            char line[NC_PATH_MAX + NC_TAG_MAX];
            if (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
                snprintf(line, sizeof(line), "%s * %s%s", perms, relpath, meta);
            else
                snprintf(line, sizeof(line), "%s   %s%s", perms, relpath, meta);
            strbuf_append_str(sb, line);
            strbuf_append_str(sb, "\n");
            (*count)++;
        }
    }
    closedir(d);
}

static int listdir_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *path = nc_cjson_str((cJSON *)input, "path", NULL);
    if (!path) {
        *output = nc_xstrdup("error: missing 'path'");
        return 1;
    }

    cJSON *rec = cJSON_GetObjectItem((cJSON *)input, "recursive");
    bool recursive = rec && cJSON_IsTrue(rec);

    int max_depth = LISTDIR_DEFAULT_DEPTH;
    cJSON *md = cJSON_GetObjectItem((cJSON *)input, "max_depth");
    if (md && cJSON_IsNumber(md))
        max_depth = (int)md->valuedouble;

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        char msg[NC_MSG_MAX];
        snprintf(msg, sizeof(msg), "error: not a directory: %s", path);
        *output = nc_xstrdup(msg);
        return 1;
    }

    strbuf_t sb;
    strbuf_init(&sb);
    int count = 0;
    list_dir(path, "", 0, max_depth, recursive, &sb, &count);

    if (count == 0) {
        strbuf_append_str(&sb, "(empty directory)");
    } else {
        char footer[NC_FMT_MAX];
        snprintf(footer, sizeof(footer), "\n(%d entries)", count);
        strbuf_append_str(&sb, footer);
    }

    if (count >= LISTDIR_MAX_ENTRIES)
        strbuf_append_str(&sb, " [truncated]");

    *output = strbuf_detach(&sb);
    return 0;
}

static tool_def_t listdir_tool;

void tool_listdir_register(void) {
    listdir_tool.name = "ListDir";
    listdir_tool.description = "List directory contents with permissions (rwx), sizes, and types. "
                               "Shows symlinks, executables (*), setuid/setgid. "
                               "Use recursive=true for tree view.";
    listdir_tool.input_schema = cJSON_Parse(SCHEMA);
    listdir_tool.execute = listdir_exec;
    listdir_tool.read_only = true;
    listdir_tool.concurrency_safe = true;
    listdir_tool.destructive = false;
    tools_register(&listdir_tool);
}
