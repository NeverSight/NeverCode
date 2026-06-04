/*
 * trajectory_compress.c — Post-process completed session trajectories
 *
 * Compresses trajectory JSONL files by protecting first/last turns
 * and replacing middle tool-heavy regions with summaries.
 *
 * Two modes:
 *   1) Full-middle — compress entire non-protected middle (TRAJ_TARGET_MAX_TOKENS == 0).
 *   2) Budget-aware — only compress enough turns to bring total under a
 *      configurable token budget (greedy, from start of compressible region).
 */

#include "nevercode.h"

typedef struct {
    char *content;
    char role[NC_TAG_MAX];
    int index;
    bool protected;
    bool compressed;
    int token_est;
} traj_entry_t;

typedef struct {
    traj_entry_t entries[TRAJ_MAX_ENTRIES];
    int count;
    int total_tokens;
    int compressed_count;
    int target_tokens;
} traj_state_t;

static bool is_tool_role(const char *role) {
    return role && (strcmp(role, "tool") == 0 || strcmp(role, "function") == 0);
}

static void mark_protected(traj_state_t *st) {
    int first_count = 0;
    for (int i = 0; i < st->count && first_count < TRAJ_PROTECT_FIRST_N; i++) {
        st->entries[i].protected = true;
        first_count++;
    }

    int last_start = st->count - TRAJ_PROTECT_LAST_N;
    if (last_start < 0)
        last_start = 0;
    for (int i = last_start; i < st->count; i++)
        st->entries[i].protected = true;
}

static char *build_region_summary(const traj_entry_t *entries, int start, int end) {
    strbuf_t sb;
    strbuf_init(&sb);

    int tool_calls = 0;
    int assistant_msgs = 0;
    int user_msgs = 0;
    int total_tokens = 0;

    for (int i = start; i < end; i++) {
        if (is_tool_role(entries[i].role))
            tool_calls++;
        else if (strcmp(entries[i].role, "assistant") == 0)
            assistant_msgs++;
        else if (strcmp(entries[i].role, "user") == 0)
            user_msgs++;
        total_tokens += entries[i].token_est;
    }

    strbuf_appendf(&sb,
                   "[Compressed region: %d entries, ~%d tokens "
                   "(%d tool, %d assistant, %d user)]",
                   end - start, total_tokens, tool_calls, assistant_msgs, user_msgs);

    return strbuf_detach(&sb);
}

static void compress_region(traj_state_t *st, int start, int end) {
    if (end - start < 2)
        return;
    char *summary = build_region_summary(st->entries, start, end);
    for (int j = start; j < end; j++) {
        if (j == start) {
            free(st->entries[j].content);
            st->entries[j].content = summary;
            st->entries[j].compressed = true;
            st->entries[j].token_est = TRAJ_SUMMARY_MAX_TOKENS;
            nc_strlcpy(st->entries[j].role, "user", sizeof(st->entries[j].role));
        } else {
            free(st->entries[j].content);
            st->entries[j].content = NULL;
            st->entries[j].compressed = true;
            st->entries[j].token_est = 0;
        }
        st->compressed_count++;
    }
}

int traj_compress_load(traj_state_t *st, const char *jsonl_path) {
    if (!st || !jsonl_path)
        return -1;
    memset(st, 0, sizeof(*st));
    st->target_tokens = TRAJ_TARGET_MAX_TOKENS;

    FILE *f = fopen(jsonl_path, "r");
    if (!f)
        return -1;

    char line[NC_CMD_MAX];
    while (fgets(line, sizeof(line), f) && st->count < TRAJ_MAX_ENTRIES) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0)
            continue;

        cJSON *obj = cJSON_Parse(line);
        if (!obj)
            continue;

        traj_entry_t *e = &st->entries[st->count];
        memset(e, 0, sizeof(*e));
        e->content = nc_xstrdup(line);
        e->index = st->count;

        const cJSON *role = cJSON_GetObjectItem(obj, "role");
        if (role && role->valuestring)
            nc_strlcpy(e->role, role->valuestring, sizeof(e->role));

        e->token_est = estimate_tokens(line);
        st->total_tokens += e->token_est;

        st->count++;
        cJSON_Delete(obj);
    }
    fclose(f);
    return st->count;
}

static int compress_budget_aware(traj_state_t *st) {
    int target = st->target_tokens;
    if (st->total_tokens <= target)
        return 0;

    mark_protected(st);

    int compress_start = -1;
    int compress_end = -1;
    for (int i = 0; i < st->count; i++) {
        if (!st->entries[i].protected) {
            if (compress_start < 0)
                compress_start = i;
            compress_end = i + 1;
        }
    }
    if (compress_start < 0 || compress_end <= compress_start)
        return 0;

    int tokens_to_save = st->total_tokens - target;
    int required = tokens_to_save + TRAJ_SUMMARY_MAX_TOKENS;
    int accumulated = 0;
    int greedy_end = compress_start;

    for (int i = compress_start; i < compress_end; i++) {
        if (st->entries[i].protected)
            continue;
        accumulated += st->entries[i].token_est;
        greedy_end = i + 1;
        if (accumulated >= required)
            break;
    }

    if (greedy_end <= compress_start)
        return 0;

    compress_region(st, compress_start, greedy_end);
    return st->compressed_count;
}

static int compress_full_middle(traj_state_t *st) {
    mark_protected(st);

    int start = -1;

    for (int i = 0; i < st->count; i++) {
        if (st->entries[i].protected) {
            if (start >= 0) {
                compress_region(st, start, i);
            }
            start = -1;
        } else {
            if (start < 0)
                start = i;
        }
    }

    if (start >= 0)
        compress_region(st, start, st->count);

    return st->compressed_count;
}

int traj_compress_run(traj_state_t *st) {
    if (!st || st->count < TRAJ_COMPRESS_THRESHOLD)
        return 0;

    if (st->target_tokens > 0)
        return compress_budget_aware(st);

    return compress_full_middle(st);
}

int traj_compress_save(const traj_state_t *st, const char *output_path) {
    if (!st || !output_path)
        return -1;

    FILE *f = fopen(output_path, "w");
    if (!f)
        return -1;

    int written = 0;
    for (int i = 0; i < st->count; i++) {
        if (!st->entries[i].content)
            continue;
        fprintf(f, "%s\n", st->entries[i].content);
        written++;
    }
    int flush_err = fflush(f) != 0;
    fclose(f);
    return flush_err ? -1 : written;
}

void traj_compress_free(traj_state_t *st) {
    if (!st)
        return;
    for (int i = 0; i < st->count; i++)
        free(st->entries[i].content);
    memset(st, 0, sizeof(*st));
}

int traj_compress_file(const char *input_path, const char *output_path) {
    traj_state_t st;
    int n = traj_compress_load(&st, input_path);
    if (n < 0)
        return -1;

    int compressed = traj_compress_run(&st);
    if (compressed <= 0) {
        traj_compress_free(&st);
        return 0;
    }

    int written = traj_compress_save(&st, output_path);
    traj_compress_free(&st);
    return written;
}

int traj_compress_file_with_budget(const char *input_path, const char *output_path,
                                   int target_tokens) {
    traj_state_t st;
    int n = traj_compress_load(&st, input_path);
    if (n < 0)
        return -1;

    st.target_tokens = target_tokens;
    int compressed = traj_compress_run(&st);
    if (compressed <= 0) {
        traj_compress_free(&st);
        return 0;
    }

    int written = traj_compress_save(&st, output_path);
    traj_compress_free(&st);
    return written;
}
