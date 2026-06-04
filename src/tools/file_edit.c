/*
 * file_edit.c — Multi-strategy find-and-replace with progressive fuzzy matching.
 *
 * Six matching levels are tried in order until one succeeds:
 *   L1: exact match
 *   L2: right-trim trailing whitespace per line
 *   L3: trim both leading and trailing whitespace per line
 *   L4: collapse runs of whitespace to single space
 *   L5: ignore indentation differences entirely
 *   L6: normalize Unicode (smart quotes, em-dashes, NBSP → ASCII)
 */

#include "tools.h"
#include "nevercode.h"

static const char *SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"File path to edit\"},"
    "\"old_string\":{\"type\":\"string\",\"description\":\"Exact string to find\"},"
    "\"new_string\":{\"type\":\"string\",\"description\":\"Replacement string\"},"
    "\"replace_all\":{\"type\":\"boolean\",\"description\":\"Replace all occurrences\"}"
    "},"
    "\"required\":[\"path\",\"old_string\",\"new_string\"]"
    "}";

static int file_edit_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *path = nc_cjson_str((cJSON *)input, "path", NULL);
    const char *old_str = nc_cjson_str((cJSON *)input, "old_string", NULL);
    const char *new_str = nc_cjson_str((cJSON *)input, "new_string", NULL);
    if (!path || !old_str || !new_str) {
        *output = nc_xstrdup("error: missing required parameters");
        return 1;
    }
    if (!sandbox_check_path(path)) {
        char msg[NC_MSG_MAX];
        snprintf(msg, sizeof(msg), "error: path outside project scope: %s", path);
        *output = nc_xstrdup(msg);
        return 1;
    }

    cJSON *ra = cJSON_GetObjectItem((cJSON *)input, "replace_all");
    bool replace_all = ra && cJSON_IsTrue(ra);

    size_t file_len;
    char *content = strbuf_read_file_text(path, &file_len);
    if (!content) {
        char msg[NC_MSG_MAX];
        snprintf(msg, sizeof(msg), "error: cannot read: %s", path);
        *output = nc_xstrdup(msg);
        return 1;
    }

    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);

    if (old_len == 0) {
        free(content);
        *output = nc_xstrdup("error: old_string cannot be empty");
        return 1;
    }

    /* Locate the first match using progressive fuzzy matching */
    size_t first_off = 0, first_mlen = 0;
    int match_level = 0;
    if (!edit_fuzzy_locate(content, old_str, old_len, &first_off, &first_mlen, &match_level)) {
        free(content);
        *output = nc_xstrdup("error: old_string not found in file");
        return 1;
    }

    /* Uniqueness check (non-replace_all mode) */
    if (!replace_all) {
        int total = edit_fuzzy_count(content, old_str, old_len, match_level);
        if (total > 1) {
            free(content);
            char msg[NC_LABEL_MAX];
            snprintf(msg, sizeof(msg),
                     "error: old_string is not unique in file (found %d matches via %s). "
                     "Add more context or use replace_all:true",
                     total, edit_level_names[match_level]);
            *output = nc_xstrdup(msg);
            return 1;
        }
    }

    /* Perform replacement(s) */
    strbuf_t sb;
    strbuf_init(&sb);
    int count = 0;
    const char *p = content;

    if (match_level == 0) {
        while (*p) {
            char *found = strstr(p, old_str);
            if (!found || (count > 0 && !replace_all)) {
                strbuf_append_str(&sb, p);
                break;
            }
            strbuf_append(&sb, p, found - p);
            strbuf_append(&sb, new_str, new_len);
            p = found + old_len;
            count++;
        }
    } else {
        while (*p) {
            size_t off = 0, mlen = 0;
            int lvl = 0;
            if (!edit_fuzzy_locate(p, old_str, old_len, &off, &mlen, &lvl) || lvl != match_level ||
                (count > 0 && !replace_all)) {
                strbuf_append_str(&sb, p);
                break;
            }
            strbuf_append(&sb, p, off);
            strbuf_append(&sb, new_str, new_len);
            p += off + mlen;
            count++;
        }
    }

    free(content);

    FILE *f = fopen(path, "w");
    if (!f) {
        strbuf_free(&sb);
        char msg[NC_MSG_MAX];
        snprintf(msg, sizeof(msg), "error: cannot write: %s (%s)", path, strerror(errno));
        *output = nc_xstrdup(msg);
        return 1;
    }
    size_t written = fwrite(sb.data, 1, sb.len, f);
    int flush_err = fflush(f);
    fclose(f);
    if (written < sb.len || flush_err) {
        char errmsg[NC_MSG_MAX];
        snprintf(errmsg, sizeof(errmsg), "error: short write to %s (%zu/%zu bytes)", path, written,
                 sb.len);
        strbuf_free(&sb);
        *output = nc_xstrdup(errmsg);
        return 1;
    }
    strbuf_free(&sb);
    frc_invalidate(path);

    /* Generate a diff snippet showing the change context */
    strbuf_t out;
    strbuf_init(&out);
    char hdr[NC_MSG_MAX];
    if (match_level > 0)
        snprintf(hdr, sizeof(hdr), "Replaced %d occurrence(s) in %s (matched via %s)\n", count,
                 path, edit_level_names[match_level]);
    else
        snprintf(hdr, sizeof(hdr), "Replaced %d occurrence(s) in %s\n", count, path);
    strbuf_append_str(&out, hdr);

    /* Show compact diff: old -> new with a few context lines */
    const char *old_lines = old_str;
    const char *new_lines = new_str;
    int old_lc = 1, new_lc = 1;
    for (const char *c = old_lines; *c; c++)
        if (*c == '\n')
            old_lc++;
    for (const char *c = new_lines; *c; c++)
        if (*c == '\n')
            new_lc++;

    if (old_lc <= NC_EDIT_DIFF_LINE_CAP && new_lc <= NC_EDIT_DIFF_LINE_CAP) {
        strbuf_appendf(&out, "--- a/%s\n+++ b/%s\n", path, path);
        const char *sides[] = {old_str, new_str};
        const char prefixes[] = {'-', '+'};
        for (int si = 0; si < 2; si++) {
            const char *lp = sides[si];
            while (*lp) {
                strbuf_appendf(&out, "%c", prefixes[si]);
                const char *nl = strchr(lp, '\n');
                if (nl) {
                    strbuf_append(&out, lp, nl - lp + 1);
                    lp = nl + 1;
                } else {
                    strbuf_append_str(&out, lp);
                    strbuf_append(&out, "\n", 1);
                    break;
                }
            }
        }
    }

    turn_diff_record(file_history_current_turn(), path, new_lc * count, old_lc * count);

    *output = strbuf_detach(&out);
    vtrig_record_edit(path);
    return 0;
}

static tool_def_t file_edit_tool;

void tool_file_edit_register(void) {
    file_edit_tool.name = "Edit";
    file_edit_tool.description = "Edit a file by replacing an exact string with a new string. "
                                 "The old_string must be unique unless replace_all is true.";
    file_edit_tool.input_schema = cJSON_Parse(SCHEMA);
    file_edit_tool.execute = file_edit_exec;
    file_edit_tool.read_only = false;
    file_edit_tool.concurrency_safe = false;
    file_edit_tool.destructive = false;
    tools_register(&file_edit_tool);
}
