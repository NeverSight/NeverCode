#include "nevercode.h"
#include "model_registry.h"

#define MAX_MODELS MAX_COST_MODELS

typedef struct {
    char model[NC_TAG_MAX];
    int input_tokens;
    int output_tokens;
    int cache_read_tokens;
    int cache_write_tokens;
    int reasoning_tokens;
    double cost_usd;
} model_usage_t;

typedef struct {
    char name[TOOL_STATS_NAME_MAX];
    int call_count;
    long total_ms;
    int fail_count;
} tool_stat_t;

typedef struct {
    model_usage_t models[MAX_MODELS];
    int num_models;
    int total_input;
    int total_output;
    int total_cache_read;
    int total_cache_write;
    int total_reasoning;
    double total_cost;
    long start_time;
    long total_api_ms;
    int total_lines_added;
    int total_lines_removed;
    int total_requests;
    int total_retries;
    tool_stat_t tool_stats[TOOL_STATS_MAX];
    int num_tool_stats;
    int total_tool_calls;
    int total_tool_fails;
} cost_state_t;

static cost_state_t g_cost = {0};

void cost_tracker_init(void) {
    memset(&g_cost, 0, sizeof(g_cost));
    g_cost.start_time = nc_time_now();
}

static double model_input_price(const char *model) {
    return model_registry_input_price(model);
}

static double model_output_price(const char *model) {
    return model_registry_output_price(model);
}

static model_usage_t *find_or_create_model(const char *model) {
    for (int i = 0; i < g_cost.num_models; i++) {
        if (strcmp(g_cost.models[i].model, model) == 0)
            return &g_cost.models[i];
    }
    if (g_cost.num_models >= MAX_MODELS)
        return NULL;

    model_usage_t *m = &g_cost.models[g_cost.num_models++];
    memset(m, 0, sizeof(*m));
    nc_strlcpy(m->model, model, sizeof(m->model));
    return m;
}

void cost_tracker_add(const char *model, int in_tok, int out_tok, long api_ms) {
    cost_tracker_add_ext(model, in_tok, out_tok, 0, 0, 0, api_ms);
}

void cost_tracker_add_ext(const char *model, int in_tok, int out_tok, int cache_read,
                          int cache_write, int reasoning, long api_ms) {
    double in_price = model_input_price(model);
    double out_price = model_output_price(model);
    double cr_price = model_registry_cache_read_price(model);
    double cw_price = model_registry_cache_write_price(model);
    double cost = ((double)in_tok * in_price + (double)out_tok * out_price +
                   (double)cache_read * cr_price + (double)cache_write * cw_price) /
                  1000000.0;

    model_usage_t *m = find_or_create_model(model ? model : "unknown");
    if (m) {
        m->input_tokens += in_tok;
        m->output_tokens += out_tok;
        m->cache_read_tokens += cache_read;
        m->cache_write_tokens += cache_write;
        m->reasoning_tokens += reasoning;
        m->cost_usd += cost;
    }

    g_cost.total_input += in_tok;
    g_cost.total_output += out_tok;
    g_cost.total_cache_read += cache_read;
    g_cost.total_cache_write += cache_write;
    g_cost.total_reasoning += reasoning;
    g_cost.total_cost += cost;
    g_cost.total_api_ms += api_ms;
    g_cost.total_requests++;
}

void cost_tracker_add_retry(void) {
    g_cost.total_retries++;
}

void cost_tracker_add_lines(int added, int removed) {
    g_cost.total_lines_added += added;
    g_cost.total_lines_removed += removed;
}

int cost_tracker_total_input(void) {
    return g_cost.total_input;
}
int cost_tracker_total_output(void) {
    return g_cost.total_output;
}
int cost_tracker_total_cache_read(void) {
    return g_cost.total_cache_read;
}
int cost_tracker_total_cache_write(void) {
    return g_cost.total_cache_write;
}
int cost_tracker_total_reasoning(void) {
    return g_cost.total_reasoning;
}
double cost_tracker_total_cost(void) {
    return g_cost.total_cost;
}
int cost_tracker_total_requests(void) {
    return g_cost.total_requests;
}
int cost_tracker_total_retries(void) {
    return g_cost.total_retries;
}

static tool_stat_t *find_or_create_tool(const char *name) {
    if (!name)
        return NULL;
    for (int i = 0; i < g_cost.num_tool_stats; i++)
        if (strcmp(g_cost.tool_stats[i].name, name) == 0)
            return &g_cost.tool_stats[i];
    if (g_cost.num_tool_stats >= TOOL_STATS_MAX)
        return NULL;
    tool_stat_t *t = &g_cost.tool_stats[g_cost.num_tool_stats++];
    memset(t, 0, sizeof(*t));
    nc_strlcpy(t->name, name, sizeof(t->name));
    return t;
}

void cost_tracker_add_tool(const char *tool_name, long exec_ms, bool failed) {
    g_cost.total_tool_calls++;
    if (failed)
        g_cost.total_tool_fails++;
    tool_stat_t *t = find_or_create_tool(tool_name);
    if (!t)
        return;
    t->call_count++;
    t->total_ms += exec_ms;
    if (failed)
        t->fail_count++;
}

int cost_tracker_tool_count(void) {
    return g_cost.num_tool_stats;
}
int cost_tracker_total_tool_calls(void) {
    return g_cost.total_tool_calls;
}
int cost_tracker_total_tool_fails(void) {
    return g_cost.total_tool_fails;
}

static void format_cost_str(double cost, char *buf, size_t sz) {
    if (cost > COST_HIGH_THRESHOLD)
        snprintf(buf, sz, "$%.2f", cost);
    else
        snprintf(buf, sz, "$%.4f", cost);
}

char *cost_tracker_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);

    char cost_str[NC_FMT_MAX];
    format_cost_str(g_cost.total_cost, cost_str, sizeof(cost_str));

    long wall_ms = (nc_time_now() - g_cost.start_time) * 1000;
    char api_dur[NC_FMT_MAX], wall_dur[NC_FMT_MAX];
    nc_fmt_duration_ms(api_dur, sizeof(api_dur), g_cost.total_api_ms);
    nc_fmt_duration_ms(wall_dur, sizeof(wall_dur), wall_ms);

    char line[NC_MSG_MAX];
    snprintf(line, sizeof(line), "%-*s %s\n", COST_FORMAT_LABEL_WIDTH, "Total cost:", cost_str);
    strbuf_append_str(&sb, line);
    snprintf(line, sizeof(line), "%-*s %s\n", COST_FORMAT_LABEL_WIDTH,
             "Total duration (API):", api_dur);
    strbuf_append_str(&sb, line);
    snprintf(line, sizeof(line), "%-*s %s\n", COST_FORMAT_LABEL_WIDTH,
             "Total duration (wall):", wall_dur);
    strbuf_append_str(&sb, line);
    snprintf(line, sizeof(line), "%-*s %d line%s added, %d line%s removed\n",
             COST_FORMAT_LABEL_WIDTH, "Total code changes:", g_cost.total_lines_added,
             g_cost.total_lines_added == 1 ? "" : "s", g_cost.total_lines_removed,
             g_cost.total_lines_removed == 1 ? "" : "s");
    strbuf_append_str(&sb, line);

    if (g_cost.total_cache_read > 0 || g_cost.total_cache_write > 0) {
        char cr_s[NC_FMT_MAX], cw_s[NC_FMT_MAX];
        nc_fmt_tokens(cr_s, sizeof(cr_s), g_cost.total_cache_read);
        nc_fmt_tokens(cw_s, sizeof(cw_s), g_cost.total_cache_write);
        snprintf(line, sizeof(line), "%-*s %s read, %s write\n", COST_FORMAT_LABEL_WIDTH,
                 "Cache tokens:", cr_s, cw_s);
        strbuf_append_str(&sb, line);
    }
    if (g_cost.total_reasoning > 0) {
        char rs[NC_FMT_MAX];
        nc_fmt_tokens(rs, sizeof(rs), g_cost.total_reasoning);
        snprintf(line, sizeof(line), "%-*s %s\n", COST_FORMAT_LABEL_WIDTH, "Reasoning tokens:", rs);
        strbuf_append_str(&sb, line);
    }
    if (g_cost.total_requests > 0) {
        snprintf(line, sizeof(line), "%-*s %d (%d retries)\n", COST_FORMAT_LABEL_WIDTH,
                 "API requests:", g_cost.total_requests, g_cost.total_retries);
        strbuf_append_str(&sb, line);
    }

    if (g_cost.num_models > 0) {
        strbuf_append_str(&sb, "\nUsage by model:\n");
        for (int i = 0; i < g_cost.num_models; i++) {
            model_usage_t *m = &g_cost.models[i];
            char in_s[NC_FMT_MAX], out_s[NC_FMT_MAX], mcost[NC_FMT_MAX];
            nc_fmt_tokens(in_s, sizeof(in_s), m->input_tokens);
            nc_fmt_tokens(out_s, sizeof(out_s), m->output_tokens);
            format_cost_str(m->cost_usd, mcost, sizeof(mcost));

            snprintf(line, sizeof(line), "  %-*s %*s in  %*s out  %s\n", COST_FORMAT_MODEL_WIDTH,
                     m->model, COST_FORMAT_TOKEN_WIDTH, in_s, COST_FORMAT_TOKEN_WIDTH, out_s,
                     mcost);
            strbuf_append_str(&sb, line);

            if (m->cache_read_tokens > 0 || m->cache_write_tokens > 0 || m->reasoning_tokens > 0) {
                char cr[NC_FMT_MAX], cw[NC_FMT_MAX], rz[NC_FMT_MAX];
                nc_fmt_tokens(cr, sizeof(cr), m->cache_read_tokens);
                nc_fmt_tokens(cw, sizeof(cw), m->cache_write_tokens);
                nc_fmt_tokens(rz, sizeof(rz), m->reasoning_tokens);
                snprintf(line, sizeof(line), "  %-*s cache %s/%s  reason %s\n",
                         COST_FORMAT_MODEL_WIDTH, "", cr, cw, rz);
                strbuf_append_str(&sb, line);
            }
        }
    }

    if (g_cost.num_tool_stats > 0) {
        strbuf_append_str(&sb, "\nTool usage:\n");
        for (int i = 0; i < g_cost.num_tool_stats; i++) {
            tool_stat_t *t = &g_cost.tool_stats[i];
            char dur[NC_FMT_MAX];
            nc_fmt_duration_ms(dur, sizeof(dur), t->total_ms);
            snprintf(line, sizeof(line), "  %-*s %4d call%s  %s", COST_FORMAT_MODEL_WIDTH, t->name,
                     t->call_count, t->call_count == 1 ? " " : "s", dur);
            strbuf_append_str(&sb, line);
            if (t->fail_count > 0) {
                char fail_s[NC_FMT_MAX];
                snprintf(fail_s, sizeof(fail_s), "  (%d fail%s)", t->fail_count,
                         t->fail_count == 1 ? "" : "s");
                strbuf_append_str(&sb, fail_s);
            }
            strbuf_append_str(&sb, "\n");
        }
        snprintf(line, sizeof(line), "%-*s %d (%d failed)\n", COST_FORMAT_LABEL_WIDTH,
                 "Total tool calls:", g_cost.total_tool_calls, g_cost.total_tool_fails);
        strbuf_append_str(&sb, line);
    }

    return strbuf_detach(&sb);
}
