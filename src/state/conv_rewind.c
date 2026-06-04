/*
 * conv_rewind.c — conversation-level backtracking
 *
 * Maintains a stack of conversation checkpoints so the user
 * can rewind to any previous user turn without losing the
 * conversation state file.
 */

#include "nevercode.h"
#include "defaults.h"

typedef struct {
    int turn_idx;
    int msg_count;
    char label[NC_LABEL_MAX];
    char *snapshot_json;
} rewind_point_t;

typedef struct {
    rewind_point_t pts[REWIND_MAX_POINTS];
    int count;
    int cursor;
} rewind_stack_t;

static rewind_stack_t g_rw;

void conv_rewind_init(void) {
    memset(&g_rw, 0, sizeof(g_rw));
    g_rw.cursor = -1;
}

void conv_rewind_free(void) {
    for (int i = 0; i < g_rw.count; i++)
        free(g_rw.pts[i].snapshot_json);
    memset(&g_rw, 0, sizeof(g_rw));
    g_rw.cursor = -1;
}

bool conv_rewind_checkpoint(int turn_idx, int msg_count, const char *label,
                            const char *snapshot_json) {
    if (g_rw.count >= REWIND_MAX_POINTS) {
        free(g_rw.pts[0].snapshot_json);
        memmove(&g_rw.pts[0], &g_rw.pts[1],
                (size_t)(REWIND_MAX_POINTS - 1) * sizeof(rewind_point_t));
        g_rw.count = REWIND_MAX_POINTS - 1;
    }

    rewind_point_t *p = &g_rw.pts[g_rw.count];
    p->turn_idx = turn_idx;
    p->msg_count = msg_count;
    nc_strlcpy(p->label, label ? label : "", sizeof(p->label));
    p->snapshot_json = snapshot_json ? nc_xstrdup(snapshot_json) : NULL;

    g_rw.cursor = g_rw.count;
    g_rw.count++;
    return true;
}

int conv_rewind_count(void) {
    return g_rw.count;
}

int conv_rewind_cursor(void) {
    return g_rw.cursor;
}

bool conv_rewind_step_back(void) {
    if (g_rw.cursor <= 0)
        return false;
    g_rw.cursor--;
    return true;
}

bool conv_rewind_step_forward(void) {
    if (g_rw.cursor >= g_rw.count - 1)
        return false;
    g_rw.cursor++;
    return true;
}

bool conv_rewind_goto(int idx) {
    if (idx < 0 || idx >= g_rw.count)
        return false;
    g_rw.cursor = idx;
    return true;
}

const char *conv_rewind_snapshot(int idx) {
    if (idx < 0 || idx >= g_rw.count)
        return NULL;
    return g_rw.pts[idx].snapshot_json;
}

int conv_rewind_turn_at(int idx) {
    if (idx < 0 || idx >= g_rw.count)
        return -1;
    return g_rw.pts[idx].turn_idx;
}

int conv_rewind_msg_count_at(int idx) {
    if (idx < 0 || idx >= g_rw.count)
        return 0;
    return g_rw.pts[idx].msg_count;
}

const char *conv_rewind_label_at(int idx) {
    if (idx < 0 || idx >= g_rw.count)
        return "";
    return g_rw.pts[idx].label;
}

bool conv_rewind_truncate_after(int idx) {
    if (idx < 0 || idx >= g_rw.count)
        return false;
    for (int i = idx + 1; i < g_rw.count; i++)
        free(g_rw.pts[i].snapshot_json);
    g_rw.count = idx + 1;
    g_rw.cursor = idx;
    return true;
}

char *conv_rewind_apply(int idx) {
    if (idx < 0 || idx >= g_rw.count)
        return NULL;
    conv_rewind_truncate_after(idx);
    return g_rw.pts[idx].snapshot_json ? nc_xstrdup(g_rw.pts[idx].snapshot_json) : NULL;
}

char *conv_rewind_list(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    for (int i = 0; i < g_rw.count; i++) {
        const rewind_point_t *p = &g_rw.pts[i];
        const char *marker = (i == g_rw.cursor) ? " <-" : "";
        strbuf_appendf(&sb, "  [%d] turn %d (%d msgs) %s%s\n", i, p->turn_idx, p->msg_count,
                       p->label, marker);
    }
    return strbuf_detach(&sb);
}
