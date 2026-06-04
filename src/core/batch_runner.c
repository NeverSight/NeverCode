/*
 * batch_runner.c — Multi-prompt batch processing.
 *
 * Reads prompts from a JSONL file and processes them sequentially,
 * writing results to BATCH_OUTPUT_DIR. Supports checkpointing so
 * interrupted runs can resume from the last completed prompt.
 */

#include "nevercode.h"
#include "defaults.h"

typedef struct {
    char *prompt;
    char *id;
    int index;
} batch_prompt_t;

typedef struct {
    batch_prompt_t *prompts;
    int count;
    int capacity;
    char *run_id;
    char *output_dir;
    int completed;
    int failed;
    double total_cost;
    time_t start_time;
} batch_state_t;

static batch_state_t g_batch;

static void batch_sanitize_id(const char *src, char *dst, size_t dst_cap) {
    if (!dst || dst_cap == 0)
        return;
    size_t out = 0;
    if (!src)
        src = "";
    for (size_t i = 0; src[i] && out + 1 < dst_cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == '-' || c == '_') {
            dst[out++] = (char)c;
        } else {
            dst[out++] = '_';
        }
    }
    dst[out] = '\0';
}

static void batch_build_result_path(int idx, char *out, size_t out_cap) {
    if (!out || out_cap == 0 || !g_batch.output_dir)
        return;
    const char *id = g_batch.prompts[idx].id;
    if (id && *id) {
        char safe_id[BATCH_ID_SAFE_MAX];
        batch_sanitize_id(id, safe_id, sizeof(safe_id));
        if (safe_id[0] != '\0') {
            snprintf(out, out_cap, "%s/%s" BATCH_RESULT_FILE_EXT, g_batch.output_dir, safe_id);
            return;
        }
    }
    snprintf(out, out_cap, "%s/%04d" BATCH_RESULT_FILE_EXT, g_batch.output_dir, idx);
}

static char *generate_run_id(void) {
    char buf[NC_FMT_MAX];
    time_t now = nc_time_now();
    struct tm tm;
    localtime_r(&now, &tm);
    snprintf(buf, sizeof(buf), BATCH_RUN_ID_PREFIX "_%04d%02d%02d_%02d%02d%02d", tm.tm_year + 1900,
             tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return nc_xstrdup(buf);
}

int batch_load_prompts(const char *path) {
    memset(&g_batch, 0, sizeof(g_batch));
    g_batch.run_id = generate_run_id();
    g_batch.start_time = nc_time_now();

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "batch: cannot open %s: %s\n", path, strerror(errno));
        free(g_batch.run_id);
        memset(&g_batch, 0, sizeof(g_batch));
        return -1;
    }

    g_batch.capacity = BATCH_INIT_CAPACITY;
    g_batch.prompts = calloc(g_batch.capacity, sizeof(batch_prompt_t));
    if (!g_batch.prompts) {
        fclose(f);
        free(g_batch.run_id);
        memset(&g_batch, 0, sizeof(g_batch));
        return -1;
    }

    char line[BATCH_PROMPT_MAX_BYTES];
    int idx = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0)
            continue;

        if (g_batch.count >= g_batch.capacity) {
            g_batch.capacity *= 2;
            if (g_batch.capacity > BATCH_MAX_PROMPTS)
                g_batch.capacity = BATCH_MAX_PROMPTS;
            batch_prompt_t *tmp =
                realloc(g_batch.prompts, g_batch.capacity * sizeof(batch_prompt_t));
            if (!tmp)
                break;
            g_batch.prompts = tmp;
        }
        if (g_batch.count >= BATCH_MAX_PROMPTS) {
            fprintf(stderr, "batch: max %d prompts reached, truncating\n", BATCH_MAX_PROMPTS);
            break;
        }

        cJSON *j = cJSON_Parse(line);
        if (j) {
            cJSON *jp = cJSON_GetObjectItem(j, "prompt");
            cJSON *jid = cJSON_GetObjectItem(j, "id");
            if (jp && cJSON_IsString(jp)) {
                batch_prompt_t *bp = &g_batch.prompts[g_batch.count];
                bp->prompt = nc_xstrdup(jp->valuestring);
                bp->id = (jid && cJSON_IsString(jid)) ? nc_xstrdup(jid->valuestring) : NULL;
                bp->index = idx;
                g_batch.count++;
            }
            cJSON_Delete(j);
        } else {
            batch_prompt_t *bp = &g_batch.prompts[g_batch.count];
            bp->prompt = nc_xstrdup(line);
            bp->id = NULL;
            bp->index = idx;
            g_batch.count++;
        }
        idx++;
    }
    fclose(f);

    char dir[NC_PATH_MAX];
    config_home_path(dir, sizeof(dir), "/" BATCH_OUTPUT_DIR);
    nc_ensure_dir(dir);

    char run_dir[NC_PATH_MAX];
    snprintf(run_dir, sizeof(run_dir), "%s/%s", dir, g_batch.run_id);
    nc_ensure_dir(run_dir);
    g_batch.output_dir = nc_xstrdup(run_dir);

    return g_batch.count;
}

int batch_count(void) {
    return g_batch.count;
}

int batch_completed(void) {
    return g_batch.completed;
}

int batch_failed(void) {
    return g_batch.failed;
}

const char *batch_prompt_at(int idx) {
    return (idx >= 0 && idx < g_batch.count) ? g_batch.prompts[idx].prompt : NULL;
}

const char *batch_id_at(int idx) {
    return (idx >= 0 && idx < g_batch.count) ? g_batch.prompts[idx].id : NULL;
}

void batch_record_result(int idx, const char *result, bool success) {
    if (idx < 0 || idx >= g_batch.count || !g_batch.output_dir)
        return;

    char path[NC_PATH_MAX];
    batch_build_result_path(idx, path, sizeof(path));
    const char *id = g_batch.prompts[idx].id;

    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "index", idx);
    if (id)
        cJSON_AddStringToObject(j, "id", id);
    cJSON_AddStringToObject(j, "prompt", g_batch.prompts[idx].prompt);
    cJSON_AddStringToObject(j, "result", result ? result : "");
    cJSON_AddBoolToObject(j, "success", success);
    cJSON_AddNumberToObject(j, "timestamp", (double)nc_time_now());

    char *s = nc_cjson_detach_pretty(j);
    if (s) {
        (void)nc_write_file_atomic(path, s, strlen(s));
        free(s);
    }

    if (success)
        g_batch.completed++;
    else
        g_batch.failed++;
}

static int checkpoint_idx(void) {
    if (!g_batch.output_dir)
        return 0;
    char cp[NC_PATH_MAX];
    snprintf(cp, sizeof(cp), "%s/%s", g_batch.output_dir, BATCH_CHECKPOINT_FILENAME);
    FILE *f = fopen(cp, "r");
    if (!f)
        return 0;
    int v = 0;
    if (fscanf(f, "%d", &v) != 1)
        v = 0;
    fclose(f);
    if (v < 0)
        return 0;
    if (g_batch.count > 0 && v > g_batch.count)
        return g_batch.count;
    return v;
}

void batch_save_checkpoint(int idx) {
    if (!g_batch.output_dir)
        return;
    if (idx < 0)
        idx = 0;
    if (g_batch.count > 0 && idx > g_batch.count)
        idx = g_batch.count;
    char cp[NC_PATH_MAX];
    char body[NC_NUM_MAX + 2];
    snprintf(cp, sizeof(cp), "%s/%s", g_batch.output_dir, BATCH_CHECKPOINT_FILENAME);
    int n = snprintf(body, sizeof(body), "%d\n", idx);
    if (n > 0)
        (void)nc_write_file_atomic(cp, body, (size_t)n);
}

int batch_resume_index(void) {
    return checkpoint_idx();
}

char *batch_format_summary(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    double elapsed = difftime(nc_time_now(), g_batch.start_time);
    strbuf_appendf(&sb, "Batch run: %s\n", g_batch.run_id ? g_batch.run_id : "(none)");
    strbuf_appendf(&sb, "  Total: %d | Done: %d | Failed: %d\n", g_batch.count, g_batch.completed,
                   g_batch.failed);
    strbuf_appendf(&sb, "  Elapsed: %.0fs", elapsed);
    if (g_batch.completed > 0)
        strbuf_appendf(&sb, " | Avg: %.1fs/prompt", elapsed / g_batch.completed);
    strbuf_append_str(&sb, "\n");
    if (g_batch.output_dir)
        strbuf_appendf(&sb, "  Output: %s\n", g_batch.output_dir);
    return strbuf_detach(&sb);
}

void batch_free(void) {
    for (int i = 0; i < g_batch.count; i++) {
        free(g_batch.prompts[i].prompt);
        free(g_batch.prompts[i].id);
    }
    free(g_batch.prompts);
    free(g_batch.run_id);
    free(g_batch.output_dir);
    memset(&g_batch, 0, sizeof(g_batch));
}
