/*
 * session_stats.c — Session statistics persistence.
 * Tracks cumulative stats across sessions: total tokens consumed,
 * total cost, session count, total time, and per-model breakdowns.
 * Persists to ~/.agents/stats.json (or other config directories).
 */

#include "nevercode.h"

/* SESSION_STATS_FILENAME in defaults.h */
#define STATS_FILE SESSION_STATS_FILENAME
/* MAX_MODEL_STATS from defaults.h */

typedef struct {
    char *model;
    long input_tokens;
    long output_tokens;
    double cost;
    int sessions;
    long api_calls;
} model_stats_t;

typedef struct {
    int total_sessions;
    long total_input_tokens;
    long total_output_tokens;
    double total_cost;
    long total_duration_s;
    long total_api_calls;
    int total_tool_uses;
    int total_lines_added;
    int total_lines_removed;
    time_t first_session;
    time_t last_session;
    model_stats_t models[MAX_MODEL_STATS];
    int num_models;
} session_stats_t;

static session_stats_t g_stats = {0};
static bool g_stats_loaded = false;

void session_stats_init(void) {
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats_loaded = true;
}

void session_stats_free(void) {
    for (int i = 0; i < g_stats.num_models; i++) {
        free(g_stats.models[i].model);
        g_stats.models[i].model = NULL;
    }
    g_stats.num_models = 0;
    g_stats_loaded = false;
}

static void stats_path(char *out, size_t sz) {
    config_home_path(out, sz, STATS_FILE);
}

int session_stats_load(void) {
    char path[NC_PATH_MAX];
    stats_path(path, sizeof(path));

    char *buf = strbuf_read_file(path, NULL);
    if (!buf) {
        g_stats_loaded = true;
        return 0;
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        g_stats_loaded = true;
        return -1;
    }

    g_stats.total_sessions = nc_cjson_int(root, "total_sessions", 0);
    g_stats.total_input_tokens = (long)nc_cjson_dbl(root, "total_input_tokens", 0);
    g_stats.total_output_tokens = (long)nc_cjson_dbl(root, "total_output_tokens", 0);
    g_stats.total_cost = nc_cjson_dbl(root, "total_cost", 0);
    g_stats.total_duration_s = (long)nc_cjson_dbl(root, "total_duration_s", 0);
    g_stats.total_api_calls = (long)nc_cjson_dbl(root, "total_api_calls", 0);
    g_stats.total_tool_uses = nc_cjson_int(root, "total_tool_uses", 0);
    g_stats.total_lines_added = nc_cjson_int(root, "total_lines_added", 0);
    g_stats.total_lines_removed = nc_cjson_int(root, "total_lines_removed", 0);
    g_stats.first_session = (time_t)nc_cjson_dbl(root, "first_session", 0);
    g_stats.last_session = (time_t)nc_cjson_dbl(root, "last_session", 0);

    cJSON *models = cJSON_GetObjectItem(root, "models");
    if (models) {
        int nm = cJSON_GetArraySize(models);
        for (int i = 0; i < nm && g_stats.num_models < MAX_MODEL_STATS; i++) {
            cJSON *m = cJSON_GetArrayItem(models, i);
            model_stats_t *ms = &g_stats.models[g_stats.num_models];
            const char *name = nc_cjson_str(m, "model", NULL);
            if (!name)
                continue;
            ms->model = nc_xstrdup(name);
            ms->input_tokens = (long)nc_cjson_dbl(m, "input_tokens", 0);
            ms->output_tokens = (long)nc_cjson_dbl(m, "output_tokens", 0);
            ms->cost = nc_cjson_dbl(m, "cost", 0);
            ms->sessions = nc_cjson_int(m, "sessions", 0);
            ms->api_calls = (long)nc_cjson_dbl(m, "api_calls", 0);
            g_stats.num_models++;
        }
    }

    cJSON_Delete(root);
    g_stats_loaded = true;
    return 0;
}

int session_stats_save(void) {
    char path[NC_PATH_MAX];
    stats_path(path, sizeof(path));

    char dir[NC_PATH_MAX];
    nc_strlcpy(dir, config_home_dir(), sizeof(dir));
    mkdir(dir, NC_DIR_MODE_PRIVATE);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "total_sessions", g_stats.total_sessions);
    cJSON_AddNumberToObject(root, "total_input_tokens", (double)g_stats.total_input_tokens);
    cJSON_AddNumberToObject(root, "total_output_tokens", (double)g_stats.total_output_tokens);
    cJSON_AddNumberToObject(root, "total_cost", g_stats.total_cost);
    cJSON_AddNumberToObject(root, "total_duration_s", (double)g_stats.total_duration_s);
    cJSON_AddNumberToObject(root, "total_api_calls", (double)g_stats.total_api_calls);
    cJSON_AddNumberToObject(root, "total_tool_uses", g_stats.total_tool_uses);
    cJSON_AddNumberToObject(root, "total_lines_added", g_stats.total_lines_added);
    cJSON_AddNumberToObject(root, "total_lines_removed", g_stats.total_lines_removed);
    cJSON_AddNumberToObject(root, "first_session", (double)g_stats.first_session);
    cJSON_AddNumberToObject(root, "last_session", (double)g_stats.last_session);

    if (g_stats.num_models > 0) {
        cJSON *models = cJSON_AddArrayToObject(root, "models");
        for (int i = 0; i < g_stats.num_models; i++) {
            model_stats_t *ms = &g_stats.models[i];
            cJSON *m = cJSON_CreateObject();
            cJSON_AddStringToObject(m, "model", ms->model);
            cJSON_AddNumberToObject(m, "input_tokens", (double)ms->input_tokens);
            cJSON_AddNumberToObject(m, "output_tokens", (double)ms->output_tokens);
            cJSON_AddNumberToObject(m, "cost", ms->cost);
            cJSON_AddNumberToObject(m, "sessions", ms->sessions);
            cJSON_AddNumberToObject(m, "api_calls", (double)ms->api_calls);
            cJSON_AddItemToArray(models, m);
        }
    }

    char *json = nc_cjson_detach_pretty(root);
    if (!json)
        return -1;

    int rc = nc_write_file_atomic(path, json, strlen(json));
    free(json);
    return rc;
}

static model_stats_t *find_or_create_model(const char *model) {
    for (int i = 0; i < g_stats.num_models; i++) {
        if (strcmp(g_stats.models[i].model, model) == 0)
            return &g_stats.models[i];
    }
    if (g_stats.num_models >= MAX_MODEL_STATS)
        return NULL;
    model_stats_t *ms = &g_stats.models[g_stats.num_models++];
    memset(ms, 0, sizeof(*ms));
    ms->model = nc_xstrdup(model);
    return ms;
}

void session_stats_record_session(const char *model, long input_tokens, long output_tokens,
                                  double cost, long duration_s, long api_calls, int tool_uses,
                                  int lines_added, int lines_removed) {
    if (!g_stats_loaded)
        session_stats_load();

    g_stats.total_sessions++;
    g_stats.total_input_tokens += input_tokens;
    g_stats.total_output_tokens += output_tokens;
    g_stats.total_cost += cost;
    g_stats.total_duration_s += duration_s;
    g_stats.total_api_calls += api_calls;
    g_stats.total_tool_uses += tool_uses;
    g_stats.total_lines_added += lines_added;
    g_stats.total_lines_removed += lines_removed;

    time_t now = nc_time_now();
    if (g_stats.first_session == 0)
        g_stats.first_session = now;
    g_stats.last_session = now;

    if (model) {
        model_stats_t *ms = find_or_create_model(model);
        if (ms) {
            ms->input_tokens += input_tokens;
            ms->output_tokens += output_tokens;
            ms->cost += cost;
            ms->sessions++;
            ms->api_calls += api_calls;
        }
    }
}

int session_stats_total_sessions(void) {
    if (!g_stats_loaded)
        session_stats_load();
    return g_stats.total_sessions;
}

long session_stats_total_tokens(void) {
    if (!g_stats_loaded)
        session_stats_load();
    return g_stats.total_input_tokens + g_stats.total_output_tokens;
}

double session_stats_total_cost(void) {
    if (!g_stats_loaded)
        session_stats_load();
    return g_stats.total_cost;
}

long session_stats_total_duration(void) {
    if (!g_stats_loaded)
        session_stats_load();
    return g_stats.total_duration_s;
}

int session_stats_model_count(void) {
    if (!g_stats_loaded)
        session_stats_load();
    return g_stats.num_models;
}

char *session_stats_format(void) {
    if (!g_stats_loaded)
        session_stats_load();

    strbuf_t sb;
    strbuf_init(&sb);

    char line[NC_MSG_MAX];
    snprintf(line, sizeof(line), "Cumulative Statistics (%d sessions):\n", g_stats.total_sessions);
    strbuf_append_str(&sb, line);

    snprintf(line, sizeof(line), "  Tokens:     %ld in + %ld out = %ld total\n",
             g_stats.total_input_tokens, g_stats.total_output_tokens,
             g_stats.total_input_tokens + g_stats.total_output_tokens);
    strbuf_append_str(&sb, line);

    snprintf(line, sizeof(line), "  Cost:       $%.4f\n", g_stats.total_cost);
    strbuf_append_str(&sb, line);

    {
        char dur_s[NC_FMT_MAX];
        nc_fmt_elapsed_s(dur_s, sizeof(dur_s), (int)g_stats.total_duration_s);
        snprintf(line, sizeof(line), "  Time:       %s\n", dur_s);
        strbuf_append_str(&sb, line);
    }

    snprintf(line, sizeof(line), "  API Calls:  %ld\n", g_stats.total_api_calls);
    strbuf_append_str(&sb, line);

    snprintf(line, sizeof(line), "  Tool Uses:  %d\n", g_stats.total_tool_uses);
    strbuf_append_str(&sb, line);

    snprintf(line, sizeof(line), "  Lines:      +%d / -%d\n", g_stats.total_lines_added,
             g_stats.total_lines_removed);
    strbuf_append_str(&sb, line);

    if (g_stats.num_models > 0) {
        strbuf_append_str(&sb, "\n  Per-model breakdown:\n");
        for (int i = 0; i < g_stats.num_models; i++) {
            model_stats_t *ms = &g_stats.models[i];
            snprintf(line, sizeof(line), "    %s: %ld in + %ld out, $%.4f (%d sessions)\n",
                     ms->model, ms->input_tokens, ms->output_tokens, ms->cost, ms->sessions);
            strbuf_append_str(&sb, line);
        }
    }

    if (g_stats.first_session > 0) {
        struct tm *tm = localtime(&g_stats.first_session);
        char date[NC_FMT_MAX];
        strftime(date, sizeof(date), "%Y-%m-%d", tm);
        snprintf(line, sizeof(line), "\n  First session: %s\n", date);
        strbuf_append_str(&sb, line);
    }

    return strbuf_detach(&sb);
}

char *session_stats_format_brief(void) {
    if (!g_stats_loaded)
        session_stats_load();

    char buf[NC_LABEL_MAX];
    snprintf(buf, sizeof(buf), "%d sessions, %ld tokens, $%.2f, %ldh", g_stats.total_sessions,
             g_stats.total_input_tokens + g_stats.total_output_tokens, g_stats.total_cost,
             g_stats.total_duration_s / 3600);
    return nc_xstrdup(buf);
}
