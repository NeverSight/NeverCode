/*
 * mem_pipeline.c — Two-phase memory extraction and consolidation.
 *
 * Phase 1: Extract raw memories from session rollouts.
 * Phase 2: Consolidate raw memories into a unified summary.
 */

#include "nevercode.h"

/* MPL_RAW_FILENAME, MPL_SUMMARY_SUBDIR, MPL_SUMMARY_FILENAME in defaults.h */
#define MPL_RAW_FILE MPL_RAW_FILENAME
#define MPL_SUMMARY_DIR MPL_SUMMARY_SUBDIR
#define MPL_SUMMARY_FILE MPL_SUMMARY_FILENAME

typedef struct {
    char root[NC_PATH_MAX];
    bool initialized;
    int phase1_runs;
    int phase1_extracted;
    int phase2_runs;
    int phase2_consolidated;
    long last_phase1_time;
    long last_phase2_time;
} mem_pipeline_ctx_t;

static mem_pipeline_ctx_t g_mpl;

void mem_pipeline_init(void) {
    config_home_path(g_mpl.root, sizeof(g_mpl.root), "memories");
    nc_mkdir_p(g_mpl.root);

    char sub[NC_PATH_MAX];
    snprintf(sub, sizeof(sub), "%s/%s", g_mpl.root, MPL_SUMMARY_DIR);
    nc_mkdir_p(sub);

    g_mpl.phase1_runs = 0;
    g_mpl.phase1_extracted = 0;
    g_mpl.phase2_runs = 0;
    g_mpl.phase2_consolidated = 0;
    g_mpl.last_phase1_time = 0;
    g_mpl.last_phase2_time = 0;
    g_mpl.initialized = true;
}

void mem_pipeline_free(void) {
    g_mpl.initialized = false;
}

bool mem_pipeline_ready(void) {
    return g_mpl.initialized;
}

int mem_pipeline_phase1_extract(const char *session_id, const message_t *msgs, int n_msgs,
                                char **raw_memory_out, char **summary_out) {
    if (!g_mpl.initialized || !session_id || !msgs || n_msgs <= 0)
        return -1;

    strbuf_t raw;
    strbuf_init(&raw);
    strbuf_appendf(&raw, "## Session `%s`\n\n", session_id);

    int user_turns = 0, tool_calls = 0;
    for (int i = 0; i < n_msgs; i++) {
        if (msgs[i].role == ROLE_USER)
            user_turns++;
        for (int j = 0; j < msgs[i].num_blocks; j++)
            if (msgs[i].blocks[j].type == CONTENT_TOOL_USE)
                tool_calls++;
    }
    strbuf_appendf(&raw, "- user_turns: %d\n- tool_calls: %d\n- total_messages: %d\n\n", user_turns,
                   tool_calls, n_msgs);

    int files_changed = 0;
    for (int i = 0; i < n_msgs; i++)
        for (int j = 0; j < msgs[i].num_blocks; j++)
            if (msgs[i].blocks[j].type == CONTENT_TOOL_USE && msgs[i].blocks[j].tool_name &&
                (strcmp(msgs[i].blocks[j].tool_name, "Write") == 0 ||
                 strcmp(msgs[i].blocks[j].tool_name, "Edit") == 0 ||
                 strcmp(msgs[i].blocks[j].tool_name, "ApplyPatch") == 0))
                files_changed++;
    if (files_changed > 0)
        strbuf_appendf(&raw, "- files_changed: %d\n\n", files_changed);

    for (int i = 0; i < n_msgs; i++) {
        if (msgs[i].role != ROLE_USER && msgs[i].role != ROLE_ASSISTANT)
            continue;
        const char *role = msgs[i].role == ROLE_USER ? "user" : "assistant";

        for (int j = 0; j < msgs[i].num_blocks; j++) {
            if (msgs[i].blocks[j].type != CONTENT_TEXT || !msgs[i].blocks[j].text)
                continue;

            int tok = estimate_tokens(msgs[i].blocks[j].text);
            if (tok > MPL_PHASE1_MIN_TOKENS) {
                char *trunc =
                    middle_truncate_tokens(msgs[i].blocks[j].text, MPL_PHASE1_SUMMARY_TOKENS);
                strbuf_appendf(&raw, "**%s**: %s\n\n", role,
                               trunc ? trunc : msgs[i].blocks[j].text);
                free(trunc);
            } else {
                strbuf_appendf(&raw, "**%s**: %s\n\n", role, msgs[i].blocks[j].text);
            }
        }
    }

    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s/%s.md", g_mpl.root, MPL_SUMMARY_DIR, session_id);

    *raw_memory_out = strbuf_detach(&raw);
    nc_write_file_str(path, *raw_memory_out);

    strbuf_t sum;
    strbuf_init(&sum);
    strbuf_appendf(&sum, "Session %s: %d user turns, %d tool calls, %d messages", session_id,
                   user_turns, tool_calls, n_msgs);
    *summary_out = strbuf_detach(&sum);

    g_mpl.phase1_runs++;
    g_mpl.phase1_extracted++;
    g_mpl.last_phase1_time = nc_wall_sec();
    return 0;
}

int mem_pipeline_phase2_consolidate(int max_memories) {
    if (!g_mpl.initialized)
        return -1;

    char dir_path[NC_PATH_MAX];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", g_mpl.root, MPL_SUMMARY_DIR);

    DIR *d = opendir(dir_path);
    if (!d)
        return -1;

    strbuf_t raw_all;
    strbuf_init(&raw_all);
    strbuf_append_str(&raw_all, "# Raw Memories\n\n");

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) && count < max_memories) {
        if (!str_has_suffix(ent->d_name, ".md"))
            continue;

        char fpath[NC_PATH_MAX];
        snprintf(fpath, sizeof(fpath), "%s/%s", dir_path, ent->d_name);

        char *content = strbuf_read_file(fpath, NULL);
        if (content) {
            strbuf_append_str(&raw_all, content);
            strbuf_append_str(&raw_all, "\n---\n\n");
            free(content);
            count++;
        }
    }
    closedir(d);

    char raw_path[NC_PATH_MAX];
    snprintf(raw_path, sizeof(raw_path), "%s/%s", g_mpl.root, MPL_RAW_FILE);
    char *raw_text = strbuf_detach(&raw_all);
    nc_write_file_str(raw_path, raw_text);
    free(raw_text);

    strbuf_t sum;
    strbuf_init(&sum);
    strbuf_appendf(&sum, "# Memory Summary\n\nTotal: %d\nLast: %ld\n", count, nc_wall_sec());

    char sum_path[NC_PATH_MAX];
    snprintf(sum_path, sizeof(sum_path), "%s/%s", g_mpl.root, MPL_SUMMARY_FILE);
    char *sum_text = strbuf_detach(&sum);
    nc_write_file_str(sum_path, sum_text);
    free(sum_text);

    g_mpl.phase2_runs++;
    g_mpl.phase2_consolidated += count;
    g_mpl.last_phase2_time = nc_wall_sec();
    return count;
}

int mem_pipeline_phase1_count(void) {
    return g_mpl.phase1_runs;
}

char *mem_pipeline_format_status(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb,
                   "Memory Pipeline:\n"
                   "  root: %s\n"
                   "  phase1 runs: %d (extracted: %d)\n"
                   "  phase2 runs: %d (consolidated: %d)\n",
                   g_mpl.root, g_mpl.phase1_runs, g_mpl.phase1_extracted, g_mpl.phase2_runs,
                   g_mpl.phase2_consolidated);
    return strbuf_detach(&sb);
}
