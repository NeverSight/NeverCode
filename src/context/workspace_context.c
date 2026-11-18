/*
 * workspace_context.c — Startup workspace context builder.
 *
 * Constructs a bounded summary of the current workspace: directory tree,
 * recent session asks, git layout, and environment notes. Injected once
 * into the system prompt so the model understands the project landscape
 * without wasting a tool call.
 */

#include "nevercode.h"
#include "git_ops.h"

static const char *NOISY_DIRS[] = {
    ".git", ".next",        ".pytest_cache", ".ruff_cache", "__pycache__", "build",
    "dist", "node_modules", "out",           "target",      ".cache",      ".venv",
    "venv", ".idea",        ".vscode",       ".gradle",     "vendor",      NULL};

static bool is_noisy_name(const char *name) {
    if (!name || name[0] == '.')
        return true;
    for (int i = 0; NOISY_DIRS[i]; i++) {
        if (strcmp(name, NOISY_DIRS[i]) == 0)
            return true;
    }
    return false;
}

typedef struct {
    char name[NC_LABEL_MAX];
    bool is_dir;
} dir_item_t;

static int cmp_dir_items(const void *a, const void *b) {
    const dir_item_t *ia = a, *ib = b;
    if (ia->is_dir != ib->is_dir)
        return ia->is_dir ? -1 : 1;
    return strcmp(ia->name, ib->name);
}

static void collect_tree(const char *dir_path, int depth, strbuf_t *sb) {
    if (depth >= WS_TREE_MAX_DEPTH)
        return;
    DIR *d = opendir(dir_path);
    if (!d)
        return;
    dir_item_t items[WORKSPACE_MAX_DIR_ITEMS];
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) && n < WORKSPACE_MAX_DIR_ITEMS) {
        if (is_noisy_name(ent->d_name))
            continue;
        char full[NC_PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0)
            continue;
        nc_strlcpy(items[n].name, ent->d_name, sizeof(items[n].name));
        items[n].is_dir = S_ISDIR(st.st_mode);
        n++;
    }
    closedir(d);
    qsort(items, n, sizeof(dir_item_t), cmp_dir_items);
    int shown = n < WS_DIR_ENTRY_LIMIT ? n : WS_DIR_ENTRY_LIMIT;
    for (int i = 0; i < shown; i++) {
        for (int j = 0; j < depth; j++)
            strbuf_append_str(sb, "  ");
        strbuf_appendf(sb, "- %s%s\n", items[i].name, items[i].is_dir ? "/" : "");
        if (items[i].is_dir) {
            char sub[NC_PATH_MAX];
            snprintf(sub, sizeof(sub), "%.2045s/%.2045s", dir_path, items[i].name);
            collect_tree(sub, depth + 1, sb);
        }
    }
    if (n > WS_DIR_ENTRY_LIMIT) {
        for (int j = 0; j < depth; j++)
            strbuf_append_str(sb, "  ");
        strbuf_appendf(sb, "- ... %d more entries\n", n - WS_DIR_ENTRY_LIMIT);
    }
}

static char *build_tree_section(const char *root) {
    if (!root)
        return NULL;
    struct stat st;
    if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode))
        return NULL;
    strbuf_t sb;
    strbuf_init(&sb);
    collect_tree(root, 0, &sb);
    if (sb.len == 0) {
        strbuf_free(&sb);
        return NULL;
    }
    return strbuf_detach(&sb);
}

static char *truncate_section(const char *body, int budget_tokens) {
    if (!body || !body[0])
        return NULL;
    int approx_tok = (int)(strlen(body) / 4);
    if (approx_tok <= budget_tokens)
        return nc_xstrdup(body);
    size_t max_bytes = (size_t)budget_tokens * 4;
    if (max_bytes >= strlen(body))
        return nc_xstrdup(body);
    static const char ellipsis[] = "\n... (truncated)";
    char *trunc = malloc(max_bytes + sizeof(ellipsis));
    if (!trunc)
        return nc_xstrdup(body);
    memcpy(trunc, body, max_bytes);
    memcpy(trunc + max_bytes, ellipsis, sizeof(ellipsis));
    return trunc;
}

char *workspace_context_build(const char *cwd) {
    if (!cwd)
        return NULL;
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "Startup context.\n"
                           "Background context about the workspace layout. "
                           "May be incomplete. Use to inform responses.\n\n");
    strbuf_appendf(&sb, "## Machine / Workspace Map\n");
    strbuf_appendf(&sb, "Current working directory: %s\n", cwd);
    const char *slash = strrchr(cwd, '/');
    const char *dirname = slash ? slash + 1 : cwd;
    strbuf_appendf(&sb, "Working directory name: %s\n", dirname);
    char git_root[NC_PATH_MAX] = {0};
    git_repo_root(cwd, git_root, sizeof(git_root));
    if (git_root[0]) {
        strbuf_appendf(&sb, "Git root: %s\n", git_root);
        const char *gs = strrchr(git_root, '/');
        strbuf_appendf(&sb, "Git project: %s\n", gs ? gs + 1 : git_root);
    }
    char *tree = build_tree_section(cwd);
    if (tree) {
        char *bounded = truncate_section(tree, WS_WORKSPACE_BUDGET);
        if (bounded) {
            strbuf_append_str(&sb, "\nWorking directory tree:\n");
            strbuf_append_str(&sb, bounded);
            free(bounded);
        }
        free(tree);
    }
    if (git_root[0] && strcmp(git_root, cwd) != 0) {
        char *gt = build_tree_section(git_root);
        if (gt) {
            char *bounded = truncate_section(gt, WS_WORKSPACE_BUDGET / 2);
            if (bounded) {
                strbuf_append_str(&sb, "\nGit root tree:\n");
                strbuf_append_str(&sb, bounded);
                free(bounded);
            }
            free(gt);
        }
    }
    strbuf_append_str(&sb, "\n## Notes\n");
    strbuf_append_str(&sb, "Built at startup from a bounded local workspace scan. "
                           "Excludes repo memory instructions and project-doc prompt blends.\n");
    return strbuf_detach(&sb);
}

int workspace_context_tree_lines(const char *root, char *buf, size_t buf_sz) {
    if (!root || !buf || buf_sz < 2)
        return 0;
    strbuf_t sb;
    strbuf_init(&sb);
    collect_tree(root, 0, &sb);
    if (sb.len == 0) {
        strbuf_free(&sb);
        buf[0] = '\0';
        return 0;
    }
    int lines = 0;
    for (size_t i = 0; i < sb.len; i++) {
        if (sb.data[i] == '\n')
            lines++;
    }
    size_t copy = sb.len < buf_sz - 1 ? sb.len : buf_sz - 1;
    memcpy(buf, sb.data, copy);
    buf[copy] = '\0';
    strbuf_free(&sb);
    return lines;
}
