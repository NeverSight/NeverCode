/*
 * repo_map.c — Compact project structure overview for context engineering
 */
#include "nevercode.h"
static const char *g_skip_exts[] = {".o",   ".a",     ".so",   ".dylib", ".dll",  ".exe", ".pyc",
                                    ".pyo", ".class", ".jar",  ".wasm",  ".png",  ".jpg", ".jpeg",
                                    ".gif", ".bmp",   ".ico",  ".mp3",   ".mp4",  ".wav", ".avi",
                                    ".mov", ".zip",   ".tar",  ".gz",    ".bz2",  ".xz",  ".7z",
                                    ".pdf", ".doc",   ".docx", ".xls",   ".xlsx", NULL};
static bool rmap_skip_dir(const char *n) {
    if (n[0] == '.' && RMAP_SKIP_HIDDEN)
        return true;
    return nc_is_noise_dir(n);
}
static bool rmap_skip_file(const char *n) {
    const char *d = strrchr(n, '.');
    if (!d)
        return false;
    for (int i = 0; g_skip_exts[i]; i++)
        if (strcmp(d, g_skip_exts[i]) == 0)
            return true;
    return false;
}
typedef struct {
    char relpath[NC_PATH_MAX];
    int lines;
    long size_bytes;
    bool is_dir;
    int depth;
    int child_count;
} rmap_entry_t;
typedef struct {
    rmap_entry_t entries[RMAP_MAX_ENTRIES];
    int count;
    int total_files;
    int total_dirs;
    long total_bytes;
    int total_lines;
} rmap_ctx_t;
static int rmap_count_lines(const char *p) {
    FILE *f = fopen(p, "r");
    if (!f)
        return 0;
    int l = 0;
    int c;
    while ((c = fgetc(f)) != EOF)
        if (c == '\n')
            l++;
    fclose(f);
    return l > 0 ? l : 1;
}
static void rmap_walk(rmap_ctx_t *ctx, const char *dir, const char *rel, int depth) {
    if (depth > RMAP_MAX_DEPTH || ctx->count >= RMAP_MAX_ENTRIES)
        return;
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && ctx->count < RMAP_MAX_ENTRIES) {
        if (ent->d_name[0] == '.' &&
            (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
            continue;
        char fp[NC_PATH_MAX];
        snprintf(fp, sizeof(fp), "%s/%s", dir, ent->d_name);
        char rp[NC_PATH_MAX];
        if (rel[0])
            snprintf(rp, sizeof(rp), "%s/%s", rel, ent->d_name);
        else
            nc_strlcpy(rp, ent->d_name, sizeof(rp));
        struct stat st;
        if (stat(fp, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (rmap_skip_dir(ent->d_name))
                continue;
            rmap_entry_t *e = &ctx->entries[ctx->count++];
            nc_strlcpy(e->relpath, rp, sizeof(e->relpath));
            e->is_dir = true;
            e->depth = depth;
            e->child_count = 0;
            int before = ctx->count;
            ctx->total_dirs++;
            rmap_walk(ctx, fp, rp, depth + 1);
            e->child_count = ctx->count - before;
        } else if (S_ISREG(st.st_mode)) {
            if (rmap_skip_file(ent->d_name))
                continue;
            rmap_entry_t *e = &ctx->entries[ctx->count++];
            nc_strlcpy(e->relpath, rp, sizeof(e->relpath));
            e->is_dir = false;
            e->depth = depth;
            e->size_bytes = st.st_size;
            e->lines = rmap_count_lines(fp);
            ctx->total_files++;
            ctx->total_bytes += st.st_size;
            ctx->total_lines += e->lines;
        }
    }
    closedir(d);
}
char *repo_map_generate(const char *root, int max_tok) {
    if (!root)
        return NULL;
    rmap_ctx_t *ctx = calloc(1, sizeof(rmap_ctx_t));
    if (!ctx)
        return NULL;
    rmap_walk(ctx, root, "", 0);
    int budget = max_tok > 0 ? max_tok : RMAP_BUDGET_TOKENS;
    int cap = budget * APPROX_BYTES_PER_TOKEN;
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "# Repository Map\nFiles: %d | Dirs: %d | Lines: %d | Size: %ldKB\n\n",
                   ctx->total_files, ctx->total_dirs, ctx->total_lines, ctx->total_bytes / 1024);
    for (int i = 0; i < ctx->count && (int)sb.len < cap; i++) {
        const rmap_entry_t *e = &ctx->entries[i];
        for (int d = 0; d < e->depth; d++)
            strbuf_append_str(&sb, "  ");
        if (e->is_dir)
            strbuf_appendf(&sb, "%s/ (%d items)\n", e->relpath, e->child_count);
        else {
            const char *b = strrchr(e->relpath, '/');
            b = b ? b + 1 : e->relpath;
            if (e->lines > 0)
                strbuf_appendf(&sb, "%s (%dL)\n", b, e->lines);
            else
                strbuf_appendf(&sb, "%s\n", b);
        }
    }
    if ((int)sb.len >= cap)
        strbuf_append_str(&sb, "\n... (truncated)\n");
    free(ctx);
    return strbuf_detach(&sb);
}
int repo_map_entry_count(const char *root) {
    if (!root)
        return 0;
    rmap_ctx_t *ctx = calloc(1, sizeof(rmap_ctx_t));
    if (!ctx)
        return 0;
    rmap_walk(ctx, root, "", 0);
    int c = ctx->count;
    free(ctx);
    return c;
}
char *repo_map_summary(const char *root) {
    if (!root)
        return NULL;
    rmap_ctx_t *ctx = calloc(1, sizeof(rmap_ctx_t));
    if (!ctx)
        return NULL;
    rmap_walk(ctx, root, "", 0);
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "%d files, %d dirs, %d lines, %ldKB", ctx->total_files, ctx->total_dirs,
                   ctx->total_lines, ctx->total_bytes / 1024);
    free(ctx);
    return strbuf_detach(&sb);
}
