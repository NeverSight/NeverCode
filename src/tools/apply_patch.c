#include "nevercode.h"
#include "tools.h"

/*
 * ApplyPatch — applies patches in two formats:
 *
 *   1) Standard unified diff (--- a/ / +++ b/ / @@ ... @@)
 *   2) Structured marker format:
 *        *** Begin Patch
 *        *** Add File: <path>
 *        +line1
 *        *** Delete File: <path>
 *        *** Update File: <path>
 *        @@ optional context anchor
 *         context_line
 *        -old_line
 *        +new_line
 *        *** End Patch
 *
 * The marker format is friendlier for LLM generation (no line counts).
 * Both formats support multi-file patches.
 *
 * Input: { "patch": "<patch text>" }
 */

#define MARKER_BEGIN "*** Begin Patch"
#define MARKER_END "*** End Patch"
#define MARKER_ADD "*** Add File: "
#define MARKER_DELETE "*** Delete File: "
#define MARKER_UPDATE "*** Update File: "
#define MARKER_MOVE "*** Move to: "
#define MARKER_EOF "*** End of File"
#define MARKER_CTX "@@"

typedef struct {
    char **lines;
    int count;
    int cap;
} line_array_t;

static void la_init(line_array_t *la) {
    la->lines = NULL;
    la->count = 0;
    la->cap = 0;
}

static void la_free(line_array_t *la) {
    for (int i = 0; i < la->count; i++)
        free(la->lines[i]);
    free(la->lines);
    la->lines = NULL;
    la->count = la->cap = 0;
}

static void la_push(line_array_t *la, const char *line) {
    if (la->count >= la->cap) {
        int newcap = la->cap < 32 ? 32 : la->cap * 2;
        char **tmp = realloc(la->lines, newcap * sizeof(char *));
        if (!tmp)
            return;
        la->lines = tmp;
        la->cap = newcap;
    }
    la->lines[la->count++] = nc_xstrdup(line);
}

static void la_insert(line_array_t *la, int pos, const char *line) {
    if (pos < 0)
        pos = 0;
    if (pos > la->count)
        pos = la->count;
    if (la->count >= la->cap) {
        int newcap = la->cap < 32 ? 32 : la->cap * 2;
        char **tmp = realloc(la->lines, newcap * sizeof(char *));
        if (!tmp)
            return;
        la->lines = tmp;
        la->cap = newcap;
    }
    memmove(&la->lines[pos + 1], &la->lines[pos], (la->count - pos) * sizeof(char *));
    la->lines[pos] = nc_xstrdup(line);
    la->count++;
}

static void la_remove(line_array_t *la, int pos) {
    if (pos < 0 || pos >= la->count)
        return;
    free(la->lines[pos]);
    for (int i = pos; i < la->count - 1; i++)
        la->lines[i] = la->lines[i + 1];
    la->count--;
}

static void split_lines(const char *text, line_array_t *la) {
    la_init(la);
    if (!text || *text == '\0')
        return;
    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        if (!nl) {
            la_push(la, p);
            break;
        }
        int len = (int)(nl - p);
        char *line = malloc(len + 1);
        if (!line)
            break;
        memcpy(line, p, len);
        line[len] = '\0';
        la_push(la, line);
        free(line);
        p = nl + 1;
    }
}

static char *join_lines(const line_array_t *la) {
    strbuf_t sb;
    strbuf_init(&sb);
    for (int i = 0; i < la->count; i++) {
        strbuf_append_str(&sb, la->lines[i]);
        if (i < la->count - 1)
            strbuf_append(&sb, "\n", 1);
    }
    if (la->count > 0)
        strbuf_append(&sb, "\n", 1);
    return strbuf_detach(&sb);
}

/*
 * Gradual-relaxation context matching:
 *   Level 1: exact match
 *   Level 2: right-trim whitespace
 *   Level 3: both-side trim
 *   Level 4: unicode-normalize + trim (fancy quotes/dashes → ASCII)
 *
 * When eof_hint is set, we try matching at end-of-file first.
 */

static const char *rstrip(const char *s, char *buf, size_t sz) {
    size_t len = strlen(s);
    if (len >= sz)
        len = sz - 1;
    memcpy(buf, s, len);
    buf[len] = '\0';
    while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
    return buf;
}

static const char *strip_both(const char *s, char *buf, size_t sz) {
    while (*s == ' ' || *s == '\t')
        s++;
    rstrip(s, buf, sz);
    return buf;
}

static void normalize_line(const char *s, char *buf, size_t sz) {
    const char *src = s;
    size_t out = 0;
    while (*src == ' ' || *src == '\t')
        src++;

    while (*src && out < sz - 1) {
        unsigned char c = (unsigned char)*src;
        if (c == 0xC2 && (unsigned char)src[1] == 0xA0) {
            /* U+00A0 non-breaking space → normal space */
            buf[out++] = ' ';
            src += 2;
        } else if (c == 0xE2) {
            unsigned char b1 = (unsigned char)src[1];
            unsigned char b2 = (unsigned char)src[2];
            if (b1 == 0x80 && (b2 >= 0x90 && b2 <= 0x95)) {
                /* U+2010..U+2015 dashes → ASCII '-' */
                buf[out++] = '-';
                src += 3;
            } else if (b1 == 0x80 && (b2 >= 0x98 && b2 <= 0x9B)) {
                /* U+2018..U+201B fancy single quotes → '\'' */
                buf[out++] = '\'';
                src += 3;
            } else if (b1 == 0x80 && (b2 >= 0x9C && b2 <= 0x9F)) {
                /* U+201C..U+201F fancy double quotes → '"' */
                buf[out++] = '"';
                src += 3;
            } else if (b1 == 0x88 && b2 == 0x92) {
                /* U+2212 minus sign → '-' */
                buf[out++] = '-';
                src += 3;
            } else if (b1 == 0x80 &&
                       (b2 == 0x82 || b2 == 0x83 || b2 == 0x89 || b2 == 0x8A || b2 == 0xAF)) {
                /* U+2002/2003/2009/200A/202F various spaces → ' ' */
                buf[out++] = ' ';
                src += 3;
            } else {
                buf[out++] = (char)c;
                src++;
            }
        } else {
            buf[out++] = (char)c;
            src++;
        }
    }
    while (out > 0 && (buf[out - 1] == ' ' || buf[out - 1] == '\t' || buf[out - 1] == '\r'))
        out--;
    buf[out] = '\0';
}

typedef int (*line_cmp_fn)(const char *a, const char *b);

static int cmp_exact(const char *a, const char *b) {
    return strcmp(a, b);
}
static int cmp_rstrip(const char *a, const char *b) {
    char ba[NC_PATH_MAX], bb[NC_PATH_MAX];
    return strcmp(rstrip(a, ba, sizeof(ba)), rstrip(b, bb, sizeof(bb)));
}
static int cmp_trim(const char *a, const char *b) {
    char ba[NC_PATH_MAX], bb[NC_PATH_MAX];
    return strcmp(strip_both(a, ba, sizeof(ba)), strip_both(b, bb, sizeof(bb)));
}
static int cmp_normalize(const char *a, const char *b) {
    char ba[NC_PATH_MAX], bb[NC_PATH_MAX];
    normalize_line(a, ba, sizeof(ba));
    normalize_line(b, bb, sizeof(bb));
    return strcmp(ba, bb);
}

static int seek_with_cmp(const line_array_t *file, int start, const char **pattern, int plen,
                         line_cmp_fn cmp, int eof_hint) {
    if (plen == 0)
        return start;
    if (plen > file->count)
        return -1;
    int limit = file->count - plen;

    /* When eof_hint is set, try matching at end-of-file first. */
    if (eof_hint && file->count >= plen) {
        int eof_start = file->count - plen;
        int ok = 1;
        for (int j = 0; j < plen; j++) {
            if (cmp(file->lines[eof_start + j], pattern[j]) != 0) {
                ok = 0;
                break;
            }
        }
        if (ok)
            return eof_start;
    }

    for (int i = start; i <= limit; i++) {
        int ok = 1;
        for (int j = 0; j < plen; j++) {
            if (cmp(file->lines[i + j], pattern[j]) != 0) {
                ok = 0;
                break;
            }
        }
        if (ok)
            return i;
    }
    int best = -1;
    for (int i = 0; i < start && i <= limit; i++) {
        int ok = 1;
        for (int j = 0; j < plen; j++) {
            if (cmp(file->lines[i + j], pattern[j]) != 0) {
                ok = 0;
                break;
            }
        }
        if (ok && (best < 0 || abs(i - start) < abs(best - start)))
            best = i;
    }
    return best;
}

static int find_context_match_ex(const line_array_t *file, int start_hint, const char **ctx_lines,
                                 int ctx_count, int eof_hint) {
    if (ctx_count == 0)
        return start_hint;
    if (ctx_count > file->count)
        return -1;

    static const line_cmp_fn levels[] = {cmp_exact, cmp_rstrip, cmp_trim, cmp_normalize};
    for (int lvl = 0; lvl < 4; lvl++) {
        int pos = seek_with_cmp(file, start_hint, ctx_lines, ctx_count, levels[lvl], eof_hint);
        if (pos >= 0)
            return pos;
    }
    return -1;
}

static int find_context_match(const line_array_t *file, int start_hint, const char **ctx_lines,
                              int ctx_count) {
    return find_context_match_ex(file, start_hint, ctx_lines, ctx_count, 0);
}

typedef struct {
    int old_start, old_count;
    int new_start, new_count;
    char **lines; /* prefixed with ' ', '+', '-' */
    int num_lines;
} hunk_t;

static int parse_hunk_header(const char *line, hunk_t *h) {
    if (!nc_starts_with(line, "@@"))
        return -1;
    int os = 1, oc = 1, ns = 1, nc = 1;
    const char *p = line + 2;
    while (*p == ' ')
        p++;
    if (*p == '-')
        p++;
    sscanf(p, "%d,%d", &os, &oc);
    p = strchr(p, '+');
    if (p)
        sscanf(p + 1, "%d,%d", &ns, &nc);
    h->old_start = os;
    h->old_count = oc;
    h->new_start = ns;
    h->new_count = nc;
    return 0;
}

static int apply_single_file_patch(const char *path, const char *patch_text, strbuf_t *out) {
    line_array_t patch_lines;
    split_lines(patch_text, &patch_lines);
    if (patch_lines.count == 0) {
        la_free(&patch_lines);
        return 0;
    }

    int is_new_file = 0;
    int is_delete = 0;

    for (int i = 0; i < patch_lines.count; i++) {
        if (nc_starts_with(patch_lines.lines[i], "--- /dev/null"))
            is_new_file = 1;
        if (nc_starts_with(patch_lines.lines[i], "+++ /dev/null"))
            is_delete = 1;
    }

    line_array_t file_lines;
    la_init(&file_lines);

    if (is_delete) {
        file_history_snapshot(path);
        if (unlink(path) == 0) {
            vtrig_record_edit(path);
            strbuf_append_str(out, "Deleted: ");
            strbuf_append_str(out, path);
            strbuf_append_str(out, "\n");
        } else {
            strbuf_append_str(out, "Error deleting ");
            strbuf_append_str(out, path);
            strbuf_append_str(out, ": ");
            strbuf_append_str(out, strerror(errno));
            strbuf_append_str(out, "\n");
            la_free(&patch_lines);
            return 1;
        }
        la_free(&patch_lines);
        la_free(&file_lines);
        return 0;
    }

    if (!is_new_file) {
        char *content = strbuf_read_file_text(path, NULL);
        if (!content) {
            strbuf_append_str(out, "Error: file not found: ");
            strbuf_append_str(out, path);
            strbuf_append_str(out, "\n");
            la_free(&patch_lines);
            return 1;
        }
        split_lines(content, &file_lines);
        free(content);
    }

    file_history_snapshot(path);

    /* Collect hunks */
    hunk_t hunks[PATCH_MAX_HUNKS];
    int num_hunks = 0;
    int hunk_start = -1;

    for (int i = 0; i < patch_lines.count; i++) {
        if (nc_starts_with(patch_lines.lines[i], "@@")) {
            if (hunk_start >= 0 && num_hunks > 0) {
                hunks[num_hunks - 1].num_lines = i - hunk_start;
            }
            if (num_hunks >= PATCH_MAX_HUNKS)
                break;
            hunk_t *h = &hunks[num_hunks];
            if (parse_hunk_header(patch_lines.lines[i], h) < 0)
                continue;
            hunk_start = i + 1;
            h->lines = &patch_lines.lines[hunk_start];
            h->num_lines = 0;
            num_hunks++;
        }
    }
    if (num_hunks > 0 && hunk_start >= 0)
        hunks[num_hunks - 1].num_lines = patch_lines.count - hunk_start;

    /* Apply hunks in reverse order to maintain line numbering */
    int offset = 0;
    for (int hi = 0; hi < num_hunks; hi++) {
        hunk_t *h = &hunks[hi];
        int target_line = h->old_start - 1 + offset;

        /* Build context lines for matching */
        const char *ctx[64];
        int ctx_count = 0;
        for (int j = 0; j < h->num_lines && ctx_count < 64; j++) {
            if (h->lines[j][0] == ' ' || h->lines[j][0] == '-')
                ctx[ctx_count++] = h->lines[j] + 1;
        }

        int match_pos = target_line;
        if (!is_new_file && ctx_count > 0) {
            int found = find_context_match(&file_lines, target_line, ctx, ctx_count);
            if (found >= 0)
                match_pos = found;
        }

        int cur = match_pos;
        for (int j = 0; j < h->num_lines; j++) {
            char prefix = h->lines[j][0];
            const char *content = h->lines[j] + 1;
            if (prefix == ' ') {
                cur++;
            } else if (prefix == '-') {
                if (cur < file_lines.count)
                    la_remove(&file_lines, cur);
                offset--;
            } else if (prefix == '+') {
                la_insert(&file_lines, cur, content);
                cur++;
                offset++;
            }
        }
    }

    /* Write result */
    if (is_new_file) {
        char parent[NC_PATH_MAX];
        nc_strlcpy(parent, path, sizeof(parent));
        char *sl = strrchr(parent, '/');
        if (sl) {
            *sl = '\0';
            nc_mkdir_p(parent);
        }
    }

    char *result = join_lines(&file_lines);
    FILE *f = fopen(path, "w");
    if (!f) {
        strbuf_append_str(out, "Error writing ");
        strbuf_append_str(out, path);
        strbuf_append_str(out, ": ");
        strbuf_append_str(out, strerror(errno));
        strbuf_append_str(out, "\n");
        free(result);
        la_free(&file_lines);
        la_free(&patch_lines);
        return 1;
    }
    int we = fputs(result, f);
    int fe = fflush(f);
    fclose(f);
    if (we == EOF || fe) {
        strbuf_appendf(out, "Error: short write to %s\n", path);
        free(result);
        la_free(&file_lines);
        la_free(&patch_lines);
        return 1;
    }

    {
        int added = 0, removed = 0;
        for (int hi = 0; hi < num_hunks; hi++)
            for (int j = 0; j < hunks[hi].num_lines; j++) {
                if (hunks[hi].lines[j][0] == '+')
                    added++;
                else if (hunks[hi].lines[j][0] == '-')
                    removed++;
            }
        turn_diff_record(file_history_current_turn(), path, added, removed);
    }

    strbuf_append_str(out, is_new_file ? "Created: " : "Modified: ");
    strbuf_append_str(out, path);
    char stats[NC_FMT_MAX];
    snprintf(stats, sizeof(stats), " (%d hunks applied)\n", num_hunks);
    strbuf_append_str(out, stats);

    free(result);
    la_free(&file_lines);
    la_free(&patch_lines);
    vtrig_record_edit(path);
    return 0;
}

void tool_apply_patch_register(void) {
    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON *props = cJSON_AddObjectToObject(schema, "properties");
    cJSON *patch_prop = cJSON_AddObjectToObject(props, "patch");
    cJSON_AddStringToObject(patch_prop, "type", "string");
    cJSON_AddStringToObject(
        patch_prop, "description",
        "A patch to apply. Accepts two formats: (1) unified diff "
        "(--- a/ +++ b/ @@ @@), (2) structured markers "
        "(*** Begin Patch / *** Add File / *** Update File / *** Delete File / *** End Patch). "
        "Both support multi-file patches with fuzzy context matching.");
    cJSON *req = cJSON_AddArrayToObject(schema, "required");
    cJSON_AddItemToArray(req, cJSON_CreateString("patch"));

    tool_def_t t = {
        .name = "ApplyPatch",
        .description = "Apply a unified diff patch to create, modify, or delete files. "
                       "Preferred for multi-file changes. More efficient than multiple "
                       "FileEdit calls when making related changes across files.",
        .input_schema = schema,
        .execute = apply_patch_execute,
        .check_permission = NULL,
        .read_only = false,
        .concurrency_safe = false,
        .destructive = true,
    };
    tools_register(&t);
}

/* ── Structured marker format (*** Begin Patch / *** End Patch) ── */

static const char *trim_lead(const char *s) {
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

static int marker_match(const char *line, const char *marker) {
    return nc_starts_with(trim_lead(line), marker);
}

static const char *marker_payload(const char *line, const char *marker) {
    return trim_lead(line) + strlen(marker);
}

static int apply_marker_add(const char *path, const char **lines, int nlines, strbuf_t *out) {
    file_history_snapshot(path);
    char parent[NC_PATH_MAX];
    nc_strlcpy(parent, path, sizeof(parent));
    char *sl = strrchr(parent, '/');
    if (sl) {
        *sl = '\0';
        nc_mkdir_p(parent);
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        strbuf_appendf(out, "Error creating %s: %s\n", path, strerror(errno));
        return 1;
    }
    for (int i = 0; i < nlines; i++) {
        const char *content = lines[i];
        if (content[0] == '+')
            content++;
        fputs(content, f);
        fputc('\n', f);
    }
    if (fflush(f)) {
        fclose(f);
        strbuf_appendf(out, "Error: write failed for %s\n", path);
        return 1;
    }
    fclose(f);
    strbuf_appendf(out, "Created: %s (%d lines)\n", path, nlines);

    int added = nlines;
    turn_diff_record(file_history_current_turn(), path, added, 0);
    vtrig_record_edit(path);
    return 0;
}

static int apply_marker_delete(const char *path, strbuf_t *out) {
    file_history_snapshot(path);
    if (unlink(path) == 0) {
        vtrig_record_edit(path);
        strbuf_appendf(out, "Deleted: %s\n", path);
        return 0;
    }
    strbuf_appendf(out, "Error deleting %s: %s\n", path, strerror(errno));
    return 1;
}

static int apply_marker_update(const char *path, const char *move_path, const char **plines,
                               int nlines, strbuf_t *out) {
    char *content = strbuf_read_file_text(path, NULL);
    if (!content) {
        strbuf_appendf(out, "Error: file not found: %s\n", path);
        return 1;
    }

    file_history_snapshot(path);
    if (move_path && strcmp(path, move_path) != 0)
        file_history_snapshot(move_path);

    line_array_t file;
    split_lines(content, &file);
    free(content);

    int cursor = 0;
    int added = 0, removed = 0;
    int i = 0;

    int eof_hint = 0;

    while (i < nlines) {
        if (marker_match(plines[i], MARKER_CTX)) {
            const char *raw = marker_payload(plines[i], MARKER_CTX);
            while (*raw == ' ')
                raw++;
            if (*raw) {
                for (int k = cursor; k < file.count; k++) {
                    char trimmed[NC_PATH_MAX];
                    strip_both(file.lines[k], trimmed, sizeof(trimmed));
                    if (strstr(trimmed, raw)) {
                        cursor = k + 1;
                        break;
                    }
                }
            }
            i++;
            continue;
        }
        if (marker_match(plines[i], MARKER_EOF)) {
            eof_hint = 1;
            i++;
            continue;
        }

        /* Collect old/new lines for this chunk */
        const char *old_ctx[PATCH_MAX_HUNKS];
        int old_count = 0;
        const char *new_lines_buf[PATCH_MAX_HUNKS];
        int new_count = 0;
        int chunk_start = i;

        while (i < nlines && plines[i][0] != '\0' && !marker_match(plines[i], MARKER_CTX) &&
               !marker_match(plines[i], MARKER_EOF) && !marker_match(plines[i], MARKER_ADD) &&
               !marker_match(plines[i], MARKER_DELETE) && !marker_match(plines[i], MARKER_UPDATE) &&
               !marker_match(plines[i], MARKER_END)) {
            char prefix = plines[i][0];
            if (prefix == ' ' || prefix == '-') {
                old_ctx[old_count++] = plines[i] + 1;
            }
            if (prefix == ' ' || prefix == '+') {
                new_lines_buf[new_count++] = plines[i] + 1;
            }
            i++;
        }

        if (old_count == 0 && new_count == 0)
            continue;

        int match = find_context_match_ex(&file, cursor, old_ctx, old_count, eof_hint);
        if (match < 0)
            match = cursor;
        eof_hint = 0;

        /* Remove old lines */
        for (int k = 0; k < old_count; k++) {
            if (match < file.count)
                la_remove(&file, match);
            removed++;
        }
        /* Insert new lines */
        for (int k = 0; k < new_count; k++) {
            la_insert(&file, match + k, new_lines_buf[k]);
            added++;
        }
        cursor = match + new_count;
        (void)chunk_start;
    }

    const char *write_path = move_path ? move_path : path;

    if (move_path) {
        char parent[NC_PATH_MAX];
        nc_strlcpy(parent, move_path, sizeof(parent));
        char *sl = strrchr(parent, '/');
        if (sl) {
            *sl = '\0';
            nc_mkdir_p(parent);
        }
    }

    char *result = join_lines(&file);
    FILE *f = fopen(write_path, "w");
    if (!f) {
        strbuf_appendf(out, "Error writing %s: %s\n", write_path, strerror(errno));
        free(result);
        la_free(&file);
        return 1;
    }
    int mwe = fputs(result, f);
    int mfe = fflush(f);
    fclose(f);
    if (mwe == EOF || mfe) {
        strbuf_appendf(out, "Error: short write to %s\n", write_path);
        free(result);
        la_free(&file);
        return 1;
    }
    free(result);

    if (move_path && strcmp(path, move_path) != 0)
        unlink(path);

    turn_diff_record(file_history_current_turn(), write_path, added, removed);
    strbuf_appendf(out, "Modified: %s (+%d/-%d)\n", write_path, added, removed);
    vtrig_record_edit(write_path);

    la_free(&file);
    return 0;
}

static bool is_marker_format(const char *patch) {
    const char *p = patch;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    return nc_starts_with(p, MARKER_BEGIN);
}

static int apply_marker_patch(const char *patch, strbuf_t *out) {
    line_array_t lines;
    split_lines(patch, &lines);
    int errors = 0;
    int i = 0;

    /* Skip to *** Begin Patch (lenient: tolerate leading whitespace) */
    while (i < lines.count && !marker_match(lines.lines[i], MARKER_BEGIN))
        i++;
    if (i < lines.count)
        i++;

    while (i < lines.count) {
        char *line = lines.lines[i];

        if (marker_match(line, MARKER_END))
            break;

        if (marker_match(line, MARKER_ADD)) {
            const char *path = marker_payload(line, MARKER_ADD);
            i++;
            const char *add_lines[PATCH_MAX_CHUNK_LINES];
            int add_count = 0;
            while (i < lines.count && lines.lines[i][0] == '+' &&
                   add_count < PATCH_MAX_CHUNK_LINES) {
                add_lines[add_count++] = lines.lines[i];
                i++;
            }
            errors += apply_marker_add(path, add_lines, add_count, out);
            continue;
        }

        if (marker_match(line, MARKER_DELETE)) {
            const char *path = marker_payload(line, MARKER_DELETE);
            errors += apply_marker_delete(path, out);
            i++;
            continue;
        }

        if (marker_match(line, MARKER_UPDATE)) {
            const char *path = marker_payload(line, MARKER_UPDATE);
            i++;
            char *move_path = NULL;
            if (i < lines.count && marker_match(lines.lines[i], MARKER_MOVE)) {
                move_path = (char *)marker_payload(lines.lines[i], MARKER_MOVE);
                i++;
            }
            const char *update_lines[PATCH_MAX_CHUNK_LINES];
            int update_count = 0;
            while (i < lines.count && !marker_match(lines.lines[i], MARKER_ADD) &&
                   !marker_match(lines.lines[i], MARKER_DELETE) &&
                   !marker_match(lines.lines[i], MARKER_UPDATE) &&
                   !marker_match(lines.lines[i], MARKER_END) &&
                   update_count < PATCH_MAX_CHUNK_LINES) {
                update_lines[update_count++] = lines.lines[i];
                i++;
            }
            errors += apply_marker_update(path, move_path, update_lines, update_count, out);
            continue;
        }

        i++;
    }

    la_free(&lines);
    return errors;
}

int apply_patch_execute(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *patch = nc_cjson_str((cJSON *)input, "patch", NULL);
    if (!patch || *patch == '\0') {
        *output = nc_xstrdup("Error: 'patch' parameter is required");
        return 1;
    }

    /* Detect and dispatch structured marker format */
    if (is_marker_format(patch)) {
        strbuf_t out;
        strbuf_init(&out);
        int errors = apply_marker_patch(patch, &out);
        if (out.len == 0)
            strbuf_append_str(&out, errors ? "Patch failed\n" : "Patch applied\n");
        *output = strbuf_detach(&out);
        return errors;
    }

    strbuf_t out;
    strbuf_init(&out);
    int errors = 0;

    /* Parse multi-file unified diff: split on "diff --git" or "--- " boundaries */
    const char *p = patch;
    char current_path[NC_PATH_MAX] = {0};
    strbuf_t current_patch;
    strbuf_init(&current_patch);
    int have_patch = 0;

    while (*p) {
        const char *nl = strchr(p, '\n');
        int line_len = nl ? (int)(nl - p) : (int)strlen(p);
        char line[NC_PATH_MAX];
        if (line_len >= (int)sizeof(line))
            line_len = (int)sizeof(line) - 1;
        memcpy(line, p, line_len);
        line[line_len] = '\0';

        if (nc_starts_with(line, "diff --git ")) {
            /* Flush previous file patch */
            if (have_patch && current_path[0]) {
                errors += apply_single_file_patch(current_path, current_patch.data, &out);
                strbuf_clear(&current_patch);
            }
            have_patch = 1;
            current_path[0] = '\0';
            /* Extract path from "diff --git a/path b/path" */
            const char *b_part = strstr(line + 11, " b/");
            if (b_part) {
                nc_strlcpy(current_path, b_part + 3, sizeof(current_path));
            }
        } else if (nc_starts_with(line, "+++ b/")) {
            nc_strlcpy(current_path, line + 6, sizeof(current_path));
        } else if (nc_starts_with(line, "--- a/") && current_path[0] == '\0') {
            nc_strlcpy(current_path, line + 6, sizeof(current_path));
        } else if (nc_starts_with(line, "+++ /dev/null")) {
            /* Keep current_path from --- line for deletion */
        }

        strbuf_append_str(&current_patch, line);
        strbuf_append(&current_patch, "\n", 1);
        p = nl ? nl + 1 : p + strlen(p);
    }

    /* Flush last file */
    if (have_patch && current_path[0]) {
        errors += apply_single_file_patch(current_path, current_patch.data, &out);
    } else if (!have_patch) {
        /* Single file patch without "diff --git" header */
        /* Try +++ first, fall back to --- for deletions (+++ /dev/null) */
        const char *plus = strstr(patch, "+++ ");
        const char *minus = strstr(patch, "--- ");
        const char *src = plus;
        int is_del = 0;

        if (plus && nc_starts_with(plus + 4, "/dev/null")) {
            src = minus;
            is_del = 1;
        }

        if (src) {
            const char *path_start = src + 4;
            if (nc_starts_with(path_start, "b/"))
                path_start += 2;
            if (nc_starts_with(path_start, "a/"))
                path_start += 2;
            const char *path_end = strchr(path_start, '\n');
            if (!path_end)
                path_end = path_start + strlen(path_start);
            int plen = (int)(path_end - path_start);
            if (plen > 0 && plen < (int)sizeof(current_path) &&
                !nc_starts_with(path_start, "/dev/null")) {
                memcpy(current_path, path_start, plen);
                current_path[plen] = '\0';
                while (plen > 0 &&
                       (current_path[plen - 1] == '\t' || current_path[plen - 1] == ' '))
                    current_path[--plen] = '\0';
                errors += apply_single_file_patch(current_path, patch, &out);
            }
        }
        (void)is_del;
    }

    strbuf_free(&current_patch);

    if (out.len == 0) {
        strbuf_append_str(&out,
                          errors ? "Patch application failed\n" : "Patch applied successfully\n");
    }

    *output = strbuf_detach(&out);
    return errors;
}
