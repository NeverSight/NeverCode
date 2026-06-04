/*
 * turn_meta.c — Per-turn structured metrics collector
 *
 * Records tokens, API latency, tool calls, file operations,
 * errors, retries, and compaction/handoff markers for each turn.
 * Aggregated into session-level summaries for observability.
 */

#include "nevercode.h"

typedef struct {
    char name[NC_FMT_MAX];
    int count;
    long total_ms;
} turn_tool_stat_t;

typedef struct {
    int turn;
    long start_ts;
    long end_ts;
    int input_tokens;
    int output_tokens;
    long api_latency_ms;
    int tool_call_count;
    turn_tool_stat_t tool_stats[TURN_META_MAX_TOOLS];
    int num_tool_stats;
    int files_read;
    int files_written;
    int lines_added;
    int lines_removed;
    int errors;
    int retries;
    bool compacted;
    bool handed_off;
} turn_meta_entry_t;

static turn_meta_entry_t g_turns[TURN_META_MAX_ENTRIES];
static int g_turn_count = 0;
static int g_current_turn = -1;

void turn_meta_init(void) {
    g_turn_count = 0;
    g_current_turn = -1;
}

void turn_meta_free(void) {
    g_turn_count = 0;
    g_current_turn = -1;
}

int turn_meta_begin(int turn) {
    if (g_turn_count >= TURN_META_MAX_ENTRIES)
        return -1;

    turn_meta_entry_t *e = &g_turns[g_turn_count];
    memset(e, 0, sizeof(*e));
    e->turn = turn;
    e->start_ts = nc_wall_sec();
    g_current_turn = g_turn_count;
    g_turn_count++;
    return 0;
}

void turn_meta_end(void) {
    if (g_current_turn < 0 || g_current_turn >= g_turn_count)
        return;
    g_turns[g_current_turn].end_ts = nc_wall_sec();
    g_current_turn = -1;
}

static turn_meta_entry_t *current(void) {
    if (g_current_turn < 0 || g_current_turn >= g_turn_count)
        return NULL;
    return &g_turns[g_current_turn];
}

void turn_meta_set_tokens(int input, int output) {
    turn_meta_entry_t *e = current();
    if (e) {
        e->input_tokens = input;
        e->output_tokens = output;
    }
}

void turn_meta_set_latency(long api_ms) {
    turn_meta_entry_t *e = current();
    if (e)
        e->api_latency_ms = api_ms;
}

void turn_meta_record_tool(const char *name, long ms) {
    turn_meta_entry_t *e = current();
    if (!e)
        return;

    e->tool_call_count++;

    for (int i = 0; i < e->num_tool_stats; i++) {
        if (strcmp(e->tool_stats[i].name, name) == 0) {
            e->tool_stats[i].count++;
            e->tool_stats[i].total_ms += ms;
            return;
        }
    }

    if (e->num_tool_stats < TURN_META_MAX_TOOLS) {
        turn_tool_stat_t *s = &e->tool_stats[e->num_tool_stats++];
        nc_strlcpy(s->name, name, sizeof(s->name));
        s->count = 1;
        s->total_ms = ms;
    }
}

void turn_meta_record_file_read(void) {
    turn_meta_entry_t *e = current();
    if (e)
        e->files_read++;
}

void turn_meta_record_file_write(int lines_added, int lines_removed) {
    turn_meta_entry_t *e = current();
    if (!e)
        return;
    e->files_written++;
    e->lines_added += lines_added;
    e->lines_removed += lines_removed;
}

void turn_meta_record_error(void) {
    turn_meta_entry_t *e = current();
    if (e)
        e->errors++;
}

void turn_meta_record_retry(void) {
    turn_meta_entry_t *e = current();
    if (e)
        e->retries++;
}

void turn_meta_mark_compacted(void) {
    turn_meta_entry_t *e = current();
    if (e)
        e->compacted = true;
}

int turn_meta_count(void) {
    return g_turn_count;
}

int turn_meta_total_tools(void) {
    int total = 0;
    for (int i = 0; i < g_turn_count; i++)
        total += g_turns[i].tool_call_count;
    return total;
}

int turn_meta_total_errors(void) {
    int total = 0;
    for (int i = 0; i < g_turn_count; i++)
        total += g_turns[i].errors;
    return total;
}

long turn_meta_total_api_ms(void) {
    long total = 0;
    for (int i = 0; i < g_turn_count; i++)
        total += g_turns[i].api_latency_ms;
    return total;
}

char *turn_meta_format(int turn_idx) {
    if (turn_idx < 0 || turn_idx >= g_turn_count)
        return nc_xstrdup("Invalid turn index.\n");

    const turn_meta_entry_t *e = &g_turns[turn_idx];
    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_appendf(&sb, "Turn %d:\n", e->turn);
    strbuf_appendf(&sb, "  Tokens: %d in / %d out\n", e->input_tokens, e->output_tokens);
    strbuf_appendf(&sb, "  API Latency: %ldms\n", e->api_latency_ms);
    strbuf_appendf(&sb, "  Tools: %d calls", e->tool_call_count);

    if (e->num_tool_stats > 0) {
        strbuf_append_str(&sb, " (");
        for (int i = 0; i < e->num_tool_stats; i++) {
            if (i > 0)
                strbuf_append_str(&sb, ", ");
            strbuf_appendf(&sb, "%sx%d", e->tool_stats[i].name, e->tool_stats[i].count);
        }
        strbuf_append_str(&sb, ")");
    }
    strbuf_append_str(&sb, "\n");

    if (e->files_read > 0 || e->files_written > 0)
        strbuf_appendf(&sb, "  Files: %d read, %d written (+%d/-%d lines)\n", e->files_read,
                       e->files_written, e->lines_added, e->lines_removed);

    if (e->errors > 0)
        strbuf_appendf(&sb, "  Errors: %d (retries: %d)\n", e->errors, e->retries);
    if (e->compacted)
        strbuf_append_str(&sb, "  [compacted]\n");
    if (e->handed_off)
        strbuf_append_str(&sb, "  [handed off]\n");

    return strbuf_detach(&sb);
}

char *turn_meta_format_summary(void) {
    strbuf_t sb;
    strbuf_init(&sb);

    int ti = 0, to = 0, tt = 0, te = 0;
    long ta = 0;
    for (int i = 0; i < g_turn_count; i++) {
        ti += g_turns[i].input_tokens;
        to += g_turns[i].output_tokens;
        tt += g_turns[i].tool_call_count;
        te += g_turns[i].errors;
        ta += g_turns[i].api_latency_ms;
    }

    strbuf_appendf(&sb, "Session: %d turns, %d+%d tokens, %d tools, %d errors, %ldms API\n",
                   g_turn_count, ti, to, tt, te, ta);

    return strbuf_detach(&sb);
}
