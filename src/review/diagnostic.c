/*
 * diagnostic.c — Compiler/linter error parsing and fix suggestions
 *
 * Parses output from common build tools (gcc, clang, rustc, eslint, tsc,
 * python tracebacks) and extracts structured diagnostic entries with file,
 * line, column, severity, and message. Provides fix suggestions for common
 * error patterns.
 */

#include "nevercode.h"

void diagnostic_init(diagnostic_entry_t *d) {
    memset(d, 0, sizeof(*d));
    d->severity = DIAG_ERROR;
    d->line = -1;
    d->column = -1;
}

void diagnostic_free(diagnostic_entry_t *d) {
    free(d->file);
    free(d->message);
    free(d->suggestion);
    free(d->code);
    memset(d, 0, sizeof(*d));
}

void diagnostics_free(diagnostic_entry_t *diags, int count) {
    for (int i = 0; i < count; i++)
        diagnostic_free(&diags[i]);
}

static void set_str(char **dest, const char *src, size_t len) {
    free(*dest);
    *dest = malloc(len + 1);
    if (*dest) {
        memcpy(*dest, src, len);
        (*dest)[len] = '\0';
    }
}

static bool parse_gcc_clang_line(const char *line, diagnostic_entry_t *d) {
    const char *p = line;

    const char *colon1 = strchr(p, ':');
    if (!colon1 || colon1 == p)
        return false;

    if (*(colon1 + 1) < '0' || *(colon1 + 1) > '9')
        return false;

    set_str(&d->file, p, (size_t)(colon1 - p));

    const char *num_start = colon1 + 1;
    char *end;
    long lineno = strtol(num_start, &end, 10);
    if (end == num_start)
        return false;
    d->line = (int)lineno;

    if (*end == ':') {
        end++;
        long col = strtol(end, &end, 10);
        if (col > 0)
            d->column = (int)col;
    }

    if (*end == ':')
        end++;
    while (*end == ' ')
        end++;

    if (nc_starts_with(end, "error:")) {
        d->severity = DIAG_ERROR;
        end += 6;
    } else if (nc_starts_with(end, "warning:")) {
        d->severity = DIAG_WARNING;
        end += 8;
    } else if (nc_starts_with(end, "note:")) {
        d->severity = DIAG_NOTE;
        end += 5;
    } else if (nc_starts_with(end, "fatal error:")) {
        d->severity = DIAG_ERROR;
        end += 12;
    } else {
        d->severity = DIAG_ERROR;
    }

    while (*end == ' ')
        end++;
    size_t mlen = strlen(end);
    while (mlen > 0 && (end[mlen - 1] == '\n' || end[mlen - 1] == '\r'))
        mlen--;
    set_str(&d->message, end, mlen);

    return true;
}

static bool parse_python_traceback(const char *line, diagnostic_entry_t *d) {
    /* Match:   File "path", line N, ... */
    const char *marker = strstr(line, "File \"");
    if (!marker)
        return false;

    const char *fstart = marker + 6;
    const char *fend = strchr(fstart, '"');
    if (!fend)
        return false;

    set_str(&d->file, fstart, (size_t)(fend - fstart));

    const char *lp = strstr(fend, "line ");
    if (lp) {
        d->line = nc_safe_atoi(lp + 5, 0);
    }
    d->severity = DIAG_ERROR;
    d->message = nc_xstrdup("Python traceback");
    return d->line > 0;
}

static bool parse_eslint_line(const char *line, diagnostic_entry_t *d) {
    while (*line == ' ')
        line++;

    if (!isdigit((unsigned char)*line))
        return false;

    char *end;
    long lineno = strtol(line, &end, 10);
    if (*end != ':')
        return false;
    d->line = (int)lineno;

    end++;
    long col = strtol(end, &end, 10);
    if (col > 0)
        d->column = (int)col;

    while (*end == ' ')
        end++;

    if (nc_starts_with(end, "error")) {
        d->severity = DIAG_ERROR;
        end += 5;
    } else if (nc_starts_with(end, "warning")) {
        d->severity = DIAG_WARNING;
        end += 7;
    } else {
        return false;
    }

    while (*end == ' ')
        end++;
    size_t mlen = strlen(end);
    while (mlen > 0 && (end[mlen - 1] == '\n' || end[mlen - 1] == '\r'))
        mlen--;
    set_str(&d->message, end, mlen);

    return true;
}

static bool parse_tsc_line(const char *line, diagnostic_entry_t *d) {
    const char *paren = strchr(line, '(');
    if (!paren)
        return false;

    const char *comma = strchr(paren + 1, ',');
    if (!comma)
        return false;

    const char *rparen = strchr(comma + 1, ')');
    if (!rparen)
        return false;

    set_str(&d->file, line, (size_t)(paren - line));
    d->line = nc_safe_atoi(paren + 1, 0);
    d->column = nc_safe_atoi(comma + 1, 0);

    const char *after = rparen + 1;
    if (*after == ':')
        after++;
    while (*after == ' ')
        after++;

    if (nc_starts_with(after, "error TS")) {
        d->severity = DIAG_ERROR;
        const char *colon = strchr(after, ':');
        if (colon) {
            after = colon + 1;
            while (*after == ' ')
                after++;
        }
    }

    size_t mlen = strlen(after);
    while (mlen > 0 && (after[mlen - 1] == '\n' || after[mlen - 1] == '\r'))
        mlen--;
    set_str(&d->message, after, mlen);

    return true;
}

int diagnostic_parse_output(const char *output, diagnostic_entry_t *diags, int max_diags) {
    if (!output || !diags || max_diags <= 0)
        return 0;

    int count = 0;
    const char *line = output;

    while (*line && count < max_diags) {
        const char *eol = strchr(line, '\n');
        if (!eol)
            eol = line + strlen(line);

        size_t linelen = (size_t)(eol - line);
        char *buf = malloc(linelen + 1);
        if (!buf)
            break;
        memcpy(buf, line, linelen);
        buf[linelen] = '\0';

        diagnostic_entry_t d;
        diagnostic_init(&d);

        bool parsed = false;
        if (!parsed)
            parsed = parse_gcc_clang_line(buf, &d);
        if (!parsed)
            parsed = parse_python_traceback(buf, &d);
        if (!parsed)
            parsed = parse_tsc_line(buf, &d);
        if (!parsed)
            parsed = parse_eslint_line(buf, &d);

        if (parsed && d.message) {
            diags[count++] = d;
        } else {
            diagnostic_free(&d);
        }

        free(buf);
        line = (*eol) ? eol + 1 : eol;
    }

    return count;
}

static const struct {
    const char *pattern;
    const char *suggestion;
} fix_hints[] = {
    {"undeclared identifier",
     "Add a #include for the missing declaration or define the identifier"},
    {"implicit declaration", "Add #include for the function's header file"},
    {"incompatible pointer type",
     "Check the pointer types and add an explicit cast if appropriate"},
    {"unused variable", "Remove the variable or prefix with (void) to suppress the warning"},
    {"unused parameter", "Add (void)param; or use __attribute__((unused))"},
    {"expected ';'", "Add a semicolon at the indicated position"},
    {"expected '}'", "Add a closing brace to balance the block"},
    {"redefinition of", "Remove the duplicate definition or use a conditional guard"},
    {"no member named", "Check the struct definition; the field may be misspelled"},
    {"use of undeclared", "Ensure the variable/function is defined before use"},
    {"cannot find module", "Run npm install or check the import path"},
    {"ModuleNotFoundError", "Install the missing Python package with pip install"},
    {"ImportError", "Verify the module name and that it is installed"},
    {"SyntaxError", "Fix the syntax at the indicated line"},
    {"IndentationError", "Fix the indentation (use consistent spaces or tabs)"},
    {"TypeError", "Check the types of operands/arguments at the error site"},
    {"NameError", "The variable is not defined; check spelling or scope"},
    {"undefined reference", "Link the library that provides this symbol (-l flag)"},
    {"multiple definition", "Move the definition to a single .c file or use static"},
    {"control reaches end", "Add a return statement at the end of the function"},
    {NULL, NULL}};

const char *diagnostic_suggest_fix(const diagnostic_entry_t *d) {
    if (!d || !d->message)
        return NULL;
    for (int i = 0; fix_hints[i].pattern; i++) {
        if (strstr(d->message, fix_hints[i].pattern))
            return fix_hints[i].suggestion;
    }
    return NULL;
}

int diagnostic_annotate_suggestions(diagnostic_entry_t *diags, int count) {
    int annotated = 0;
    for (int i = 0; i < count; i++) {
        const char *hint = diagnostic_suggest_fix(&diags[i]);
        if (hint && !diags[i].suggestion) {
            diags[i].suggestion = nc_xstrdup(hint);
            annotated++;
        }
    }
    return annotated;
}

char *diagnostic_format(const diagnostic_entry_t *diags, int count) {
    if (!diags || count <= 0)
        return nc_xstrdup("No diagnostics.\n");

    strbuf_t sb;
    strbuf_init(&sb);

    const char *sev_names[] = {"error", "warning", "note", "hint"};

    for (int i = 0; i < count; i++) {
        const diagnostic_entry_t *d = &diags[i];
        char line[NC_MSG_MAX];

        const char *sname =
            (d->severity >= 0 && d->severity <= DIAG_HINT) ? sev_names[d->severity] : "?";

        if (d->file && d->line > 0 && d->column > 0) {
            snprintf(line, sizeof(line), "%s:%d:%d: %s: %s\n", d->file, d->line, d->column, sname,
                     d->message ? d->message : "");
        } else if (d->file && d->line > 0) {
            snprintf(line, sizeof(line), "%s:%d: %s: %s\n", d->file, d->line, sname,
                     d->message ? d->message : "");
        } else {
            snprintf(line, sizeof(line), "%s: %s\n", sname, d->message ? d->message : "");
        }
        strbuf_append_str(&sb, line);

        if (d->suggestion) {
            snprintf(line, sizeof(line), "  -> Hint: %s\n", d->suggestion);
            strbuf_append_str(&sb, line);
        }
    }

    return strbuf_detach(&sb);
}

int diagnostic_count_errors(const diagnostic_entry_t *diags, int count) {
    int errors = 0;
    for (int i = 0; i < count; i++) {
        if (diags[i].severity == DIAG_ERROR)
            errors++;
    }
    return errors;
}

int diagnostic_count_warnings(const diagnostic_entry_t *diags, int count) {
    int warnings = 0;
    for (int i = 0; i < count; i++) {
        if (diags[i].severity == DIAG_WARNING)
            warnings++;
    }
    return warnings;
}

char *diagnostic_summary(const diagnostic_entry_t *diags, int count) {
    int errors = diagnostic_count_errors(diags, count);
    int warnings = diagnostic_count_warnings(diags, count);
    char buf[NC_TAG_MAX];
    snprintf(buf, sizeof(buf), "%d error(s), %d warning(s) in %d diagnostic(s)", errors, warnings,
             count);
    return nc_xstrdup(buf);
}
