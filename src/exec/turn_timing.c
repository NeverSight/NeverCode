#include "nevercode.h"

/* MAX_TURN_RECORDS from defaults.h */

typedef struct {
    int turn;
    long api_ms;
    long tool_ms;
    long ttft_ms;
    int tool_count;
    int input_tokens;
    int output_tokens;
} turn_record_t;

static turn_record_t g_records[MAX_TURN_RECORDS];
static int g_record_count = 0;
static long g_turn_start_ms;
static int g_timing_initialized = 0;

void turn_timing_init(void) {
    g_record_count = 0;
    g_timing_initialized = 1;
    g_turn_start_ms = nc_mono_ms();
}

void turn_timing_start(void) {
    g_turn_start_ms = nc_mono_ms();
}

long turn_timing_elapsed_ms(void) {
    return nc_mono_ms() - g_turn_start_ms;
}

void turn_timing_record(int turn, long api_ms, long tool_ms, int tool_count, int in_tok,
                        int out_tok) {
    turn_timing_record_ex(turn, api_ms, tool_ms, -1, tool_count, in_tok, out_tok);
}

void turn_timing_record_ex(int turn, long api_ms, long tool_ms, long ttft_ms, int tool_count,
                           int in_tok, int out_tok) {
    if (g_record_count >= MAX_TURN_RECORDS)
        return;
    turn_record_t *r = &g_records[g_record_count++];
    r->turn = turn;
    r->api_ms = api_ms;
    r->tool_ms = tool_ms;
    r->ttft_ms = ttft_ms;
    r->tool_count = tool_count;
    r->input_tokens = in_tok;
    r->output_tokens = out_tok;
}

int turn_timing_count(void) {
    return g_record_count;
}

char *turn_timing_format(void) {
    if (g_record_count == 0)
        return nc_xstrdup("No turn timing data recorded.");

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "Turn Timing Report:\n");
    strbuf_append_str(&sb,
                      "  Turn  │ TTFT(ms) │ API (ms) │ Tools (ms) │ Tools │ In tok │ Out tok\n");
    strbuf_append_str(&sb,
                      "  ──────┼──────────┼──────────┼────────────┼───────┼────────┼────────\n");

    long total_api = 0, total_tool = 0, total_ttft = 0;
    int total_tools = 0, total_in = 0, total_out = 0, ttft_count = 0;

    for (int i = 0; i < g_record_count; i++) {
        turn_record_t *r = &g_records[i];
        char ttft_str[NC_NUM_MAX];
        if (r->ttft_ms >= 0) {
            snprintf(ttft_str, sizeof(ttft_str), "%8ld", r->ttft_ms);
            total_ttft += r->ttft_ms;
            ttft_count++;
        } else {
            nc_strlcpy(ttft_str, "     n/a", sizeof(ttft_str));
        }
        char line[NC_LABEL_MAX];
        snprintf(line, sizeof(line), "  %5d │ %s │ %8ld │ %10ld │ %5d │ %6d │ %6d\n", r->turn,
                 ttft_str, r->api_ms, r->tool_ms, r->tool_count, r->input_tokens, r->output_tokens);
        strbuf_append_str(&sb, line);
        total_api += r->api_ms;
        total_tool += r->tool_ms;
        total_tools += r->tool_count;
        total_in += r->input_tokens;
        total_out += r->output_tokens;
    }

    strbuf_append_str(&sb,
                      "  ──────┼──────────┼──────────┼────────────┼───────┼────────┼────────\n");
    char ttft_total_str[NC_NUM_MAX];
    if (ttft_count > 0)
        snprintf(ttft_total_str, sizeof(ttft_total_str), "%8ld", total_ttft / ttft_count);
    else
        nc_strlcpy(ttft_total_str, "     n/a", sizeof(ttft_total_str));
    char summary[NC_LABEL_MAX];
    snprintf(summary, sizeof(summary), "  Total │ %s │ %8ld │ %10ld │ %5d │ %6d │ %6d\n",
             ttft_total_str, total_api, total_tool, total_tools, total_in, total_out);
    strbuf_append_str(&sb, summary);

    if (g_record_count > 0) {
        long avg_api = total_api / g_record_count;
        long avg_total = (total_api + total_tool) / g_record_count;
        long avg_ttft = ttft_count > 0 ? total_ttft / ttft_count : -1;
        strbuf_appendf(&sb, "\n  Avg API latency: %ldms, Avg turn time: %ldms (%d turns)", avg_api,
                       avg_total, g_record_count);
        if (avg_ttft >= 0)
            strbuf_appendf(&sb, ", Avg TTFT: %ldms", avg_ttft);
        strbuf_append_str(&sb, "\n");
    }

    return strbuf_detach(&sb);
}

long turn_timing_total_api_ms(void) {
    long t = 0;
    for (int i = 0; i < g_record_count; i++)
        t += g_records[i].api_ms;
    return t;
}

long turn_timing_total_tool_ms(void) {
    long t = 0;
    for (int i = 0; i < g_record_count; i++)
        t += g_records[i].tool_ms;
    return t;
}
