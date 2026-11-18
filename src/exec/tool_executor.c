#include "repl.h"
#include "tools.h"
#include "terminal.h"
#include "git_ops.h"

static void doom_after_tool(const char *name, cJSON *input, const char *args_fallback) {
    char *json = NULL;
    if (input)
        json = cJSON_PrintUnformatted(input);
    else if (args_fallback && args_fallback[0])
        json = nc_xstrdup(args_fallback);
    else
        json = nc_xstrdup("{}");
    if (!json)
        json = nc_xstrdup("{}");
    if (doom_loop_record(name, json)) {
        char *s = doom_loop_format_status();
        if (s) {
            term_color(T_YELLOW, "[doom-loop] %s\n", s);
            free(s);
        }
    }
    free(json);
}

typedef struct {
    int index;
    int rc;
    char *output;
    const char *name;
    const char *args_str;
    const tool_def_t *tool;
} parallel_result_t;

typedef enum {
    BASH_PATH_TOKEN_END = 0,
    BASH_PATH_TOKEN_WORD,
    BASH_PATH_TOKEN_CMDSEP,
    BASH_PATH_TOKEN_REDIR,
    BASH_PATH_TOKEN_OTHER
} bash_path_token_kind_t;

/* BASH_CAPTURE_ARGS_MAX defined in defaults.h */

static const char *bash_path_next_token(const char *s, char *out, size_t cap,
                                        bash_path_token_kind_t *kind) {
    s = nc_skip_ws(s);
    if (!s || !*s) {
        if (out && cap)
            out[0] = '\0';
        *kind = BASH_PATH_TOKEN_END;
        return s;
    }

    if ((s[0] == '&' && s[1] == '&') || (s[0] == '|' && s[1] == '|')) {
        if (cap >= 3) {
            out[0] = s[0];
            out[1] = s[1];
            out[2] = '\0';
        }
        *kind = BASH_PATH_TOKEN_CMDSEP;
        return s + 2;
    }
    if (s[0] == ';' || s[0] == '|') {
        if (cap >= 2) {
            out[0] = s[0];
            out[1] = '\0';
        }
        *kind = BASH_PATH_TOKEN_CMDSEP;
        return s + 1;
    }
    if (s[0] == '&' && s[1] == '>') {
        size_t n = (s[2] == '>') ? 3 : 2;
        if (cap > n) {
            memcpy(out, s, n);
            out[n] = '\0';
        }
        *kind = BASH_PATH_TOKEN_REDIR;
        return s + n;
    }
    if (isdigit((unsigned char)s[0])) {
        const char *p = s;
        while (*p && isdigit((unsigned char)*p))
            p++;
        if (*p == '>') {
            p++;
            if (*p == '>')
                p++;
            size_t n = (size_t)(p - s);
            if (cap > n) {
                memcpy(out, s, n);
                out[n] = '\0';
            }
            *kind = BASH_PATH_TOKEN_REDIR;
            return p;
        }
    }
    if (s[0] == '>') {
        size_t n = (s[1] == '>') ? 2 : 1;
        if (cap > n) {
            memcpy(out, s, n);
            out[n] = '\0';
        }
        *kind = BASH_PATH_TOKEN_REDIR;
        return s + n;
    }
    if (s[0] == '<') {
        size_t n = (s[1] == '<') ? 2 : 1;
        if (cap > n) {
            memcpy(out, s, n);
            out[n] = '\0';
        }
        *kind = BASH_PATH_TOKEN_OTHER;
        return s + n;
    }

    size_t n = 0;
    char quote = '\0';
    while (*s) {
        if (!quote) {
            if (isspace((unsigned char)*s) || *s == ';' || *s == '|' || *s == '>' || *s == '<' ||
                (s[0] == '&' && s[1] == '&'))
                break;
            if (*s == '\'' || *s == '"') {
                quote = *s++;
                continue;
            }
            if (*s == '\\' && s[1]) {
                if (n + 1 < cap)
                    out[n++] = s[1];
                s += 2;
                continue;
            }
        } else {
            if (*s == quote) {
                quote = '\0';
                s++;
                continue;
            }
            if (*s == '\\' && quote == '"' && s[1]) {
                if (n + 1 < cap)
                    out[n++] = s[1];
                s += 2;
                continue;
            }
        }
        if (n + 1 < cap)
            out[n++] = *s;
        s++;
    }
    if (cap)
        out[n] = '\0';
    *kind = BASH_PATH_TOKEN_WORD;
    return s;
}

static const char *bash_path_cmd_name(const char *cmd) {
    const char *base = strrchr(cmd ? cmd : "", '/');
    return base ? base + 1 : cmd;
}

static bool bash_cmd_tracks_literal_args(const char *cmd) {
    const char *name = bash_path_cmd_name(cmd);
    return name &&
           (strcmp(name, "mv") == 0 || strcmp(name, "cp") == 0 || strcmp(name, "touch") == 0 ||
            strcmp(name, "unlink") == 0 || strcmp(name, "tee") == 0);
}

static bool bash_cmd_is(const char *cmd, const char *name) {
    const char *base = bash_path_cmd_name(cmd);
    return base && strcmp(base, name) == 0;
}

static bool bash_token_all_digits(const char *s) {
    if (!s || !*s)
        return false;
    for (const char *p = s; *p; p++)
        if (!isdigit((unsigned char)*p))
            return false;
    return true;
}

static bool bash_token_is_literal_path(const char *token, bool allow_plain_name) {
    if (!token || !*token || strcmp(token, "-") == 0 || strcmp(token, "--") == 0)
        return false;
    if (token[0] == '&' || token[0] == '$' || strstr(token, "://") != NULL)
        return false;
    if (strpbrk(token, "*?[]{}()|;&\n\r") != NULL)
        return false;
    if (!allow_plain_name && strchr(token, '/') == NULL && !nc_starts_with(token, "./") &&
        !nc_starts_with(token, "../") && token[0] != '/' && token[0] != '~')
        return false;
    if (allow_plain_name && strchr(token, '/') == NULL && token[0] != '/' &&
        !nc_starts_with(token, "./") && !nc_starts_with(token, "../") && token[0] != '~' &&
        bash_token_all_digits(token))
        return false;
    return true;
}

static void bash_trim_trailing_slashes(char *path) {
    if (!path)
        return;
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/')
        path[--len] = '\0';
}

static void bash_resolve_base_dir(const cJSON *input, char *out, size_t cap) {
    const char *wd = nc_cjson_str(input, "working_directory", NULL);
    char cwd[NC_PATH_MAX];
    if (!wd || !*wd) {
        if (getcwd(cwd, sizeof(cwd)))
            nc_strlcpy(out, cwd, cap);
        else
            nc_strlcpy(out, ".", cap);
        return;
    }
    if (wd[0] == '/' || (wd[0] == '~' && wd[1] == '/')) {
        if (wd[0] == '~') {
            const char *home = nc_home_dir();
            if (home)
                {
                char joined[2 * NC_PATH_MAX];
                int _n = snprintf(joined, sizeof(joined), "%.4095s/%.4095s", home, wd + 2);
                if (_n > 0)
                    nc_strlcpy(out, joined, cap);
                else
                    nc_strlcpy(out, wd, cap);
            }
            else
                nc_strlcpy(out, wd, cap);
        } else
            nc_strlcpy(out, wd, cap);
        return;
    }
    if (getcwd(cwd, sizeof(cwd))) {
        char joined[2 * NC_PATH_MAX];
        int _n = snprintf(joined, sizeof(joined), "%.4095s/%.4095s", cwd, wd);
        if (_n > 0)
            nc_strlcpy(out, joined, cap);
        else
            nc_strlcpy(out, wd, cap);
    } else {
        nc_strlcpy(out, wd, cap);
    }
}

static bool git_repo_roots_match(const char *lhs, const char *rhs) {
    char lhs_root[NC_PATH_MAX];
    char rhs_root[NC_PATH_MAX];
    if (!lhs || !rhs)
        return false;
    if (!git_is_repo(lhs) || !git_is_repo(rhs))
        return false;
    if (git_repo_root(lhs, lhs_root, sizeof(lhs_root)) != 0)
        return false;
    if (git_repo_root(rhs, rhs_root, sizeof(rhs_root)) != 0)
        return false;
    return strcmp(lhs_root, rhs_root) == 0;
}

static bool tool_should_capture_git_undo(const char *name, const cJSON *input) {
    char cwd[NC_PATH_MAX];
    if (!git_undo_available())
        return false;
    if (!getcwd(cwd, sizeof(cwd)) || !git_is_repo(cwd))
        return false;
    if (strcmp(name, "Bash") != 0 || !input)
        return true;

    char base_dir[NC_PATH_MAX];
    bash_resolve_base_dir(input, base_dir, sizeof(base_dir));
    return git_repo_roots_match(cwd, base_dir);
}

static void bash_resolve_snapshot_path(const char *base_dir, const char *token, char *out,
                                       size_t cap) {
    if (!token || !*token) {
        if (cap)
            out[0] = '\0';
        return;
    }
    if (token[0] == '/') {
        nc_strlcpy(out, token, cap);
        bash_trim_trailing_slashes(out);
        return;
    }
    if (token[0] == '~' && token[1] == '/') {
        const char *home = nc_home_dir();
        if (home)
            snprintf(out, cap, "%s/%s", home, token + 2);
        else
            nc_strlcpy(out, token, cap);
        bash_trim_trailing_slashes(out);
        return;
    }
    snprintf(out, cap, "%s/%s", base_dir && *base_dir ? base_dir : ".", token);
    bash_trim_trailing_slashes(out);
}

static void bash_capture_path_candidate(const char *base_dir, const char *token,
                                        bool allow_plain_name, bool capture_directories) {
    char resolved[NC_PATH_MAX];
    struct stat st;
    if (!bash_token_is_literal_path(token, allow_plain_name))
        return;
    bash_resolve_snapshot_path(base_dir, token, resolved, sizeof(resolved));
    if (!resolved[0])
        return;
    if (stat(resolved, &st) == 0 && S_ISDIR(st.st_mode) && !capture_directories)
        return;
    file_history_snapshot(resolved);
}

static void bash_capture_mkdir_candidate(const char *base_dir, const char *token) {
    char resolved[NC_PATH_MAX];
    struct stat st;
    if (!bash_token_is_literal_path(token, true))
        return;
    bash_resolve_snapshot_path(base_dir, token, resolved, sizeof(resolved));
    if (!resolved[0])
        return;
    if (stat(resolved, &st) == 0)
        return;
    file_history_snapshot(resolved);
}

static bool bash_path_is_existing_dir(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void bash_path_basename_buf(const char *path, char *out, size_t cap) {
    char tmp[NC_PATH_MAX];
    const char *base;
    if (!path || !*path) {
        if (cap)
            out[0] = '\0';
        return;
    }
    nc_strlcpy(tmp, path, sizeof(tmp));
    bash_trim_trailing_slashes(tmp);
    base = strrchr(tmp, '/');
    base = base ? base + 1 : tmp;
    nc_strlcpy(out, base, cap);
}

static void bash_resolve_child_path(const char *parent, const char *source_path, char *out,
                                    size_t cap) {
    char base[NC_LABEL_MAX];
    bash_path_basename_buf(source_path, base, sizeof(base));
    if (!base[0]) {
        if (cap)
            out[0] = '\0';
        return;
    }
    snprintf(out, cap, "%.2045s/%.2045s", parent, base);
    bash_trim_trailing_slashes(out);
}

static void bash_capture_move_copy_paths(const char *base_dir, const char *cmd,
                                         char args[][NC_PATH_MAX], int argc) {
    bool is_move = bash_cmd_is(cmd, "mv");
    char dest_path[NC_PATH_MAX];
    bool dest_is_dir;
    bool multi_source;
    if (argc < 2)
        return;

    bash_resolve_snapshot_path(base_dir, args[argc - 1], dest_path, sizeof(dest_path));
    if (!dest_path[0])
        return;
    dest_is_dir = bash_path_is_existing_dir(dest_path);
    multi_source = argc > 2;
    if (multi_source && !dest_is_dir)
        return;

    for (int i = 0; i < argc - 1; i++) {
        char source_path[NC_PATH_MAX];
        char target_path[NC_PATH_MAX];
        bash_resolve_snapshot_path(base_dir, args[i], source_path, sizeof(source_path));
        if (!source_path[0])
            continue;
        if (is_move)
            file_history_snapshot(source_path);
        if (dest_is_dir || multi_source)
            bash_resolve_child_path(dest_path, source_path, target_path, sizeof(target_path));
        else
            nc_strlcpy(target_path, dest_path, sizeof(target_path));
        if (!target_path[0])
            continue;
        file_history_snapshot(target_path);
    }
}

static void bash_capture_command_args(const char *base_dir, const char *active_cmd,
                                      char args[][NC_PATH_MAX], int argc) {
    if (!active_cmd || !*active_cmd || argc <= 0)
        return;
    if (bash_cmd_is(active_cmd, "mkdir")) {
        for (int i = 0; i < argc; i++)
            bash_capture_mkdir_candidate(base_dir, args[i]);
        return;
    }
    if (bash_cmd_is(active_cmd, "rmdir") || bash_cmd_is(active_cmd, "rm")) {
        for (int i = 0; i < argc; i++)
            bash_capture_path_candidate(base_dir, args[i], true, true);
        return;
    }
    if (bash_cmd_is(active_cmd, "mv") || bash_cmd_is(active_cmd, "cp")) {
        bash_capture_move_copy_paths(base_dir, active_cmd, args, argc);
        return;
    }
    if (bash_cmd_tracks_literal_args(active_cmd)) {
        for (int i = 0; i < argc; i++)
            bash_capture_path_candidate(base_dir, args[i], true, false);
    }
}

static void bash_capture_literal_paths(const cJSON *input) {
    const char *cmd = input ? nc_cjson_str(input, "command", NULL) : NULL;
    if (!cmd || !*cmd)
        return;

    char base_dir[NC_PATH_MAX];
    bash_resolve_base_dir(input, base_dir, sizeof(base_dir));

    char token[NC_PATH_MAX];
    char active_cmd[NC_LABEL_MAX];
    char args[BASH_CAPTURE_ARGS_MAX][NC_PATH_MAX];
    const char *cur = cmd;
    bool have_cmd = false;
    bool expect_redir_path = false;
    int argc = 0;
    while (1) {
        bash_path_token_kind_t kind = BASH_PATH_TOKEN_END;
        cur = bash_path_next_token(cur, token, sizeof(token), &kind);
        if (kind == BASH_PATH_TOKEN_END)
            break;
        if (kind == BASH_PATH_TOKEN_CMDSEP) {
            bash_capture_command_args(base_dir, active_cmd, args, argc);
            have_cmd = false;
            expect_redir_path = false;
            argc = 0;
            continue;
        }
        if (kind == BASH_PATH_TOKEN_REDIR) {
            expect_redir_path = true;
            continue;
        }
        if (kind != BASH_PATH_TOKEN_WORD)
            continue;
        if (expect_redir_path) {
            bash_capture_path_candidate(base_dir, token, true, false);
            expect_redir_path = false;
            continue;
        }
        if (!have_cmd) {
            nc_strlcpy(active_cmd, token, sizeof(active_cmd));
            have_cmd = true;
            continue;
        }
        if (token[0] == '-')
            continue;
        if (argc < BASH_CAPTURE_ARGS_MAX)
            nc_strlcpy(args[argc++], token, sizeof(args[0]));
    }
    bash_capture_command_args(base_dir, active_cmd, args, argc);
}

static bool tool_is_concurrency_safe(const tool_def_t *t, const char *args_str) {
    if (!t)
        return false;
    if (t->concurrency_safe)
        return true;
    if (strcmp(t->name, "Bash") == 0 && args_str) {
        cJSON *inp = tool_args_parse_json(args_str);
        if (inp && bash_check_readonly(inp)) {
            cJSON_Delete(inp);
            return true;
        }
        if (inp)
            cJSON_Delete(inp);
    }
    return false;
}

static void *parallel_tool_worker(void *arg) {
    parallel_result_t *r = arg;
    cJSON *input = r->args_str ? tool_args_parse_json(r->args_str) : NULL;
    {
        routed_call_t rc;
        if (tool_router_classify(r->name, input, &rc) == 0)
            routed_call_free(&rc);
    }
    r->output = NULL;
    r->rc = r->tool->execute(input, &r->output, g_cfg_ptr);
    /* doom_loop_record: called from parent after pthread_join (not here — avoids races). */
    if (input)
        cJSON_Delete(input);
    return NULL;
}

static void execute_batch_parallel(conversation_t *conv, stream_state_t *st, int from, int to) {
    int n = to - from;
    if (n <= 0)
        return;
    if (n == 1) {
        const char *name = st->pending_tc[from].name;
        const char *args_str = st->pending_tc[from].args.data;
        const tool_def_t *tool = tools_find(name);
        if (!tool || !tool->execute) {
            message_t *rm = conversation_add(conv, ROLE_TOOL);
            message_add_tool_result(rm, st->pending_tc[from].id, "Tool not found", 1);
            return;
        }
        cJSON *input = args_str ? tool_args_parse_json(args_str) : NULL;
        if (!input && args_str && args_str[0]) {
            term_color(T_YELLOW, "[%s] warning: failed to parse args (%.*s%s)\n", name,
                       TOOL_ARGS_PREVIEW_MAX, args_str,
                       strlen(args_str) > TOOL_ARGS_PREVIEW_MAX ? "..." : "");
        }
        {
            routed_call_t rc;
            if (tool_router_classify(name, input, &rc) == 0)
                routed_call_free(&rc);
        }

        const char *cmd_hint = input ? nc_cjson_str(input, "command", NULL) : NULL;
        const char *path_hint = input ? nc_cjson_str(input, "path", NULL) : NULL;
        if (!path_hint && input)
            path_hint = nc_cjson_str(input, "file_path", NULL);
        if (cmd_hint)
            term_color(T_CYAN, "[%s] $ %s\n", name, cmd_hint);
        else if (path_hint)
            term_color(T_CYAN, "[%s] %s\n", name, path_hint);
        else
            term_color(T_CYAN, "[%s]\n", name);

        char *output = NULL;
        long t_start = nc_mono_ms();
        int rc = tool->execute(input, &output, g_cfg_ptr);
        long exec_ms = nc_mono_ms() - t_start;
        cost_tracker_add_tool(name, exec_ms, rc != 0);
        if (exec_ms >= TOOL_EXEC_TIMING_THRESHOLD_MS)
            term_color(T_DIM, "[%s done in %.1fs]\n", name, exec_ms / 1000.0);
        doom_after_tool(name, input, args_str);
        if (input)
            cJSON_Delete(input);
        if (output && strlen(output) > TOOL_RESULT_MAX_CHARS) {
            char *t = tool_result_truncate(output, name);
            free(output);
            output = t;
        }
        message_t *rm = conversation_add(conv, ROLE_TOOL);
        message_add_tool_result(rm, st->pending_tc[from].id, output ? output : "(no output)",
                                rc != 0);
        free(output);
        return;
    }

    parallel_result_t *results = calloc(n, sizeof(parallel_result_t));
    pthread_t *threads = calloc(n, sizeof(pthread_t));
    bool *launched = calloc(n, sizeof(bool));
    if (!results || !threads || !launched) {
        free(results);
        free(threads);
        free(launched);
        return;
    }

    for (int i = 0; i < n; i++) {
        int si = from + i;
        const char *tname = st->pending_tc[si].name;
        const char *args = st->pending_tc[si].args.data;
        cJSON *inp = args ? tool_args_parse_json(args) : NULL;
        const char *hint = inp ? nc_cjson_str(inp, "command", NULL) : NULL;
        bool is_cmd = (hint != NULL);
        if (!hint && inp)
            hint = nc_cjson_str(inp, "path", NULL);
        if (!hint && inp)
            hint = nc_cjson_str(inp, "file_path", NULL);
        if (!hint && inp)
            hint = nc_cjson_str(inp, "pattern", NULL);
        if (hint) {
            const char *display = hint;
            if (is_cmd) {
                while (*display && (*display == ' ' || *display == '\n'))
                    display++;
                if (nc_starts_with(display, "set -e")) {
                    display += 6;
                    while (*display && (*display == ' ' || *display == '\n' || *display == '&'))
                        display++;
                }
            }
            const char *nl = strchr(display, '\n');
            if (nl)
                term_color(T_CYAN, "[%s] %s%.*s\n", tname, is_cmd ? "$ " : "", (int)(nl - display),
                           display);
            else
                term_color(T_CYAN, "[%s] %s%s\n", tname, is_cmd ? "$ " : "", display);
        } else
            term_color(T_CYAN, "[%s]\n", tname);
        if (inp)
            cJSON_Delete(inp);
    }

    for (int i = 0; i < n; i++) {
        int si = from + i;
        results[i].index = i;
        results[i].name = st->pending_tc[si].name;
        results[i].args_str = st->pending_tc[si].args.data;
        results[i].tool = tools_find(results[i].name);
        results[i].output = NULL;
        if (!results[i].tool || !results[i].tool->execute) {
            results[i].rc = 1;
            results[i].output = nc_xstrdup("Tool not found");
            continue;
        }
        if (pthread_create(&threads[i], NULL, parallel_tool_worker, &results[i]) != 0) {
            results[i].rc = 1;
            results[i].output = nc_xstrdup("pthread_create failed");
            continue;
        }
        launched[i] = true;
    }
    for (int i = 0; i < n; i++)
        if (launched[i])
            pthread_join(threads[i], NULL);
    for (int i = 0; i < n; i++) {
        if (!launched[i])
            continue;
        cJSON *dinp = results[i].args_str ? tool_args_parse_json(results[i].args_str) : NULL;
        doom_after_tool(results[i].name, dinp, results[i].args_str);
        if (dinp)
            cJSON_Delete(dinp);
    }
    for (int i = 0; i < n; i++) {
        int si = from + i;
        char *fo = results[i].output;
        if (fo && strlen(fo) > TOOL_RESULT_MAX_CHARS) {
            char *t = tool_result_truncate(fo, results[i].name);
            free(fo);
            fo = t;
        }
        message_t *rm = conversation_add(conv, ROLE_TOOL);
        message_add_tool_result(rm, st->pending_tc[si].id, fo ? fo : "(no output)",
                                results[i].rc != 0);
        free(fo);
    }
    free(launched);
    free(threads);
    free(results);
}

static void execute_single_tool_serial(conversation_t *conv, stream_state_t *st, int i);

void execute_pending_tools(conversation_t *conv, stream_state_t *st) {
    int i = 0;
    while (i < st->num_tc) {
        const tool_def_t *t = tools_find(st->pending_tc[i].name);
        bool safe = tool_is_concurrency_safe(t, st->pending_tc[i].args.data);

        if (safe) {
            int batch_start = i;
            while (i < st->num_tc) {
                const tool_def_t *bt = tools_find(st->pending_tc[i].name);
                if (!tool_is_concurrency_safe(bt, st->pending_tc[i].args.data))
                    break;
                i++;
            }
            execute_batch_parallel(conv, st, batch_start, i);
        } else {
            execute_single_tool_serial(conv, st, i);
            i++;
        }
    }
}

#define SERIAL_CLEANUP(input)                                                                      \
    do {                                                                                           \
        if (input)                                                                                 \
            cJSON_Delete(input);                                                                   \
        return;                                                                                    \
    } while (0)

static void execute_single_tool_serial(conversation_t *conv, stream_state_t *st, int i) {
    const char *name = st->pending_tc[i].name;
    const char *id = st->pending_tc[i].id;
    const char *args_str = st->pending_tc[i].args.data;

    cJSON *input = args_str ? tool_args_parse_json(args_str) : NULL;
    if (!input && args_str && args_str[0]) {
        size_t preview = strlen(args_str);
        if (preview > TOOL_ARGS_PREVIEW_MAX)
            preview = TOOL_ARGS_PREVIEW_MAX;
        term_color(T_YELLOW, "[%s] warning: failed to parse args (%.*s%s)\n", name, (int)preview,
                   args_str, strlen(args_str) > TOOL_ARGS_PREVIEW_MAX ? "..." : "");
    }
    {
        routed_call_t rc;
        if (tool_router_classify(name, input, &rc) == 0)
            routed_call_free(&rc);
    }
    const tool_def_t *tool = tools_find(name);

    if (tool && tool->execute) {
        if (input && nc_starts_with(name, "mcp_"))
            cJSON_AddStringToObject(input, "_tool_name", name);

        bool effective_readonly = tool->read_only;
        if (strcmp(name, "Bash") == 0 && input)
            effective_readonly = bash_check_readonly(input);

        if (strcmp(name, "Bash") == 0 && input && !effective_readonly) {
            const char *cmd = nc_cjson_str(input, "command", NULL);
            if (cmd) {
                const char *justification = NULL;
                exec_decision_t ed = exec_rules_check(exec_rules_global(), cmd, &justification);
                if (ed == EXEC_DENY) {
                    char msg[NC_LINE_MAX];
                    snprintf(msg, sizeof(msg), "Command rejected by exec policy: %s%s%s", cmd,
                             justification ? " — " : "", justification ? justification : "");
                    term_color(T_RED, "[exec-policy] %s\n", msg);
                    message_t *rm = conversation_add(conv, ROLE_TOOL);
                    message_add_tool_result(rm, id, msg, 1);
                    SERIAL_CLEANUP(input);
                }
                if (ed == EXEC_PROMPT) {
                    char detail[NC_MSG_MAX];
                    snprintf(detail, sizeof(detail), "%s%s%s", cmd, justification ? " — " : "",
                             justification ? justification : "");
                    int choice = perm_prompt_user("Bash (exec-policy)", detail);
                    if (choice == 0) {
                        term_color(T_YELLOW, "[exec-policy] denied by user\n");
                        message_t *rm = conversation_add(conv, ROLE_TOOL);
                        message_add_tool_result(rm, id,
                                                "Command denied by user (exec policy prompt).", 1);
                        SERIAL_CLEANUP(input);
                    }
                }

                {
                    risk_assessment_t ra;
                    risk_assess_action(&ra, name, cmd);
                    risk_outcome_t ro = risk_assess_outcome(&ra);
                    if (ro == RISK_OUTCOME_DENY) {
                        char *rf = risk_assess_format(&ra);
                        if (rf) {
                            term_color(T_RED, "[risk] command blocked\n%s", rf);
                            free(rf);
                        } else
                            term_color(T_RED, "[risk] command blocked (score=%d)\n", ra.score);
                        message_t *rm = conversation_add(conv, ROLE_TOOL);
                        message_add_tool_result(
                            rm, id, "Command rejected: action risk score exceeds deny threshold.",
                            1);
                        SERIAL_CLEANUP(input);
                    }
                    if (ro == RISK_OUTCOME_REVIEW) {
                        char detail[TERR_DETAIL_MAX];
                        snprintf(detail, sizeof(detail), "%.500s", ra.rationale);
                        int choice = perm_prompt_user("Bash (risk)", detail);
                        if (choice == 0) {
                            term_color(T_YELLOW, "[risk] denied by user\n");
                            message_t *rm = conversation_add(conv, ROLE_TOOL);
                            message_add_tool_result(rm, id, "Command denied after risk review.", 1);
                            SERIAL_CLEANUP(input);
                        }
                    }
                }
            }
        }

        if (g_plan_mode && !effective_readonly) {
            term_color(T_YELLOW, "[plan mode] %s blocked (write tool)\n", name);
            message_t *rm = conversation_add(conv, ROLE_TOOL);
            message_add_tool_result(rm, id,
                                    "Tool blocked: plan mode is active. Only read-only tools "
                                    "are available. Use /endplan to exit plan mode.",
                                    1);
            SERIAL_CLEANUP(input);
        }

        char *hook_ctx_str = NULL;
        char *input_json = input ? cJSON_PrintUnformatted(input) : NULL;
        hook_decision_t hd =
            hooks_run(&g_hooks, HOOK_PRE_TOOL_USE, name, input_json, &hook_ctx_str);
        free(input_json);
        if (hd == HOOK_DECISION_BLOCK) {
            char msg[NC_LINE_MAX] = "Tool call blocked by hook";
            if (hook_ctx_str) {
                snprintf(msg, sizeof(msg), "Blocked by hook: %s", hook_ctx_str);
                free(hook_ctx_str);
            }
            message_t *rm = conversation_add(conv, ROLE_TOOL);
            message_add_tool_result(rm, id, msg, 1);
            SERIAL_CLEANUP(input);
        }
        free(hook_ctx_str);

        const char *path_hint = input ? nc_cjson_str(input, "path", NULL) : NULL;
        if (!path_hint && input)
            path_hint = nc_cjson_str(input, "url", NULL);
        if (!path_hint && input)
            path_hint = nc_cjson_str(input, "file_path", NULL);

        perm_decision_t pd;
        if (hd == HOOK_DECISION_SKIP || effective_readonly)
            pd = PERM_ALLOW;
        else
            pd = perm_check(&g_perms, name, path_hint);

        if (pd == PERM_ASK && strcmp(name, "Bash") == 0 && input) {
            const char *acmd = nc_cjson_str(input, "command", NULL);
            if (acmd && approval_cache_check(acmd))
                pd = PERM_ALLOW;
        }

        if (pd == PERM_ASK) {
            char detail[NC_LABEL_MAX] = "";
            if (path_hint)
                nc_strlcpy(detail, path_hint, sizeof(detail));
            else if (input) {
                const char *cmd = nc_cjson_str(input, "command", NULL);
                if (cmd)
                    snprintf(detail, sizeof(detail), "%.200s", cmd);
            }
            int answer = perm_prompt_user(name, detail);
            if (answer == 0) {
                message_t *rm = conversation_add(conv, ROLE_TOOL);
                message_add_tool_result(rm, id, "Permission denied by user", 1);
                SERIAL_CLEANUP(input);
            }
            if (answer == 2) {
                perm_add_rule(&g_perms, name, NULL, PERM_ALLOW);
                if (strcmp(name, "Bash") == 0 && input) {
                    const char *acmd = nc_cjson_str(input, "command", NULL);
                    if (acmd)
                        approval_cache_add(acmd);
                }
            }
        } else if (pd == PERM_DENY) {
            message_t *rm = conversation_add(conv, ROLE_TOOL);
            message_add_tool_result(rm, id, "Permission denied by policy", 1);
            SERIAL_CLEANUP(input);
        }

        if (!effective_readonly) {
            int turn = file_history_current_turn();
            if (turn > 0 && tool_should_capture_git_undo(name, input))
                git_undo_capture_turn(turn, name);
            const char *fpath = input ? nc_cjson_str(input, "file_path", NULL) : NULL;
            if (!fpath)
                fpath = path_hint;
            if (fpath)
                file_history_snapshot(fpath);
            if (strcmp(name, "Bash") == 0 && input)
                bash_capture_literal_paths(input);
        }

        char *output = NULL;
        const char *cmd_hint = input ? nc_cjson_str(input, "command", NULL) : NULL;
        if (cmd_hint) {
            const char *display = cmd_hint;
            while (*display && (*display == ' ' || *display == '\n'))
                display++;
            if (nc_starts_with(display, "set -e")) {
                display += 6;
                while (*display && (*display == ' ' || *display == '\n' || *display == '&'))
                    display++;
            }
            const char *nl = strchr(display, '\n');
            if (nl)
                term_color(T_CYAN, "[%s] $ %.*s\n", name, (int)(nl - display), display);
            else
                term_color(T_CYAN, "[%s] $ %s\n", name, display);
        } else if (path_hint)
            term_color(T_CYAN, "[%s] %s\n", name, path_hint);
        else if (strcmp(name, "TodoWrite") == 0 && input) {
            cJSON *todos = cJSON_GetObjectItem(input, "todos");
            int tc = todos ? cJSON_GetArraySize(todos) : 0;
            if (tc > 0) {
                cJSON *merge = cJSON_GetObjectItem(input, "merge");
                bool is_update = merge && cJSON_IsTrue(merge);
                cJSON *first = cJSON_GetArrayItem(todos, 0);
                const char *c = first ? nc_cjson_str(first, "content", NULL) : NULL;
                const char *todo_st = first ? nc_cjson_str(first, "status", NULL) : NULL;
                if (is_update && tc == 1 && todo_st && strcmp(todo_st, "completed") == 0)
                    term_color(T_DIM, "[%s] \xe2\x9c\x93 %s\n", name, c ? c : "done");
                else if (is_update)
                    term_color(T_DIM, "[%s] updated %d item%s\n", name, tc, tc > 1 ? "s" : "");
                else
                    term_color(T_DIM, "[%s] %d item%s%s%s\n", name, tc, tc > 1 ? "s" : "",
                               c ? " — " : "", c ? c : "");
            } else
                term_color(T_CYAN, "[%s]\n", name);
        } else
            term_color(T_CYAN, "[%s]\n", name);

        if (strcmp(name, "Read") == 0 && path_hint)
            read_file_tracker_add(&g_read_tracker, path_hint);

        int timeout_sec =
            (strcmp(name, "Bash") == 0) ? TOOL_EXEC_TIMEOUT_BASH : TOOL_EXEC_TIMEOUT_DEFAULT;
        if (strcmp(name, "WebFetch") == 0 || strcmp(name, "WebSearch") == 0)
            timeout_sec = TOOL_EXEC_TIMEOUT_SLOW;
        if (strcmp(name, "Agent") == 0 || strcmp(name, "Task") == 0)
            timeout_sec = TOOL_EXEC_TIMEOUT_LONG;

        long t0_ms = nc_mono_ms();
        int rc = tool->execute(input, &output, g_cfg_ptr);
        doom_after_tool(name, input, args_str);
        long elapsed_ms = nc_mono_ms() - t0_ms;

        if (elapsed_ms > timeout_sec * 1000L && rc != 0 && !output) {
            char tbuf[NC_LABEL_MAX];
            snprintf(tbuf, sizeof(tbuf), "Tool timed out after %lds (limit: %ds)",
                     elapsed_ms / 1000, timeout_sec);
            output = nc_xstrdup(tbuf);
        }
        if (elapsed_ms >= TOOL_EXEC_TIMING_THRESHOLD_MS)
            term_color(T_DIM, "[%s done in %.1fs]\n", name, elapsed_ms / 1000.0);

        char *final_output = output;
        if (output && strlen(output) > TOOL_RESULT_MAX_CHARS) {
            final_output = tool_result_truncate(output, name);
            term_color(T_DIM, "[output truncated: %zu -> %zu chars]\n", strlen(output),
                       strlen(final_output));
            free(output);
        }

        hooks_run(&g_hooks, HOOK_POST_TOOL_USE, name, final_output, NULL);

        if (strcmp(name, "Bash") == 0 && rc != 0 && final_output) {
            const char *lines = final_output;
            while (*lines) {
                const char *eol = strchr(lines, '\n');
                size_t llen = eol ? (size_t)(eol - lines) : strlen(lines);
                if (llen > 5 && llen < NC_TOOL_ERROR_LINE_MAX &&
                    (strstr(lines, "error") || strstr(lines, "Error") || strstr(lines, "warning") ||
                     strstr(lines, "FAIL"))) {
                    char lbuf[NC_MSG_MAX];
                    if (llen >= sizeof(lbuf))
                        llen = sizeof(lbuf) - 1;
                    memcpy(lbuf, lines, llen);
                    lbuf[llen] = '\0';
                    autofix_add_issue("(bash)", 0, lbuf);
                }
                if (!eol)
                    break;
                lines = eol + 1;
            }
        }

        if (strcmp(name, "Bash") == 0 && rc == 0)
            shell_snapshot_capture();

        message_t *rm = conversation_add(conv, ROLE_TOOL);
        message_add_tool_result(rm, id, final_output ? final_output : "(no output)", rc != 0);
        free(final_output);
    } else {
        term_color(T_YELLOW, "[unknown tool: %s]\n", name);
        message_t *rm = conversation_add(conv, ROLE_TOOL);
        message_add_tool_result(rm, id, "Unknown tool; not registered in tool registry", 1);
    }

    if (input)
        cJSON_Delete(input);
}

#undef SERIAL_CLEANUP

void auto_extract_memories(const conversation_t *conv, memory_ctx_t *mem) {
    if (!conv || conv->len < 4 || !mem)
        return;

    static int last_extracted_at = 0;
    if (conv->len - last_extracted_at < 4)
        return;
    last_extracted_at = conv->len;

    for (int i = conv->len - 2; i >= 0 && i > conv->len - 6; i--) {
        const message_t *m = &conv->msgs[i];
        if (m->role != ROLE_ASSISTANT)
            continue;

        for (int j = 0; j < m->num_blocks; j++) {
            const content_block_t *b = &m->blocks[j];
            if (b->type != CONTENT_TEXT || !b->text)
                continue;
            const char *txt = b->text;
            size_t tlen = strlen(txt);
            if (tlen < NC_INSIGHT_TEXT_MIN || tlen > NC_INSIGHT_TEXT_MAX)
                continue;

            const char *patterns[] = {"project uses ",    "stack is ",         "built with ",
                                      "framework is ",    "language is ",      "database is ",
                                      "deployed on ",     "runs on ",          "uses port ",
                                      "API key ",         "endpoint is ",      "base URL ",
                                      "test command is ", "build command is ", "prefer ",
                                      "convention is ",   "style guide ",      NULL};

            for (int k = 0; patterns[k]; k++) {
                const char *pos = strcasestr(txt, patterns[k]);
                if (!pos)
                    continue;

                const char *start = pos;
                while (start > txt && *(start - 1) != '.' && *(start - 1) != '\n')
                    start--;
                while (*start == ' ' || *start == '\n')
                    start++;

                const char *end = pos + strlen(patterns[k]);
                while (*end && *end != '.' && *end != '\n' &&
                       (end - start) < NC_INSIGHT_SNIPPET_MAX)
                    end++;
                if (*end == '.')
                    end++;

                size_t klen = strlen(patterns[k]);
                char key[NC_FMT_MAX];
                snprintf(key, sizeof(key), "auto_%.*s", (int)(klen > 50 ? 50 : klen), patterns[k]);
                for (char *p = key; *p; p++) {
                    if (*p == ' ')
                        *p = '_';
                }

                char val[NC_LABEL_MAX];
                size_t vlen = (size_t)(end - start);
                if (vlen > sizeof(val) - 1)
                    vlen = sizeof(val) - 1;
                memcpy(val, start, vlen);
                val[vlen] = '\0';

                if (vlen > 10 && !memory_get(mem, key))
                    memory_set(mem, key, val);
                break;
            }
        }
    }
}
