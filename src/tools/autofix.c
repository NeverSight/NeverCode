/*
 * autofix.c — Automated issue fixing tool
 *
 * Detects and applies fixes for common code issues:
 * linter errors, missing imports, formatting, etc.
 * Works with the agent loop to suggest corrective patches.
 */

#include "nevercode.h"
#include "defaults.h"

/* ── Internal types ── */

typedef struct {
    fix_category_t category;
    char file[NC_PATH_MAX];
    int line;
    char message[TERR_DETAIL_MAX];
    char fix_cmd[TERR_DETAIL_MAX];
    fix_status_t status;
} fix_entry_t;

typedef struct {
    fix_entry_t entries[MAX_DIAGNOSTICS];
    int count;
    int applied;
    int failed;
} fix_ctx_t;

static fix_ctx_t g_fixes;

/* ── Init / Free ── */

void autofix_init(void) {
    memset(&g_fixes, 0, sizeof(g_fixes));
}

void autofix_free(void) {
    g_fixes.count = 0;
    g_fixes.applied = 0;
    g_fixes.failed = 0;
}

/* ── Detection ── */

static fix_category_t detect_category(const char *msg) {
    if (!msg)
        return FIX_LINT;
    if (strstr(msg, "import") || strstr(msg, "require") || strstr(msg, "include"))
        return FIX_IMPORT;
    if (strstr(msg, "format") || strstr(msg, "indent") || strstr(msg, "whitespace"))
        return FIX_FORMAT;
    if (strstr(msg, "type") || strstr(msg, "expected") || strstr(msg, "cannot assign"))
        return FIX_TYPECHECK;
    if (strstr(msg, "build") || strstr(msg, "compile") || strstr(msg, "link"))
        return FIX_BUILD;
    if (strstr(msg, "test") || strstr(msg, "assert") || strstr(msg, "FAIL"))
        return FIX_TEST;
    return FIX_LINT;
}

int autofix_add_issue(const char *file, int line, const char *message) {
    if (g_fixes.count >= MAX_DIAGNOSTICS)
        return -1;
    if (!file || !message)
        return -1;

    fix_entry_t *e = &g_fixes.entries[g_fixes.count];
    e->category = detect_category(message);
    nc_strlcpy(e->file, file, sizeof(e->file));
    e->line = line;
    nc_strlcpy(e->message, message, sizeof(e->message));
    e->status = FIX_STATUS_PENDING;
    g_fixes.count++;
    return g_fixes.count - 1;
}

/* ── Fix application ── */

int autofix_apply(int idx) {
    if (idx < 0 || idx >= g_fixes.count)
        return -1;
    fix_entry_t *e = &g_fixes.entries[idx];
    if (e->status != FIX_STATUS_PENDING)
        return -1;

    struct stat st;
    if (stat(e->file, &st) != 0) {
        e->status = FIX_STATUS_FAILED;
        g_fixes.failed++;
        return -1;
    }

    if (e->fix_cmd[0]) {
        int rc = nc_exec_quiet(e->fix_cmd);
        if (rc == 0) {
            e->status = FIX_STATUS_APPLIED;
            g_fixes.applied++;
            return 0;
        }
        e->status = FIX_STATUS_FAILED;
        g_fixes.failed++;
        return -1;
    }

    e->status = FIX_STATUS_SKIPPED;
    return 1;
}

/* ── Queries ── */

int autofix_count(void) {
    return g_fixes.count;
}
int autofix_pending_count(void) {
    int c = 0;
    for (int i = 0; i < g_fixes.count; i++)
        if (g_fixes.entries[i].status == FIX_STATUS_PENDING)
            c++;
    return c;
}

const char *fix_category_name(fix_category_t c) {
    switch (c) {
    case FIX_LINT:
        return "lint";
    case FIX_FORMAT:
        return "format";
    case FIX_IMPORT:
        return "import";
    case FIX_TYPECHECK:
        return "typecheck";
    case FIX_BUILD:
        return "build";
    case FIX_TEST:
        return "test";
    }
    return "unknown";
}

const char *fix_status_name(fix_status_t s) {
    switch (s) {
    case FIX_STATUS_PENDING:
        return "pending";
    case FIX_STATUS_APPLIED:
        return "applied";
    case FIX_STATUS_FAILED:
        return "failed";
    case FIX_STATUS_SKIPPED:
        return "skipped";
    }
    return "unknown";
}

char *autofix_format(void) {
    if (g_fixes.count == 0)
        return nc_xstrdup("No issues detected.\n");

    size_t sz = (size_t)g_fixes.count * 256 + 256;
    char *buf = malloc(sz);
    if (!buf)
        return NULL;

    int off = snprintf(buf, sz, "Auto-fix: %d issues (%d applied, %d failed, %d pending)\n",
                       g_fixes.count, g_fixes.applied, g_fixes.failed, autofix_pending_count());
    for (int i = 0; i < g_fixes.count; i++) {
        const fix_entry_t *e = &g_fixes.entries[i];
        off += snprintf(buf + off, sz - (size_t)off, "  [%s] %s:%d — %s [%s]\n",
                        fix_category_name(e->category), e->file, e->line, e->message,
                        fix_status_name(e->status));
    }
    return buf;
}
