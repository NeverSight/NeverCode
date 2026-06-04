#include "terminal.h"
#include "nevercode.h"
#include <stdarg.h>
#include <sys/ioctl.h>

/* Slash names: tab completion (readline) + fuzzy hints for unknown commands. */
static const char *g_term_slash_cmds[] = {"/agents",
                                          "/autotest",
                                          "/away",
                                          "/batch",
                                          "/branch",
                                          "/bridge",
                                          "/brief",
                                          "/btw",
                                          "/clear",
                                          "/commit",
                                          "/compact",
                                          "/completion",
                                          "/config",
                                          "/context",
                                          "/coord",
                                          "/coordinator",
                                          "/copy",
                                          "/cost",
                                          "/debug",
                                          "/diag",
                                          "/diff",
                                          "/doctor",
                                          "/effort",
                                          "/endplan",
                                          "/env",
                                          "/episodes",
                                          "/eval",
                                          "/exit",
                                          "/exploit",
                                          "/export",
                                          "/fallback",
                                          "/fast",
                                          "/features",
                                          "/files",
                                          "/git",
                                          "/harness",
                                          "/help",
                                          "/history",
                                          "/hooks",
                                          "/hyp",
                                          "/hypothesis",
                                          "/init",
                                          "/insights",
                                          "/kb",
                                          "/keybindings",
                                          "/keys",
                                          "/mcp",
                                          "/memory",
                                          "/model",
                                          "/models",
                                          "/mpreset",
                                          "/network",
                                          "/output-style",
                                          "/paste",
                                          "/perf",
                                          "/permissions",
                                          "/personality",
                                          "/plan",
                                          "/pr",
                                          "/preset",
                                          "/procs",
                                          "/quit",
                                          "/realtime",
                                          "/recon",
                                          "/remember",
                                          "/rename",
                                          "/resume",
                                          "/review",
                                          "/rewind",
                                          "/routing",
                                          "/rules",
                                          "/scaffold",
                                          "/security-review",
                                          "/session",
                                          "/session-memory",
                                          "/share",
                                          "/simplify",
                                          "/skills",
                                          "/sm",
                                          "/snip",
                                          "/stats",
                                          "/status",
                                          "/stuck",
                                          "/suggest",
                                          "/summary",
                                          "/tasks",
                                          "/test",
                                          "/timing",
                                          "/tips",
                                          "/tokens",
                                          "/undo",
                                          "/verify",
                                          "/version",
                                          "/vim",
                                          NULL};

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>

static fuzzy_result_t g_fuzzy_results[64];
static int g_fuzzy_count;
static int g_fuzzy_idx;

static char *slash_cmd_generator(const char *text, int state) {
    if (!state) {
        int len = (int)strlen(text);

        /* First pass: try exact prefix matching (fast path) */
        int prefix_count = 0;
        for (int i = 0; g_term_slash_cmds[i]; i++)
            if (strncmp(g_term_slash_cmds[i], text, len) == 0)
                prefix_count++;

        if (prefix_count > 0) {
            /* Exact prefix matches available — use them (traditional behavior) */
            g_fuzzy_count = 0;
            g_fuzzy_idx = 0;
            for (int i = 0; g_term_slash_cmds[i]; i++) {
                if (strncmp(g_term_slash_cmds[i], text, len) == 0 && g_fuzzy_count < 64) {
                    g_fuzzy_results[g_fuzzy_count].item = g_term_slash_cmds[i];
                    g_fuzzy_results[g_fuzzy_count].score = 0;
                    g_fuzzy_count++;
                }
            }
        } else {
            /* No prefix match — fall back to fuzzy matching */
            const char *items[128];
            int item_count = 0;
            for (int i = 0; g_term_slash_cmds[i] && item_count < 128; i++)
                items[item_count++] = g_term_slash_cmds[i];
            g_fuzzy_count = fuzzy_filter(items, item_count, text, g_fuzzy_results, 64);
            g_fuzzy_idx = 0;
        }
    }

    if (g_fuzzy_idx < g_fuzzy_count)
        return nc_xstrdup(g_fuzzy_results[g_fuzzy_idx++].item);
    return NULL;
}

static char *file_path_generator(const char *text, int state) {
    static DIR *dir;
    static char dir_part[NC_PATH_MAX];
    static const char *base_part;
    static size_t base_len;

    if (!state) {
        const char *at = text;
        if (*at == '@')
            at++;

        const char *last_slash = strrchr(at, '/');
        if (last_slash) {
            size_t dlen = last_slash - at + 1;
            if (dlen >= sizeof(dir_part))
                dlen = sizeof(dir_part) - 1;
            memcpy(dir_part, at, dlen);
            dir_part[dlen] = '\0';
            base_part = last_slash + 1;
        } else {
            dir_part[0] = '.';
            dir_part[1] = '\0';
            base_part = at;
        }
        base_len = strlen(base_part);

        if (dir)
            closedir(dir);
        dir = opendir(dir_part);
    }

    if (!dir)
        return NULL;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        if (strncmp(ent->d_name, base_part, base_len) != 0)
            continue;

        char full[NC_PATH_MAX];
        if (strcmp(dir_part, ".") == 0)
            nc_strlcpy(full, ent->d_name, sizeof(full));
        else
            snprintf(full, sizeof(full), "%s%s", dir_part, ent->d_name);

        struct stat st;
        char stat_path[NC_PATH_MAX];
        snprintf(stat_path, sizeof(stat_path), "%s/%s", dir_part, ent->d_name);
        if (stat(stat_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            size_t fl = strlen(full);
            full[fl] = '/';
            full[fl + 1] = '\0';
        }

        char result[NC_PATH_MAX];
        snprintf(result, sizeof(result), "@%s", full);
        return nc_xstrdup(result);
    }
    closedir(dir);
    dir = NULL;
    return NULL;
}

static char **nevercode_completion(const char *text, int start, int end) {
    (void)end;
    rl_attempted_completion_over = 1;

    if (start == 0 && text[0] == '/')
        return rl_completion_matches(text, slash_cmd_generator);

    if (text[0] == '@')
        return rl_completion_matches(text, file_path_generator);

    return NULL;
}
#endif

static bool g_tty;
static bool g_no_markdown;

/* streaming markdown state */
static bool g_in_code_block;
static bool g_line_start;
static bool g_waiting;
static bool g_pending_trailing_nl;
static bool g_partial_line_drawn;
static long g_stream_start_ms;
static strbuf_t g_line_buf;

/* integrated subsystems */
static spinner_t g_spinner;

static void render_line(const char *line);

static bool term_stream_should_draw(void) {
    return true;
}

static void term_stream_flush_draw(void) {
    fflush(stdout);
}

static void term_stream_reset_partial_line(void) {
    if (!g_tty || !g_partial_line_drawn)
        return;
    if (g_in_code_block)
        fputs(T_RESET, stdout);
    fputs("\033[u\033[J", stdout);
    g_partial_line_drawn = false;
}

static void term_stream_render_partial_line(void) {
    if (!g_tty || g_line_buf.len == 0 || !g_line_buf.data)
        return;
    term_stream_reset_partial_line();
    fputs("\033[s", stdout);
    if (g_in_code_block)
        fputs(T_DIM, stdout);
    render_line(g_line_buf.data);
    g_partial_line_drawn = true;
}

void term_init(void) {
    g_tty = isatty(STDOUT_FILENO);
    g_no_markdown = false;
    g_in_code_block = false;
    g_line_start = true;
    g_pending_trailing_nl = false;
    g_partial_line_drawn = false;
    strbuf_init(&g_line_buf);
    spinner_init(&g_spinner);
#ifdef USE_READLINE
    rl_attempted_completion_function = nevercode_completion;
    rl_completer_word_break_characters = " \t\n";
#endif
}

void term_set_no_markdown(bool v) {
    g_no_markdown = v;
}

static char g_history_path[NC_PATH_MAX];

static void ensure_history_path(void) {
    if (g_history_path[0])
        return;
    const char *home = nc_home_dir();
    if (!home)
        return;
    config_find_path(g_history_path, sizeof(g_history_path), "input_history");
}

void term_history_load(void) {
#ifdef USE_READLINE
    ensure_history_path();
    if (g_history_path[0])
        read_history(g_history_path);
#endif
}

void term_history_save(void) {
#ifdef USE_READLINE
    ensure_history_path();
    if (!g_history_path[0])
        return;
    char dir[NC_PATH_MAX];
    nc_strlcpy(dir, config_home_dir(), sizeof(dir));
    mkdir(dir, NC_DIR_MODE_PRIVATE);
    stifle_history(HISTORY_STIFLE_MAX);
    write_history(g_history_path);
#endif
}

void term_cleanup(void) {
    if (g_in_code_block && g_tty)
        fputs(T_RESET, stdout);
    g_in_code_block = false;
    g_partial_line_drawn = false;
    strbuf_free(&g_line_buf);
}

void term_color(const char *color, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (g_tty && color)
        fputs(color, stdout);
    vfprintf(stdout, fmt, ap);
    if (g_tty && color)
        fputs(T_RESET, stdout);
    va_end(ap);
    fflush(stdout);
}

#define is_cjk_punct nc_is_cjk_punct

static void fputs_code_tokens(const char *code, size_t code_len, const char *color, FILE *f) {
    size_t end = code_len;
    for (;;) {
        if (end > 0 && strchr(".,;:!?", code[end - 1]))
            end--;
        else if (end >= 3 && is_cjk_punct(code + end - 3, 3))
            end -= 3;
        else
            break;
    }
    size_t k = 0;
    while (k < end) {
        size_t sep;
        if (code[k] == ' ') {
            fputc(' ', f);
            k++;
        } else if ((sep = is_cjk_punct(code + k, end - k)) > 0) {
            fwrite(code + k, 1, sep, f);
            k += sep;
        } else {
            size_t ts = k;
            while (k < end && code[k] != ' ' && is_cjk_punct(code + k, end - k) == 0)
                k++;
            size_t tl = k - ts;
            fputs(color, f);
            fwrite(code + ts, 1, tl, f);
            fputs(T_RESET, f);
        }
    }
    if (end < code_len)
        fwrite(code + end, 1, code_len - end, f);
}

static void fputs_rendered(const char *text, FILE *f, const char *code_color) {
    size_t len = strlen(text);
    size_t i = 0;
    while (i < len) {
        if (text[i] == '`') {
            size_t j = i + 1;
            while (j < len && text[j] != '`')
                j++;
            if (j < len) {
                fputs_code_tokens(text + i + 1, j - i - 1, code_color, f);
                i = j + 1;
                continue;
            }
        }
        if (i + 1 < len && text[i] == '~' && text[i + 1] == '~') {
            size_t j = i + 2;
            while (j + 1 < len && !(text[j] == '~' && text[j + 1] == '~'))
                j++;
            if (j + 1 < len) {
                fputs(T_STRIKE, f);
                fwrite(text + i + 2, 1, j - i - 2, f);
                fputs(T_RESET, f);
                i = j + 2;
                continue;
            }
        }
        if (i + 1 < len && text[i] == '*' && text[i + 1] == '*') {
            size_t j = i + 2;
            while (j + 1 < len && !(text[j] == '*' && text[j + 1] == '*'))
                j++;
            if (j + 1 < len) {
                fputs(T_BOLD, f);
                fwrite(text + i + 2, 1, j - i - 2, f);
                fputs(T_RESET, f);
                i = j + 2;
                continue;
            }
        }
        if (text[i] == '*' && (i + 1 < len) && text[i + 1] != '*') {
            size_t j = i + 1;
            while (j < len && text[j] != '*')
                j++;
            if (j < len) {
                fputs(T_ITALIC, f);
                fwrite(text + i + 1, 1, j - i - 1, f);
                fputs(T_RESET, f);
                i = j + 1;
                continue;
            }
        }
        if (text[i] == '[') {
            size_t close = i + 1;
            while (close < len && text[close] != ']')
                close++;
            if (close + 1 < len && text[close + 1] == '(') {
                size_t paren = close + 2;
                while (paren < len && text[paren] != ')')
                    paren++;
                if (paren < len) {
                    fputs(T_CYAN, f);
                    fwrite(text + i + 1, 1, close - i - 1, f);
                    fputs(T_RESET T_DIM " (", f);
                    fwrite(text + close + 2, 1, paren - close - 2, f);
                    fputs(")" T_RESET, f);
                    i = paren + 1;
                    continue;
                }
            }
        }
        fputc(text[i], f);
        i++;
    }
}

static void render_line(const char *line) {
    const char *raw = line;
    size_t raw_len = strlen(line);
    size_t ll = raw_len;

    if (!g_tty || g_no_markdown) {
        fputs(line, stdout);
        return;
    }

    if (g_in_code_block) {
        fputs(line, stdout);
        return;
    }

    while (ll > 0 && isspace((unsigned char)line[0])) {
        line++;
        ll--;
    }

    /* headers */
    if (ll >= 3 && line[0] == '#' && line[1] == '#' && line[2] == '#') {
        const char *h = line + 3;
        size_t hl = ll - 3;
        fputs(T_BOLD T_CYAN "### ", stdout);
        while (hl > 0 && *h == ' ') {
            h++;
            hl--;
        }
        fputs_rendered(h, stdout, T_GREEN);
        fputs(T_RESET, stdout);
        return;
    }
    if (ll >= 2 && line[0] == '#' && line[1] == '#') {
        const char *h = line + 2;
        size_t hl = ll - 2;
        fputs(T_BOLD T_CYAN "## ", stdout);
        while (hl > 0 && *h == ' ') {
            h++;
            hl--;
        }
        fputs_rendered(h, stdout, T_GREEN);
        fputs(T_RESET, stdout);
        return;
    }
    if (ll >= 1 && line[0] == '#') {
        int lvl = 0;
        const char *h;
        size_t hl;
        while (line[lvl] == '#')
            lvl++;
        if (line[lvl] == ' ') {
            h = line + lvl;
            hl = ll - (size_t)lvl;
            while (hl > 0 && *h == ' ') {
                h++;
                hl--;
            }
            fputs(T_BOLD T_CYAN, stdout);
            fputs_rendered(h, stdout, T_GREEN);
            fputs(T_RESET, stdout);
            return;
        }
    }

    /* horizontal rule: --- or *** or ___ (3+ chars) */
    if (ll >= 3 && (line[0] == '-' || line[0] == '*' || line[0] == '_') && line[1] == line[0] &&
        line[2] == line[0]) {
        bool is_hr = true;
        for (const char *c = line + 3; *c; c++) {
            if (*c != line[0] && *c != ' ') {
                is_hr = false;
                break;
            }
        }
        if (is_hr) {
            int w = term_width();
            if (w <= 0)
                w = TD_DEFAULT_COLS;
            fputs(T_DIM, stdout);
            for (int i = 0; i < w; i++)
                fputc('-', stdout);
            fputs(T_RESET, stdout);
            return;
        }
    }

    /* bullet points */
    if (raw_len >= 4 && (raw[0] == ' ' || raw[0] == '\t') &&
        ((raw[0] == ' ' && raw_len >= 4 && (raw[2] == '-' || raw[2] == '*') && raw[3] == ' ') ||
         (raw[0] == '\t' && raw_len >= 3 && (raw[1] == '-' || raw[1] == '*') && raw[2] == ' '))) {
        const char *body = raw;
        size_t remain = raw_len;
        while (*body == ' ' || *body == '\t') {
            body++;
            remain--;
        }
        if (remain >= 2 && (body[0] == '-' || body[0] == '*') && body[1] == ' ') {
            fputs("    \xe2\x97\xa6 ", stdout); /* ◦ */
            fputs_rendered(body + 2, stdout, T_GREEN);
            return;
        }
    }

    /* bullet points */
    if (ll >= 2 && (line[0] == '-' || line[0] == '*') && line[1] == ' ') {
        fputs("  \xe2\x80\xa2 ", stdout); /* • */
        fputs_rendered(line + 2, stdout, T_GREEN);
        return;
    }

    /* numbered lists: "1. ", "2. ", etc. */
    if (ll >= 2 && line[0] >= '0' && line[0] <= '9') {
        const char *dot = line;
        while (*dot >= '0' && *dot <= '9')
            dot++;
        if (dot > line && dot[0] == '.' && dot[1] == ' ') {
            fputs("  ", stdout);
            fwrite(line, 1, dot - line + 1, stdout);
            fputs_rendered(dot + 1, stdout, T_GREEN);
            return;
        }
    }

    /* blockquotes: "> text" */
    if (ll >= 1 && line[0] == '>' && (line[1] == ' ' || line[1] == '\0')) {
        fputs(T_DIM, stdout);
        fputs("\xe2\x94\x82 ", stdout); /* │ */
        fputs(T_RESET T_DIM, stdout);
        fputs_rendered(line + (line[1] == ' ' ? 2 : 1), stdout, T_GREEN);
        fputs(T_RESET, stdout);
        return;
    }

    /* diff-style lines in code context */
    if (ll >= 1 && line[0] == '+' && (ll == 1 || line[1] != '+')) {
        fputs(T_GREEN, stdout);
        fputs(line, stdout);
        fputs(T_RESET, stdout);
        return;
    }
    if (ll >= 1 && line[0] == '-' && (ll == 1 || line[1] != '-')) {
        fputs(T_RED, stdout);
        fputs(line, stdout);
        fputs(T_RESET, stdout);
        return;
    }

    /* inline formatting: **bold**, `code`, [text](url) */
    fputs_rendered(line, stdout, T_YELLOW);
}

static void term_stream_emit_complete_line(const char *line, bool defer_newline) {
    term_stream_reset_partial_line();
    if (g_in_code_block && g_tty)
        fputs(T_DIM, stdout);

    if (!g_no_markdown && nc_starts_with(line, "```")) {
        if (!g_in_code_block) {
            g_in_code_block = true;
            if (g_tty)
                fputs(T_DIM, stdout);
            fputs("──────────", stdout);
        } else {
            fputs("──────────", stdout);
            if (g_tty)
                fputs(T_RESET, stdout);
            g_in_code_block = false;
        }
    } else {
        render_line(line);
    }

    g_partial_line_drawn = false;
    if (defer_newline) {
        g_pending_trailing_nl = true;
    } else {
        fputc('\n', stdout);
        g_pending_trailing_nl = false;
    }
}

void term_stream_start(void) {
    g_in_code_block = false;
    g_line_start = true;
    g_waiting = true;
    g_pending_trailing_nl = false;
    g_partial_line_drawn = false;
    g_stream_start_ms = nc_mono_ms();
    strbuf_clear(&g_line_buf);
    if (g_tty) {
        spinner_start(&g_spinner);
        const char *f = spinner_tick(&g_spinner);
        fprintf(stdout, T_DIM "%s thinking..." T_RESET, f ? f : "⠋");
        fflush(stdout);
    }
}

void term_stream_clear_waiting(void) {
    if (g_waiting && g_tty) {
        spinner_stop(&g_spinner);
        fputs(T_CLEAR_LINE, stdout);
        fflush(stdout);
    }
    g_waiting = false;
}

void term_stream_text(const char *text) {
    bool emitted_complete_line = false;

    if (!text)
        return;
    if (g_waiting)
        term_stream_clear_waiting();
    if (g_pending_trailing_nl) {
        /* Previous chunk ended right after '\n'; flush it now so the next
         * chunk starts on a fresh visual line instead of merging content. */
        fputc('\n', stdout);
        g_pending_trailing_nl = false;
        emitted_complete_line = true;
    }

    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            if (g_line_buf.data)
                g_line_buf.data[g_line_buf.len] = '\0';
            const char *line = g_line_buf.data ? g_line_buf.data : "";
            bool defer_newline = (*(p + 1) == '\0');
            term_stream_emit_complete_line(line, defer_newline);
            emitted_complete_line = true;

            strbuf_clear(&g_line_buf);
            g_line_start = true;
        } else {
            char c = *p;
            strbuf_append(&g_line_buf, &c, 1);
            g_line_start = false;
        }
    }

    bool drew_partial = false;
    if (g_line_buf.len > 0 && term_stream_should_draw()) {
        term_stream_render_partial_line();
        drew_partial = true;
    }

    if (!g_tty) {
        fflush(stdout);
        return;
    }
    if (emitted_complete_line || drew_partial)
        term_stream_flush_draw();
}

void term_stream_end(void) {
    /* flush remaining partial line */
    if (g_line_buf.len > 0 && g_line_buf.data) {
        term_stream_reset_partial_line();
        if (g_in_code_block && g_tty)
            fputs(T_DIM, stdout);
        render_line(g_line_buf.data);
    }
    strbuf_clear(&g_line_buf);
    g_pending_trailing_nl = false;
    g_partial_line_drawn = false;

    if (g_in_code_block && g_tty)
        fputs(T_RESET, stdout);
    g_in_code_block = false;

    fputc('\n', stdout);
    fflush(stdout);

    double elapsed = (double)(nc_mono_ms() - g_stream_start_ms) / 1000.0;
    term_notify_task_done("response", elapsed);
}

double term_stream_elapsed(void) {
    return (double)(nc_mono_ms() - g_stream_start_ms) / 1000.0;
}

void term_separator(void) {
    int w = term_width();
    if (w <= 0)
        w = TD_DEFAULT_COLS;
    if (g_tty)
        fputs(T_DIM, stdout);
    bool unicode = td_has_unicode();
    for (int i = 0; i < w; i++)
        fputs(unicode ? "\u2500" : "-", stdout);
    fputc('\n', stdout);
    if (g_tty)
        fputs(T_RESET, stdout);
}

char *term_readline(const char *prompt) {
#ifdef USE_READLINE
    char *line = readline(prompt);
    if (line && *line)
        add_history(line);
    return line;
#else
    if (prompt) {
        /* strip readline escape wrappers for plain output */
        const char *p = prompt;
        while (*p) {
            if (*p == '\001' || *p == '\002') {
                p++;
                continue;
            }
            fputc(*p, stdout);
            p++;
        }
    }
    fflush(stdout);
    char buf[NC_CMD_MAX];
    if (!fgets(buf, sizeof(buf), stdin))
        return NULL;
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[--len] = '\0';
    return nc_xstrdup(buf);
#endif
}

const char **term_get_slash_cmds(void) {
    return g_term_slash_cmds;
}

bool term_is_tty(void) {
    return g_tty;
}

int term_width(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80;
}

static bool g_title_active;

static bool is_disallowed_title_char(unsigned int cp) {
    if (cp < 0x20)
        return true;
    if (cp == 0x7F || cp == 0x9C || cp == 0x9D)
        return true;
    switch (cp) {
    case 0x00AD:
    case 0x034F:
    case 0x061C:
    case 0x180E:
    case 0xFEFF:
        return true;
    default:
        break;
    }
    if (cp >= 0x200B && cp <= 0x200F)
        return true;
    if (cp >= 0x202A && cp <= 0x202E)
        return true;
    if (cp >= 0x2060 && cp <= 0x206F)
        return true;
    if (cp >= 0xFE00 && cp <= 0xFE0F)
        return true;
    if (cp >= 0xFFF9 && cp <= 0xFFFB)
        return true;
    return false;
}

static unsigned int decode_utf8(const unsigned char *s, int *adv) {
    unsigned char c = s[0];
    if (c < 0x80) {
        *adv = 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        *adv = 2;
        return ((unsigned int)(c & 0x1F) << 6) | (s[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *adv = 3;
        return ((unsigned int)(c & 0x0F) << 12) | ((unsigned int)(s[1] & 0x3F) << 6) |
               (s[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
        (s[3] & 0xC0) == 0x80) {
        *adv = 4;
        return ((unsigned int)(c & 0x07) << 18) | ((unsigned int)(s[1] & 0x3F) << 12) |
               ((unsigned int)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    }
    *adv = 1;
    return '?';
}

/* NC_NC_TITLE_MAX_VISIBLE in defaults.h */

static void sanitize_title(char *buf, size_t cap) {
    char tmp[NC_LABEL_MAX];
    size_t j = 0;
    bool pending_sp = false;
    int visible = 0;
    const unsigned char *s = (const unsigned char *)buf;

    while (*s && j < sizeof(tmp) - 5 && visible < NC_TITLE_MAX_VISIBLE) {
        int adv = 1;
        unsigned int cp = decode_utf8(s, &adv);

        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') {
            if (j > 0)
                pending_sp = true;
            s += adv;
            continue;
        }
        if (is_disallowed_title_char(cp)) {
            s += adv;
            continue;
        }
        if (pending_sp && visible < NC_TITLE_MAX_VISIBLE - 1) {
            tmp[j++] = ' ';
            visible++;
            pending_sp = false;
        }
        for (int k = 0; k < adv && j < sizeof(tmp) - 1; k++)
            tmp[j++] = (char)s[k];
        visible++;
        s += adv;
    }
    tmp[j] = '\0';
    size_t len = j < cap - 1 ? j : cap - 1;
    memcpy(buf, tmp, len);
    buf[len] = '\0';
}

void term_title_set(const char *fmt, ...) {
    if (!g_tty)
        return;
    char raw[NC_LABEL_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(raw, sizeof(raw), fmt, ap);
    va_end(ap);
    sanitize_title(raw, sizeof(raw));
    if (!raw[0])
        return;
    fprintf(stdout, "\033]0;%s\033\\", raw);
    fflush(stdout);
    g_title_active = true;
}

void term_title_clear(void) {
    if (!g_tty || !g_title_active)
        return;
    fputs("\033]0;\033\\", stdout);
    fflush(stdout);
    g_title_active = false;
}
