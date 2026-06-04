#include "tools.h"
#include "nevercode.h"
#include "git_ops.h"

static const char *ENTER_SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"branch\":{\"type\":\"string\",\"description\":\"Branch name for the worktree\"},"
    "\"path\":{\"type\":\"string\",\"description\":\"Directory path for the worktree (optional)\"}"
    "},"
    "\"required\":[\"branch\"]"
    "}";

static const char *EXIT_SCHEMA = "{"
                                 "\"type\":\"object\","
                                 "\"properties\":{"
                                 "\"remove\":{\"type\":\"boolean\",\"description\":\"Remove the "
                                 "worktree directory (default false)\"}"
                                 "}"
                                 "}";

static char g_original_cwd[NC_PATH_MAX] = {0};
static char g_worktree_path[NC_PATH_MAX] = {0};
static char g_worktree_branch[NC_LABEL_MAX] = {0};

static int enter_worktree_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *branch = nc_cjson_str((cJSON *)input, "branch", NULL);
    if (!branch || !*branch) {
        *output = nc_xstrdup("error: missing 'branch'");
        return 1;
    }

    if (g_worktree_path[0]) {
        *output = nc_xstrdup("error: already in a worktree. Use ExitWorktree first.");
        return 1;
    }

    if (!git_is_repo(NULL)) {
        *output = nc_xstrdup("error: not inside a git repository");
        return 1;
    }

    if (!getcwd(g_original_cwd, sizeof(g_original_cwd))) {
        *output = nc_xstrdup("error: getcwd() failed");
        return 1;
    }

    const char *path = nc_cjson_str((cJSON *)input, "path", NULL);
    if (path && *path) {
        nc_strlcpy(g_worktree_path, path, sizeof(g_worktree_path));
    } else {
        snprintf(g_worktree_path, sizeof(g_worktree_path), "%s/.nevercode-worktree-%s",
                 g_original_cwd, branch);
    }

    char result[NC_PATH_MAX];
    (void)git_worktree_add(NULL, g_worktree_path, branch, result, sizeof(result));

    struct stat st;
    if (stat(g_worktree_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        g_worktree_path[0] = '\0';
        size_t msz = strlen(result) + 128;
        char *msg = malloc(msz);
        if (!msg) {
            *output = nc_xstrdup("error: failed to create worktree");
            return 1;
        }
        snprintf(msg, msz, "error: failed to create worktree: %s", result);
        *output = msg;
        return 1;
    }

    if (chdir(g_worktree_path) != 0) {
        *output = nc_xstrdup("error: chdir to worktree failed");
        g_worktree_path[0] = '\0';
        return 1;
    }

    nc_strlcpy(g_worktree_branch, branch, sizeof(g_worktree_branch));

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "Entered worktree:\n");
    strbuf_append_str(&sb, "  branch: ");
    strbuf_append_str(&sb, branch);
    strbuf_append_str(&sb, "\n  path: ");
    strbuf_append_str(&sb, g_worktree_path);
    strbuf_append_str(&sb, "\n  original: ");
    strbuf_append_str(&sb, g_original_cwd);
    *output = strbuf_detach(&sb);
    return 0;
}

static int exit_worktree_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;

    if (!g_worktree_path[0]) {
        *output = nc_xstrdup("Not currently in a worktree.");
        return 0;
    }

    if (chdir(g_original_cwd) != 0) {
        *output = nc_xstrdup("error: failed to return to original directory");
        return 1;
    }

    bool do_remove = false;
    if (input) {
        cJSON *rm = cJSON_GetObjectItem((cJSON *)input, "remove");
        if (rm && cJSON_IsTrue(rm))
            do_remove = true;
    }

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "Exited worktree:\n");
    strbuf_append_str(&sb, "  branch: ");
    strbuf_append_str(&sb, g_worktree_branch);
    strbuf_append_str(&sb, "\n  path: ");
    strbuf_append_str(&sb, g_worktree_path);

    if (do_remove) {
        char result[NC_PATH_MAX];
        git_worktree_remove(NULL, g_worktree_path, result, sizeof(result));
        strbuf_append_str(&sb, "\n  removed: true");
    } else {
        strbuf_append_str(&sb, "\n  removed: false (use remove=true to clean up)");
    }

    strbuf_append_str(&sb, "\n  returned to: ");
    strbuf_append_str(&sb, g_original_cwd);

    g_worktree_path[0] = '\0';
    g_worktree_branch[0] = '\0';
    g_original_cwd[0] = '\0';

    *output = strbuf_detach(&sb);
    return 0;
}

static tool_def_t enter_wt_tool;
static tool_def_t exit_wt_tool;

void tool_enter_worktree_register(void) {
    enter_wt_tool.name = "EnterWorktree";
    enter_wt_tool.description = "Create a git worktree for isolated file system work. "
                                "Changes working directory to the new worktree.";
    enter_wt_tool.input_schema = cJSON_Parse(ENTER_SCHEMA);
    enter_wt_tool.execute = enter_worktree_exec;
    enter_wt_tool.check_permission = NULL;
    enter_wt_tool.read_only = false;
    enter_wt_tool.concurrency_safe = false;
    enter_wt_tool.destructive = false;
    tools_register(&enter_wt_tool);
}

void tool_exit_worktree_register(void) {
    exit_wt_tool.name = "ExitWorktree";
    exit_wt_tool.description =
        "Exit the current git worktree and return to the original directory.";
    exit_wt_tool.input_schema = cJSON_Parse(EXIT_SCHEMA);
    exit_wt_tool.execute = exit_worktree_exec;
    exit_wt_tool.check_permission = NULL;
    exit_wt_tool.read_only = false;
    exit_wt_tool.concurrency_safe = false;
    exit_wt_tool.destructive = false;
    tools_register(&exit_wt_tool);
}
