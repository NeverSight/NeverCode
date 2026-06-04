/*
 * code_mode.c — focused editing session context for the model
 */

#include "nevercode.h"

static struct {
    bool on;
    char *file;
    char *lang;
    int sl;
    int el;
    int edits;
} g;

void code_mode_init(void) {
    code_mode_exit();
}

int code_mode_enter(const char *file, const char *language) {
    code_mode_exit();
    if (!file || !file[0])
        return -1;
    g.on = true;
    g.file = nc_xstrdup(file);
    g.lang = language && language[0] ? nc_xstrdup(language)
                                     : nc_xstrdup(code_mode_detect_language(file));
    g.sl = g.el = 0;
    g.edits = 0;
    return 0;
}

void code_mode_exit(void) {
    if (g.file)
        free(g.file);
    if (g.lang)
        free(g.lang);
    memset(&g, 0, sizeof g);
}

bool code_mode_active(void) {
    return g.on;
}

const char *code_mode_language(void) {
    return g.lang ? g.lang : "";
}

const char *code_mode_detect_intent(const char *text) {
    if (!text)
        return "general";
    if (nc_contains_ci(text, "fix") || nc_contains_ci(text, "bug"))
        return "fix";
    if (nc_contains_ci(text, "refactor"))
        return "refactor";
    if (nc_contains_ci(text, "explain") || nc_contains_ci(text, "what does"))
        return "explain";
    if (nc_contains_ci(text, "add") || nc_contains_ci(text, "implement"))
        return "implement";
    return "general";
}

char *code_mode_build_prompt(void) {
    if (!g.on)
        return nc_xstrdup("");
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Focused code session: file=%s language=%s", g.file, g.lang);
    if (g.sl > 0)
        strbuf_appendf(&sb, " lines=%d-%d", g.sl, g.el > 0 ? g.el : g.sl);
    strbuf_appendf(&sb, " edits_applied=%d\n", g.edits);
    return strbuf_detach(&sb);
}

const char *code_mode_detect_language(const char *path) {
    if (!path)
        return "text";
    const char *dot = strrchr(path, '.');
    if (!dot)
        return "text";
    if (strcasecmp(dot, ".c") == 0 || strcasecmp(dot, ".h") == 0)
        return "c";
    if (strcasecmp(dot, ".cpp") == 0 || strcasecmp(dot, ".cc") == 0 || strcasecmp(dot, ".hpp") == 0)
        return "cpp";
    if (strcasecmp(dot, ".py") == 0)
        return "python";
    if (strcasecmp(dot, ".rs") == 0)
        return "rust";
    if (strcasecmp(dot, ".go") == 0)
        return "go";
    if (strcasecmp(dot, ".js") == 0 || strcasecmp(dot, ".mjs") == 0)
        return "javascript";
    if (strcasecmp(dot, ".ts") == 0 || strcasecmp(dot, ".tsx") == 0)
        return "typescript";
    if (strcasecmp(dot, ".sh") == 0)
        return "shell";
    if (strcasecmp(dot, ".md") == 0)
        return "markdown";
    if (strcasecmp(dot, ".json") == 0)
        return "json";
    return "text";
}

/* ── Execute / Wait / Notify Pattern ──
 *
 * Allows the model to run multi-step code execution sessions:
 * - execute: run a script, optionally yielding for tool calls
 * - wait: poll for completion or yield again
 * - notify: inject intermediate output for long-running scripts
 */

static struct {
    cm_exec_status_t status;
    char *cell_id;
    char *output;
    char *error;
    long started_ms;
    long wall_ms;
    int tool_calls;
} g_exec;

#define cm_now_ms nc_mono_ms

void code_mode_exec_reset(void) {
    free(g_exec.cell_id);
    free(g_exec.output);
    free(g_exec.error);
    memset(&g_exec, 0, sizeof(g_exec));
}

int code_mode_exec_start(const char *cell_id, const char *script) {
    if (!script || !script[0])
        return -1;
    code_mode_exec_reset();
    g_exec.cell_id = cell_id ? nc_xstrdup(cell_id) : nc_xstrdup("cell_0");
    g_exec.status = CM_EXEC_RUNNING;
    g_exec.started_ms = cm_now_ms();
    (void)script;
    return 0;
}

int code_mode_exec_yield(const char *partial_output) {
    if (g_exec.status != CM_EXEC_RUNNING)
        return -1;
    g_exec.status = CM_EXEC_YIELDED;
    free(g_exec.output);
    g_exec.output = partial_output ? nc_xstrdup(partial_output) : NULL;
    return 0;
}

int code_mode_exec_resume(void) {
    if (g_exec.status != CM_EXEC_YIELDED)
        return -1;
    g_exec.status = CM_EXEC_RUNNING;
    return 0;
}

int code_mode_exec_complete(const char *output, const char *error) {
    g_exec.wall_ms = cm_now_ms() - g_exec.started_ms;
    free(g_exec.output);
    free(g_exec.error);
    g_exec.output = output ? nc_xstrdup(output) : NULL;
    g_exec.error = error ? nc_xstrdup(error) : NULL;
    g_exec.status = error ? CM_EXEC_FAILED : CM_EXEC_COMPLETED;
    return 0;
}

int code_mode_exec_notify(const char *text) {
    if (g_exec.status != CM_EXEC_RUNNING && g_exec.status != CM_EXEC_YIELDED)
        return -1;
    if (!text || !text[0])
        return 0;
    if (g_exec.output) {
        strbuf_t sb;
        strbuf_init(&sb);
        strbuf_append_str(&sb, g_exec.output);
        strbuf_append_str(&sb, text);
        free(g_exec.output);
        g_exec.output = strbuf_detach(&sb);
    } else {
        g_exec.output = nc_xstrdup(text);
    }
    return 0;
}

void code_mode_exec_record_tool_call(void) {
    g_exec.tool_calls++;
}

cm_exec_status_t code_mode_exec_status(void) {
    return g_exec.status;
}

const char *code_mode_exec_output(void) {
    return g_exec.output;
}

long code_mode_exec_wall_ms(void) {
    if (g_exec.wall_ms > 0)
        return g_exec.wall_ms;
    if (g_exec.started_ms > 0)
        return cm_now_ms() - g_exec.started_ms;
    return 0;
}

const char *code_mode_exec_status_name(cm_exec_status_t s) {
    switch (s) {
    case CM_EXEC_IDLE:
        return "idle";
    case CM_EXEC_RUNNING:
        return "running";
    case CM_EXEC_YIELDED:
        return "yielded";
    case CM_EXEC_COMPLETED:
        return "completed";
    case CM_EXEC_FAILED:
        return "failed";
    }
    return "unknown";
}

char *code_mode_exec_format_result(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    const char *status_label = code_mode_exec_status_name(g_exec.status);

    if (g_exec.status == CM_EXEC_YIELDED) {
        strbuf_appendf(&sb, "Script running with cell ID %s\n",
                       g_exec.cell_id ? g_exec.cell_id : "?");
    } else if (g_exec.status == CM_EXEC_COMPLETED) {
        strbuf_append_str(&sb, "Script completed\n");
    } else if (g_exec.status == CM_EXEC_FAILED) {
        strbuf_append_str(&sb, "Script failed\n");
    } else {
        strbuf_appendf(&sb, "Script %s\n", status_label);
    }

    long wall = code_mode_exec_wall_ms();
    double wall_s = (double)wall / 1000.0;
    strbuf_appendf(&sb, "Wall time %.1f seconds\n", wall_s);

    if (g_exec.output && g_exec.output[0])
        strbuf_appendf(&sb, "Output:\n%s\n", g_exec.output);

    if (g_exec.error && g_exec.error[0])
        strbuf_appendf(&sb, "Script error:\n%s\n", g_exec.error);

    if (g_exec.tool_calls > 0)
        strbuf_appendf(&sb, "Nested tool calls: %d\n", g_exec.tool_calls);

    return strbuf_detach(&sb);
}
