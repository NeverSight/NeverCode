/*
 * git_undo.c — Git-based undo via stash
 *
 * Before destructive tool calls, automatically stash uncommitted changes.
 * /undo pops the stash to revert. More robust than file_history for git repos.
 */

#include "nevercode.h"
#include "git_ops.h"

/* MAX_UNDO_STASHES from defaults.h */

typedef struct {
    int turn;
    char *stash_ref; /* stash commit hash or stash ref */
    char *description;
    time_t timestamp;
} undo_entry_t;

static struct {
    undo_entry_t entries[MAX_UNDO_STASHES];
    int count;
    int initialized;
    int is_git_repo;
} g_undo = {.initialized = 0};

static void undo_entry_reset(undo_entry_t *e) {
    free(e->stash_ref);
    free(e->description);
    memset(e, 0, sizeof(*e));
}

void git_undo_init(void) {
    char cwd[NC_PATH_MAX];
    memset(&g_undo, 0, sizeof(g_undo));
    g_undo.initialized = 1;
    g_undo.is_git_repo = nc_get_cwd(cwd, sizeof(cwd)) && git_is_repo(cwd);
}

void git_undo_free(void) {
    for (int i = 0; i < g_undo.count; i++) {
        undo_entry_reset(&g_undo.entries[i]);
    }
    memset(&g_undo, 0, sizeof(g_undo));
}

bool git_undo_available(void) {
    return g_undo.initialized && g_undo.is_git_repo;
}

bool git_undo_has_turn(int turn) {
    if (turn <= 0)
        return false;
    for (int i = g_undo.count - 1; i >= 0; i--)
        if (g_undo.entries[i].turn == turn)
            return true;
    return false;
}

int git_undo_capture_turn(int turn, const char *description) {
    char cwd[NC_PATH_MAX];
    if (!git_undo_available() || turn <= 0)
        return 0;
    if (!nc_get_cwd(cwd, sizeof(cwd)))
        return 0;
    for (int i = g_undo.count - 1; i >= 0; i--) {
        if (g_undo.entries[i].turn == turn)
            return 0;
    }
    if (!git_has_uncommitted(cwd))
        return 0;

    char message[NC_MSG_MAX];
    if (description && *description)
        snprintf(message, sizeof(message), NC_STASH_MSG_PREFIX "%d:%s", turn, description);
    else
        snprintf(message, sizeof(message), NC_STASH_MSG_PREFIX "%d", turn);

    char output[NC_MSG_MAX];
    if (git_stash_push(cwd, message, true, output, sizeof(output)) != 0)
        return -1;
    if (strstr(output, "No local changes"))
        return 0;

    char hashes[NC_MSG_MAX];
    if (git_stash_list_hashes(cwd, hashes, sizeof(hashes)) != 0 || !hashes[0])
        return -1;
    char *newline = strchr(hashes, '\n');
    if (newline)
        *newline = '\0';
    if (!hashes[0])
        return -1;
    if (git_stash_apply(cwd, hashes) != 0)
        return -1;

    if (g_undo.count >= MAX_UNDO_STASHES) {
        undo_entry_reset(&g_undo.entries[0]);
        memmove(&g_undo.entries[0], &g_undo.entries[1],
                sizeof(g_undo.entries[0]) * (MAX_UNDO_STASHES - 1));
        memset(&g_undo.entries[MAX_UNDO_STASHES - 1], 0, sizeof(g_undo.entries[0]));
        g_undo.count = MAX_UNDO_STASHES - 1;
    }

    undo_entry_t *e = &g_undo.entries[g_undo.count++];
    e->turn = turn;
    e->stash_ref = nc_xstrdup(hashes);
    e->description = description ? nc_xstrdup(description) : NULL;
    e->timestamp = nc_time_now();
    if (!e->stash_ref) {
        undo_entry_reset(e);
        g_undo.count--;
        return -1;
    }
    return 0;
}

int git_undo_restore(int turn) {
    char cwd[NC_PATH_MAX];
    if (!git_undo_available())
        return -1;
    if (!nc_get_cwd(cwd, sizeof(cwd)))
        return -1;

    /* Find the stash entry for this turn */
    int idx = -1;
    for (int i = g_undo.count - 1; i >= 0; i--) {
        if (g_undo.entries[i].turn == turn) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return -1;

    const char *stored_ref = g_undo.entries[idx].stash_ref;
    if (stored_ref && *stored_ref)
        return git_stash_restore_exact(cwd, stored_ref) == 0 ? 0 : -1;

    /* Search git stash list for our marker */
    char stash_list[NC_PATH_MAX];
    if (git_stash_list(cwd, stash_list, sizeof(stash_list)) != 0)
        return -1;

    char target[NC_FMT_MAX];
    snprintf(target, sizeof(target), NC_STASH_MSG_PREFIX "%d", turn);

    char *line = stash_list;
    char stash_id[NC_SHORT_MAX] = {0};
    while (line && *line) {
        if (strstr(line, target)) {
            char *colon = strchr(line, ':');
            if (colon) {
                size_t len = (size_t)(colon - line);
                if (len < sizeof(stash_id)) {
                    memcpy(stash_id, line, len);
                    stash_id[len] = '\0';
                }
            }
            break;
        }
        line = strchr(line, '\n');
        if (line)
            line++;
    }

    if (stash_id[0] == '\0') {
        /* No matching stash found, fallback to file_history */
        return -1;
    }
    return git_stash_restore_exact(cwd, stash_id) == 0 ? 0 : -1;
}

int git_undo_count(void) {
    return g_undo.count;
}

char *git_undo_format_list(void) {
    strbuf_t sb;
    strbuf_init(&sb);

    if (g_undo.count == 0) {
        strbuf_append_str(&sb, "No undo checkpoints available.\n");
        return strbuf_detach(&sb);
    }

    strbuf_append_str(&sb, "Git undo checkpoints:\n");
    for (int i = g_undo.count - 1; i >= 0; i--) {
        char line[NC_TAG_MAX];
        struct tm *t = localtime(&g_undo.entries[i].timestamp);
        char ts[NC_SHORT_MAX];
        strftime(ts, sizeof(ts), "%H:%M:%S", t);
        snprintf(line, sizeof(line), "  Turn %d  [%s]  %s\n", g_undo.entries[i].turn, ts,
                 g_undo.entries[i].stash_ref ? g_undo.entries[i].stash_ref : "");
        strbuf_append_str(&sb, line);
    }
    return strbuf_detach(&sb);
}
