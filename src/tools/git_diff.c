#include "tools.h"
#include "nevercode.h"
#include "git_ops.h"

static int git_diff_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *target = nc_cjson_str((cJSON *)input, "target", NULL);
    cJSON *staged_j = cJSON_GetObjectItem((cJSON *)input, "staged");
    bool staged = staged_j && cJSON_IsTrue(staged_j);

    strbuf_t sb;
    strbuf_init(&sb);

    char stat_buf[GIT_OPS_OUTPUT_BUF * 4];
    if (git_diff_stat_ex(NULL, staged, target, stat_buf, sizeof(stat_buf)) == 0 && stat_buf[0]) {
        strbuf_append_str(&sb, "=== Diff Summary ===\n");
        strbuf_append_str(&sb, stat_buf);
        strbuf_append_str(&sb, "\n");
    }

    char *diff_out = git_cmd_alloc(
        NULL,
        staged ? (const char *const[]){"diff", "--cached", target ? "--" : NULL, target}
               : (const char *const[]){"diff", target ? "--" : NULL, target},
        staged ? (target ? 4 : 2) : (target ? 3 : 1), DIFF_OUTPUT_MAX_BYTES);

    char untracked_buf[GIT_OPS_OUTPUT_BUF * 4];
    int has_untracked =
        git_ls_untracked(NULL, untracked_buf, sizeof(untracked_buf)) == 0 && untracked_buf[0];

    if (diff_out && diff_out[0]) {
        strbuf_append_str(&sb, "=== Full Diff ===\n");
        strbuf_append_str(&sb, diff_out);
    } else {
        strbuf_append_str(&sb, "(no differences found)\n");
    }

    if (has_untracked) {
        strbuf_append_str(&sb, "\n=== Untracked Files ===\n");
        strbuf_append_str(&sb, untracked_buf);
    }

    free(diff_out);

    *output = sb.data ? strbuf_detach(&sb) : nc_xstrdup("(no diff output)");
    return 0;
}

static const char *SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"target\":{\"type\":\"string\","
    "\"description\":\"File or directory path to diff (omit for all changes)\"},"
    "\"staged\":{\"type\":\"boolean\","
    "\"description\":\"Show only staged changes (default: false)\"}"
    "}"
    "}";

static tool_def_t git_diff_tool;

void tool_git_diff_register(void) {
    git_diff_tool.name = "GitDiff";
    git_diff_tool.description =
        "Show git diff for the current working tree. Displays changed files, "
        "line-by-line diff, and untracked files. Use 'staged' for cached changes.";
    git_diff_tool.input_schema = cJSON_Parse(SCHEMA);
    git_diff_tool.execute = git_diff_exec;
    git_diff_tool.read_only = true;
    git_diff_tool.concurrency_safe = true;
    git_diff_tool.destructive = false;
    tools_register(&git_diff_tool);
}
