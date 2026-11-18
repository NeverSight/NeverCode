#include "tools.h"
#include "nevercode.h"

static const char *SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"pattern\":{\"type\":\"string\",\"description\":\"Search pattern (regex)\"},"
    "\"path\":{\"type\":\"string\",\"description\":\"File or directory to search\"},"
    "\"include\":{\"type\":\"string\",\"description\":\"File glob filter (e.g. *.c)\"},"
    "\"context\":{\"type\":\"integer\",\"description\":\"Lines of context before and after each "
    "match (like grep -C)\"}"
    "},"
    "\"required\":[\"pattern\"]"
    "}";

static bool has_rg_cached = false;
static bool has_rg = false;

static bool check_has_rg(void) {
    if (has_rg_cached)
        return has_rg;
    has_rg_cached = true;
    has_rg = nc_cmd_exists("rg");
    return has_rg;
}

static void shell_escape(strbuf_t *cmd, const char *s) {
    for (const char *p = s; *p; p++) {
        if (*p == '\'')
            strbuf_append_str(cmd, "'\\''");
        else {
            char c = *p;
            strbuf_append(cmd, &c, 1);
        }
    }
}

static int grep_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *pattern = nc_cjson_str((cJSON *)input, "pattern", NULL);
    const char *path = nc_cjson_str((cJSON *)input, "path", NULL);
    const char *include = nc_cjson_str((cJSON *)input, "include", NULL);
    cJSON *ctx_lines = cJSON_GetObjectItem((cJSON *)input, "context");
    int context_n = (ctx_lines && cJSON_IsNumber(ctx_lines)) ? (int)ctx_lines->valuedouble : 0;
    if (context_n < 0)
        context_n = 0;
    if (context_n > GREP_MAX_CTX_LINES)
        context_n = GREP_MAX_CTX_LINES;

    if (!pattern) {
        *output = nc_xstrdup("error: missing 'pattern'");
        return 1;
    }

    char search_path[NC_PATH_MAX];
    if (path && path[0])
        nc_strlcpy(search_path, path, sizeof(search_path));
    else if (!getcwd(search_path, sizeof(search_path)))
        search_path[0] = '\0';

    strbuf_t cmd;
    strbuf_init(&cmd);

    if (check_has_rg()) {
        char mc_arg[64];
        snprintf(mc_arg, sizeof(mc_arg), "rg --no-heading --line-number --max-count %d ",
                 GREP_MAX_MATCHES);
        strbuf_append_str(&cmd, mc_arg);
        if (context_n > 0) {
            char ctx_arg[32];
            snprintf(ctx_arg, sizeof(ctx_arg), "-C %d ", context_n);
            strbuf_append_str(&cmd, ctx_arg);
        }
        if (include) {
            strbuf_append_str(&cmd, "--glob '");
            shell_escape(&cmd, include);
            strbuf_append_str(&cmd, "' ");
        }
        strbuf_append_str(&cmd, "-- '");
        shell_escape(&cmd, pattern);
        strbuf_append_str(&cmd, "' '");
        shell_escape(&cmd, search_path);
        strbuf_append_str(&cmd, "' 2>/dev/null");
    } else {
        strbuf_append_str(&cmd, "grep -rn ");
        if (context_n > 0) {
            char ctx_arg[32];
            snprintf(ctx_arg, sizeof(ctx_arg), "-C %d ", context_n);
            strbuf_append_str(&cmd, ctx_arg);
        }
        if (include) {
            strbuf_append_str(&cmd, "--include='");
            shell_escape(&cmd, include);
            strbuf_append_str(&cmd, "' ");
        }
        strbuf_append_str(&cmd, "-- '");
        shell_escape(&cmd, pattern);
        strbuf_append_str(&cmd, "' '");
        shell_escape(&cmd, search_path);
        char head_arg[NC_FMT_MAX];
        snprintf(head_arg, sizeof(head_arg), "' 2>/dev/null | head -%d", GREP_MAX_MATCHES);
        strbuf_append_str(&cmd, head_arg);
    }

    FILE *fp = popen(cmd.data, "r");
    strbuf_free(&cmd);

    if (!fp) {
        *output = nc_xstrdup("error: failed to run search");
        return 1;
    }

    strbuf_t sb;
    strbuf_init(&sb);
    char buf[NC_IO_BUF];
    while (fgets(buf, sizeof(buf), fp)) {
        if (sb.len >= GREP_OUTPUT_MAX_BYTES) {
            strbuf_append_str(&sb, "\n... [output truncated]");
            break;
        }
        strbuf_append_str(&sb, buf);
    }
    pclose(fp);

    if (sb.len == 0)
        strbuf_append_str(&sb, "(no matches)");

    *output = strbuf_detach(&sb);
    return 0;
}

static tool_def_t grep_tool;

void tool_grep_register(void) {
    grep_tool.name = "Grep";
    grep_tool.description = "Search file contents using regex. Uses ripgrep for speed. "
                            "Search recursively in a directory or within a single file.";
    grep_tool.input_schema = cJSON_Parse(SCHEMA);
    grep_tool.execute = grep_exec;
    grep_tool.read_only = true;
    grep_tool.concurrency_safe = true;
    grep_tool.destructive = false;
    tools_register(&grep_tool);
}
