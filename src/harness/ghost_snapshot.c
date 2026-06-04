/*
 * ghost_snapshot.c — Transparent git snapshots for session rollback
 *
 * Creates lightweight git commits on a detached ref namespace
 * without polluting branch history. Enables per-turn rollback
 * during long autonomous sessions.
 */

#include "nevercode.h"
#include "harness.h"
#include "git_ops.h"

static struct {
    char session_id[NC_TAG_MAX];
    char refs[GHOST_SNAPSHOT_MAX][64];
    int turns[GHOST_SNAPSHOT_MAX];
    int count;
    int initialized;
    int is_git;
} g_ghosts;

void ghost_snapshot_init(const char *session_id) {
    char cwd[NC_PATH_MAX];
    memset(&g_ghosts, 0, sizeof(g_ghosts));
    g_ghosts.initialized = 1;
    g_ghosts.is_git = nc_get_cwd(cwd, sizeof(cwd)) && git_is_repo(cwd);
    if (session_id)
        nc_strlcpy(g_ghosts.session_id, session_id, sizeof(g_ghosts.session_id));
}

void ghost_snapshot_free(void) {
    memset(&g_ghosts, 0, sizeof(g_ghosts));
}

bool ghost_snapshot_available(void) {
    return g_ghosts.initialized && g_ghosts.is_git;
}

int ghost_snapshot_create(int turn) {
    char cwd[NC_PATH_MAX];
    if (!ghost_snapshot_available())
        return -1;
    if (g_ghosts.count >= GHOST_SNAPSHOT_MAX)
        return -1;
    if (!nc_get_cwd(cwd, sizeof(cwd)))
        return -1;
    char tree_hash[NC_FMT_MAX];
    if (git_add_all(cwd) != 0)
        return -1;
    if (git_write_tree(cwd, tree_hash, sizeof(tree_hash)) != 0)
        return -1;
    char msg[NC_LABEL_MAX];
    snprintf(msg, sizeof(msg), "snapshot turn=%d session=%s ts=%ld", turn, g_ghosts.session_id,
             nc_wall_sec());
    char commit_hash[NC_FMT_MAX];
    if (git_commit_tree(cwd, tree_hash, msg, commit_hash, sizeof(commit_hash)) != 0)
        return -1;
    char ref_name[NC_TAG_MAX];
    snprintf(ref_name, sizeof(ref_name), "refs/nevercode/snapshots/%s/turn-%d", g_ghosts.session_id,
             turn);
    if (git_ref_set(cwd, ref_name, commit_hash) != 0)
        return -1;
    int idx = g_ghosts.count++;
    nc_strlcpy(g_ghosts.refs[idx], commit_hash, sizeof(g_ghosts.refs[idx]));
    g_ghosts.turns[idx] = turn;
    return idx;
}

int ghost_snapshot_restore(int turn) {
    char cwd[NC_PATH_MAX];
    if (!ghost_snapshot_available())
        return -1;
    if (!nc_get_cwd(cwd, sizeof(cwd)))
        return -1;
    int found = -1;
    for (int i = g_ghosts.count - 1; i >= 0; i--)
        if (g_ghosts.turns[i] == turn) {
            found = i;
            break;
        }
    if (found < 0)
        return -1;
    if (git_read_tree(cwd, g_ghosts.refs[found]) != 0)
        return -1;
    return git_checkout_index_all(cwd);
}

int ghost_snapshot_restore_latest(void) {
    if (!ghost_snapshot_available() || g_ghosts.count == 0)
        return -1;
    return ghost_snapshot_restore(g_ghosts.turns[g_ghosts.count - 1]);
}

int ghost_snapshot_count(void) {
    return g_ghosts.count;
}

int ghost_snapshot_turn_at(int idx) {
    if (idx < 0 || idx >= g_ghosts.count)
        return -1;
    return g_ghosts.turns[idx];
}

int ghost_snapshot_cleanup(const char *session_id) {
    char cwd[NC_PATH_MAX];
    if (!g_ghosts.is_git)
        return 0;
    if (!nc_get_cwd(cwd, sizeof(cwd)))
        return 0;
    const char *sid = session_id ? session_id : g_ghosts.session_id;
    if (!sid[0])
        return 0;
    char prefix[NC_LABEL_MAX];
    snprintf(prefix, sizeof(prefix), "refs/nevercode/snapshots/%s/", sid);
    char refs_buf[NC_PATH_MAX];
    if (git_ref_list(cwd, prefix, refs_buf, sizeof(refs_buf)) != 0)
        return 0;
    int cleaned = 0;
    char *line = strtok(refs_buf, "\n");
    while (line) {
        if (git_ref_delete(cwd, line) == 0)
            cleaned++;
        line = strtok(NULL, "\n");
    }
    return cleaned;
}

char *ghost_snapshot_format_list(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Snapshots: %d\n", g_ghosts.count);
    for (int i = 0; i < g_ghosts.count; i++)
        strbuf_appendf(&sb, "  [%d] turn=%d ref=%.8s...\n", i, g_ghosts.turns[i], g_ghosts.refs[i]);
    return strbuf_detach(&sb);
}
