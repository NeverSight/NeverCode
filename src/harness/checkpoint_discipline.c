/*
 * checkpoint_discipline.c — Periodic git checkpoint creation during
 * long-running sessions for crash recovery and rollback.
 *
 * After every N file-changing turns, the working tree is committed
 * and tagged for rollback.  Complements ghost snapshots with broader
 * git tool compatibility.
 *
 * Shadow repo mode uses a separate GIT_DIR so no git state leaks
 * into the user's project.  The exclude list prevents bloating
 * snapshots with build artifacts and vendored dependencies.
 */

#include "harness.h"
#include "git_ops.h"

static const char *const CP_DEFAULT_EXCLUDES[] = {
    "node_modules/",
    "dist/",
    "build/",
    ".env",
    ".env.*",
    ".env.local",
    "__pycache__/",
    "*.pyc",
    "*.pyo",
    ".DS_Store",
    "*.log",
    ".cache/",
    ".next/",
    ".nuxt/",
    "coverage/",
    ".pytest_cache/",
    ".venv/",
    "venv/",
    ".git/",
    ".tox/",
    "*.o",
    "*.a",
    "*.so",
    "*.dylib",
    NULL,
};

typedef struct {
    int turn;
    long timestamp;
    char tag[NC_TAG_MAX];
} cp_entry_t;

typedef struct {
    cp_entry_t entries[NC_CHECKPOINT_MAX_ENTRIES];
    int count;
    int interval;
    int turns_since_cp;
    int consecutive_failures;
    bool active;
    char session_id[NC_LABEL_MAX];
} cp_ctx_t;

static cp_ctx_t g_cp;

/* ── Init / Free ── */

void checkpoint_discipline_init(const char *session_id, int interval_turns) {
    memset(&g_cp, 0, sizeof(g_cp));
    g_cp.interval = interval_turns > 0 ? interval_turns : HARNESS_CHECKPOINT_INTERVAL;
    g_cp.active = true;
    if (session_id)
        nc_strlcpy(g_cp.session_id, session_id, sizeof(g_cp.session_id));
}

void checkpoint_discipline_free(void) {
    memset(&g_cp, 0, sizeof(g_cp));
}

/* ── Git helpers ── */

/* ── Trigger check ── */

bool checkpoint_discipline_should_fire(int turn, bool files_changed) {
    (void)turn;
    char cwd[NC_PATH_MAX];
    if (!g_cp.active || !nc_get_cwd(cwd, sizeof(cwd)) || !git_is_repo(cwd))
        return false;
    if (files_changed)
        g_cp.turns_since_cp++;
    return g_cp.turns_since_cp >= g_cp.interval;
}

/* ── Create checkpoint ── */

int checkpoint_discipline_create(int turn) {
    char cwd[NC_PATH_MAX];
    if (!g_cp.active || g_cp.count >= NC_CHECKPOINT_MAX_ENTRIES || !nc_get_cwd(cwd, sizeof(cwd)) ||
        !git_is_repo(cwd))
        return -1;

    int tracked_files = git_tracked_file_count(cwd);
    if (tracked_files > CHECKPOINT_MAX_FILES) {
        g_cp.turns_since_cp = 0;
        return 0;
    }
    if (tracked_files < 0)
        return -1;

    if (!git_has_uncommitted(cwd)) {
        g_cp.turns_since_cp = 0;
        return 0;
    }

    cp_entry_t *e = &g_cp.entries[g_cp.count];
    memset(e, 0, sizeof(*e));
    e->turn = turn;
    e->timestamp = nc_wall_sec();
    snprintf(e->tag, sizeof(e->tag), "%s/%s/t%d", NC_CHECKPOINT_TAG_PREFIX,
             g_cp.session_id[0] ? g_cp.session_id : "default", turn);

    char message[NC_LABEL_MAX];
    snprintf(message, sizeof(message), "checkpoint turn %d", turn);
    if (git_checkpoint_commit(cwd, message) != 0) {
        g_cp.consecutive_failures++;
        if (g_cp.consecutive_failures >= 3)
            g_cp.active = false;
        return -1;
    }
    g_cp.consecutive_failures = 0;

    if (git_tag_create(cwd, e->tag) != 0) {
        g_cp.consecutive_failures++;
        if (g_cp.consecutive_failures >= 3)
            g_cp.active = false;
        return -1;
    }
    g_cp.consecutive_failures = 0;
    g_cp.count++;
    g_cp.turns_since_cp = 0;
    return 0;
}

/* ── Restore to checkpoint ── */

int checkpoint_discipline_restore(int turn) {
    char cwd[NC_PATH_MAX];
    if (!nc_get_cwd(cwd, sizeof(cwd)) || !git_is_repo(cwd))
        return -1;
    for (int i = g_cp.count - 1; i >= 0; i--) {
        if (g_cp.entries[i].turn <= turn)
            return git_restore_all_from_ref(cwd, g_cp.entries[i].tag) == 0 ? 0 : -1;
    }
    return -1;
}

/* ── Queries ── */

int checkpoint_discipline_count(void) {
    return g_cp.count;
}

/* ── Cleanup all tags ── */

int checkpoint_discipline_cleanup(void) {
    char cwd[NC_PATH_MAX];
    if (!nc_get_cwd(cwd, sizeof(cwd)) || !git_is_repo(cwd)) {
        g_cp.count = 0;
        return 0;
    }
    int removed = 0;
    for (int i = 0; i < g_cp.count; i++) {
        if (git_tag_delete(cwd, g_cp.entries[i].tag) == 0)
            removed++;
    }
    g_cp.count = 0;
    return removed;
}

/* ── Exclude pattern query ── */

int checkpoint_exclude_count(void) {
    int n = 0;
    while (CP_DEFAULT_EXCLUDES[n])
        n++;
    return n;
}

const char *checkpoint_exclude_pattern(int idx) {
    if (idx < 0)
        return NULL;
    int n = checkpoint_exclude_count();
    if (idx >= n)
        return NULL;
    return CP_DEFAULT_EXCLUDES[idx];
}

void checkpoint_discipline_snapshot(checkpoint_discipline_snapshot_t *out) {
    if (!out)
        return;

    memset(out, 0, sizeof(*out));
    long now_s = nc_wall_sec();
    out->count = g_cp.count;
    out->interval = g_cp.interval;
    out->turns_since_cp = g_cp.turns_since_cp;
    out->consecutive_failures = g_cp.consecutive_failures;
    out->active = g_cp.active;
    nc_strlcpy(out->session_id, g_cp.session_id, sizeof(out->session_id));

    if (out->count < 0)
        out->count = 0;
    if (out->count > NC_CHECKPOINT_MAX_ENTRIES)
        out->count = NC_CHECKPOINT_MAX_ENTRIES;

    for (int i = 0; i < out->count; i++) {
        out->entries[i].turn = g_cp.entries[i].turn;
        nc_strlcpy(out->entries[i].tag, g_cp.entries[i].tag, sizeof(out->entries[i].tag));
        out->entries[i].age_s = now_s - g_cp.entries[i].timestamp;
        if (out->entries[i].age_s < 0)
            out->entries[i].age_s = 0;
    }
}

void checkpoint_discipline_restore_state(const checkpoint_discipline_snapshot_t *snap) {
    checkpoint_discipline_free();
    if (!snap) {
        checkpoint_discipline_init(NULL, 0);
        return;
    }

    checkpoint_discipline_init(snap->session_id[0] ? snap->session_id : NULL, snap->interval);
    g_cp.turns_since_cp = snap->turns_since_cp;
    g_cp.consecutive_failures = snap->consecutive_failures;
    g_cp.active = snap->active;
    g_cp.count = snap->count;

    if (g_cp.turns_since_cp < 0)
        g_cp.turns_since_cp = 0;
    if (g_cp.consecutive_failures < 0)
        g_cp.consecutive_failures = 0;
    if (g_cp.count < 0)
        g_cp.count = 0;
    if (g_cp.count > NC_CHECKPOINT_MAX_ENTRIES)
        g_cp.count = NC_CHECKPOINT_MAX_ENTRIES;

    long now_s = nc_wall_sec();
    for (int i = 0; i < g_cp.count; i++) {
        g_cp.entries[i].turn = snap->entries[i].turn;
        nc_strlcpy(g_cp.entries[i].tag, snap->entries[i].tag, sizeof(g_cp.entries[i].tag));
        g_cp.entries[i].timestamp = now_s - snap->entries[i].age_s;
        if (g_cp.entries[i].timestamp > now_s)
            g_cp.entries[i].timestamp = now_s;
    }
}

/* ── Formatting ── */

char *checkpoint_discipline_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb,
                   "Checkpoints: %d | interval: every %d file-changing turns | "
                   "turns since last: %d | excludes: %d patterns",
                   g_cp.count, g_cp.interval, g_cp.turns_since_cp, checkpoint_exclude_count());

    if (g_cp.count > 0) {
        cp_entry_t *last = &g_cp.entries[g_cp.count - 1];
        strbuf_appendf(&sb, " | last: turn %d (%s)", last->turn, last->tag);
    }
    return strbuf_detach(&sb);
}
