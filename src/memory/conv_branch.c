/*
 * conv_branch.c — Enhanced conversation branching with git stash snapshots
 *
 * /branch now captures both conversation state AND file system state
 * via git stash, enabling full point-in-time restore.
 */

#include "nevercode.h"
#include "conversation.h"
#include "git_ops.h"

#define MAX_BRANCHES MAX_CONV_BRANCHES
#define BRANCH_STASH_PREFIX NC_BRANCH_STASH_PREFIX

typedef struct {
    char *session_id;
    char *name;
    char *stash_ref; /* stash commit hash or NULL */
    int msg_count;
    long timestamp;
    bool has_file_snapshot;
} branch_entry_t;

static branch_entry_t g_branches[MAX_BRANCHES];
static int g_num_branches = 0;

void conv_branch_init(void) {
    g_num_branches = 0;
    memset(g_branches, 0, sizeof(g_branches));
}

void conv_branch_free(void) {
    for (int i = 0; i < g_num_branches; i++) {
        free(g_branches[i].session_id);
        free(g_branches[i].name);
        free(g_branches[i].stash_ref);
    }
    g_num_branches = 0;
}

static bool is_git_repo(void) {
    char cwd[NC_PATH_MAX];
    if (!nc_get_cwd(cwd, sizeof(cwd)))
        return false;
    return git_is_repo(cwd);
}

static char *git_stash_create(const char *session_id, const char *branch_name) {
    char cwd[NC_PATH_MAX];
    if (!is_git_repo())
        return NULL;
    if (!nc_get_cwd(cwd, sizeof(cwd)))
        return NULL;

    char message[NC_MSG_MAX];
    snprintf(message, sizeof(message), "%s%s%s%s", BRANCH_STASH_PREFIX,
             session_id ? session_id : "unnamed", branch_name ? ":" : "",
             branch_name ? branch_name : "");
    char output[NC_MSG_MAX];
    if (git_stash_push(cwd, message, true, output, sizeof(output)) != 0)
        return NULL;
    if (strstr(output, "No local changes"))
        return NULL;

    char refs[NC_MSG_MAX];
    if (git_stash_list_hashes(cwd, refs, sizeof(refs)) != 0 || !refs[0])
        return NULL;
    char *newline = strchr(refs, '\n');
    if (newline)
        *newline = '\0';
    if (!refs[0])
        return NULL;

    char *result = nc_xstrdup(refs);
    if (!result)
        return NULL;
    if (git_stash_apply(cwd, result) != 0) {
        free(result);
        return NULL;
    }
    return result;
}

int conv_branch_create(const char *session_id, const char *name, int msg_count) {
    if (g_num_branches >= MAX_BRANCHES)
        return -1;
    if (!session_id)
        return -1;

    branch_entry_t *b = &g_branches[g_num_branches];
    b->session_id = nc_xstrdup(session_id);
    b->name = name ? nc_xstrdup(name) : NULL;
    b->msg_count = msg_count;
    b->timestamp = nc_time_now();

    b->stash_ref = git_stash_create(session_id, name);
    b->has_file_snapshot = (b->stash_ref != NULL);

    g_num_branches++;
    return 0;
}

int conv_branch_count(void) {
    return g_num_branches;
}

const char *conv_branch_session_id(int idx) {
    if (idx < 0 || idx >= g_num_branches)
        return NULL;
    return g_branches[idx].session_id;
}

const char *conv_branch_name(int idx) {
    if (idx < 0 || idx >= g_num_branches)
        return NULL;
    return g_branches[idx].name;
}

const char *conv_branch_snapshot_ref(int idx) {
    if (idx < 0 || idx >= g_num_branches)
        return NULL;
    return g_branches[idx].stash_ref;
}

bool conv_branch_has_snapshot(int idx) {
    if (idx < 0 || idx >= g_num_branches)
        return false;
    return g_branches[idx].has_file_snapshot;
}

int conv_branch_msg_count(int idx) {
    if (idx < 0 || idx >= g_num_branches)
        return 0;
    return g_branches[idx].msg_count;
}

char *conv_branch_format_list(void) {
    if (g_num_branches == 0)
        return nc_xstrdup("(no branches)\n");

    strbuf_t sb;
    strbuf_init(&sb);

    for (int i = 0; i < g_num_branches; i++) {
        branch_entry_t *b = &g_branches[i];
        char line[NC_MSG_MAX];
        char ts[NC_FMT_MAX];
        struct tm *tm = localtime(&b->timestamp);
        strftime(ts, sizeof(ts), "%H:%M:%S", tm);

        snprintf(line, sizeof(line), "  %d. %s%s [%d msgs, %s] %s\n", i + 1,
                 b->name ? b->name : b->session_id, b->name ? "" : "", b->msg_count, ts,
                 b->has_file_snapshot ? "(+files)" : "");
        strbuf_append_str(&sb, line);
    }
    return strbuf_detach(&sb);
}

int conv_branch_find(const char *query) {
    if (!query)
        return -1;
    for (int i = 0; i < g_num_branches; i++) {
        if (g_branches[i].name && strcmp(g_branches[i].name, query) == 0)
            return i;
        if (strcmp(g_branches[i].session_id, query) == 0)
            return i;
    }
    /* Try numeric index */
    char *end;
    long idx = strtol(query, &end, 10);
    if (*end == '\0' && idx >= 1 && idx <= g_num_branches)
        return (int)(idx - 1);
    return -1;
}
