/*
 * multi_edit.c — Apply multiple find-and-replace edits to a single file
 *                in one tool invocation, reducing model round-trips.
 *
 * Uses the same 6-level progressive fuzzy matching as the Edit tool:
 *   L1 exact, L2 rstrip, L3 trim, L4 ws-collapse, L5 indent-flex, L6 unicode-norm
 */

#include "tools.h"
#include "nevercode.h"

/* MULTI_EDIT_MAX_OPS defined in defaults.h */

static const char *SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"Absolute file path to edit\"},"
    "\"edits\":{\"type\":\"array\",\"items\":{\"type\":\"object\","
    "\"properties\":{"
    "\"old_string\":{\"type\":\"string\",\"description\":\"Text to find\"},"
    "\"new_string\":{\"type\":\"string\",\"description\":\"Replacement text\"},"
    "\"replace_all\":{\"type\":\"boolean\",\"description\":\"Replace all occurrences\"}"
    "},\"required\":[\"old_string\",\"new_string\"]},"
    "\"description\":\"Array of sequential edit operations\"}"
    "},"
    "\"required\":[\"path\",\"edits\"]"
    "}";

static char *apply_single_edit(char *content, const char *old_str, const char *new_str,
                               bool replace_all, int *count, int *out_level) {
    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);
    *count = 0;

    if (old_len == 0)
        return content;

    size_t off = 0, mlen = 0;
    int lvl = 0;
    if (!edit_fuzzy_locate(content, old_str, old_len, &off, &mlen, &lvl))
        return content;
    *out_level = lvl;

    if (!replace_all) {
        const char *rest = content + off + mlen;
        size_t off2 = 0, mlen2 = 0;
        int lvl2 = 0;
        if (edit_fuzzy_locate(rest, old_str, old_len, &off2, &mlen2, &lvl2) && lvl2 == lvl) {
            *count = -1;
            return content;
        }
    }

    strbuf_t sb;
    strbuf_init(&sb);
    const char *p = content;
    int replaced = 0;

    while (*p) {
        size_t o = 0, m = 0;
        int l = 0;
        if (!edit_fuzzy_locate(p, old_str, old_len, &o, &m, &l) || l != lvl ||
            (replaced > 0 && !replace_all)) {
            strbuf_append_str(&sb, p);
            break;
        }
        strbuf_append(&sb, p, o);
        strbuf_append(&sb, new_str, new_len);
        p += o + m;
        replaced++;
    }

    *count = replaced;
    free(content);
    return strbuf_detach(&sb);
}

static int multi_edit_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *path = nc_cjson_str((cJSON *)input, "path", NULL);
    cJSON *edits = cJSON_GetObjectItem((cJSON *)input, "edits");

    if (!path || !edits || !cJSON_IsArray(edits)) {
        *output = nc_xstrdup("error: missing required parameters (path, edits)");
        return 1;
    }
    if (!sandbox_check_path(path)) {
        char msg[NC_MSG_MAX];
        snprintf(msg, sizeof(msg), "error: path outside project scope: %s", path);
        *output = nc_xstrdup(msg);
        return 1;
    }

    int num_edits = cJSON_GetArraySize(edits);
    if (num_edits == 0) {
        *output = nc_xstrdup("error: edits array is empty");
        return 1;
    }
    if (num_edits > MULTI_EDIT_MAX_OPS) {
        char msg[NC_MSG_MAX];
        snprintf(msg, sizeof(msg), "error: too many edits (%d), max %d", num_edits,
                 MULTI_EDIT_MAX_OPS);
        *output = nc_xstrdup(msg);
        return 1;
    }

    size_t file_len;
    char *content = strbuf_read_file_text(path, &file_len);
    if (!content) {
        char msg[NC_MSG_MAX];
        snprintf(msg, sizeof(msg), "error: cannot read: %s", path);
        *output = nc_xstrdup(msg);
        return 1;
    }

    strbuf_t sb;
    strbuf_init(&sb);
    int total_applied = 0;
    int total_failed = 0;

    for (int i = 0; i < num_edits; i++) {
        cJSON *edit = cJSON_GetArrayItem(edits, i);
        const char *old_str = nc_cjson_str(edit, "old_string", NULL);
        const char *new_str = nc_cjson_str(edit, "new_string", NULL);
        cJSON *ra = cJSON_GetObjectItem(edit, "replace_all");
        bool replace_all = ra && cJSON_IsTrue(ra);

        if (!old_str || !new_str) {
            strbuf_appendf(&sb, "edit %d: skipped (missing old_string/new_string)\n", i + 1);
            total_failed++;
            continue;
        }
        if (!old_str[0]) {
            strbuf_appendf(&sb, "edit %d: skipped (empty old_string)\n", i + 1);
            total_failed++;
            continue;
        }

        int count = 0;
        int mlvl = 0;
        content = apply_single_edit(content, old_str, new_str, replace_all, &count, &mlvl);
        if (count < 0) {
            strbuf_appendf(&sb, "edit %d: failed (old_string not unique, found multiple matches)\n",
                           i + 1);
            total_failed++;
        } else if (count == 0) {
            strbuf_appendf(&sb, "edit %d: failed (old_string not found)\n", i + 1);
            total_failed++;
        } else {
            if (mlvl > 0)
                strbuf_appendf(&sb, "edit %d: %d replacement(s) applied (via %s)\n", i + 1, count,
                               edit_level_names[mlvl]);
            else
                strbuf_appendf(&sb, "edit %d: %d replacement(s) applied\n", i + 1, count);
            total_applied++;
        }
    }

    if (total_applied > 0) {
        FILE *f = fopen(path, "w");
        if (f) {
            size_t clen = strlen(content);
            size_t written = fwrite(content, 1, clen, f);
            int flush_err = fflush(f);
            fclose(f);
            if (written < clen || flush_err) {
                strbuf_appendf(&sb, "error: short write to %s (%zu/%zu bytes)\n", path, written,
                               clen);
                free(content);
                *output = strbuf_detach(&sb);
                return 1;
            }
            frc_invalidate(path);
            vtrig_record_edit(path);

            int added = 0, removed = 0;
            for (const char *c = content; *c; c++)
                if (*c == '\n')
                    added++;
            added++;
            turn_diff_record(file_history_current_turn(), path, added, removed);
        } else {
            strbuf_appendf(&sb, "error: failed to write file: %s\n", strerror(errno));
            free(content);
            *output = strbuf_detach(&sb);
            return 1;
        }
    }
    free(content);

    strbuf_appendf(&sb, "\n%d/%d edits applied to %s", total_applied, num_edits, path);
    if (total_failed > 0)
        strbuf_appendf(&sb, " (%d failed)", total_failed);

    *output = strbuf_detach(&sb);
    return total_failed > 0 && total_applied == 0 ? 1 : 0;
}

static tool_def_t multi_edit_tool;

void tool_multi_edit_register(void) {
    memset(&multi_edit_tool, 0, sizeof(multi_edit_tool));
    multi_edit_tool.name = "MultiEdit";
    multi_edit_tool.description =
        "Apply multiple find-and-replace edits to a single file in one call. "
        "Each edit specifies old_string and new_string. Edits are applied "
        "sequentially, so later edits see the result of earlier ones. "
        "Use this instead of multiple Edit calls when making several changes "
        "to the same file.";
    multi_edit_tool.input_schema = cJSON_Parse(SCHEMA);
    multi_edit_tool.execute = multi_edit_exec;
    multi_edit_tool.read_only = false;
    multi_edit_tool.concurrency_safe = false;
    multi_edit_tool.destructive = true;
    tools_register(&multi_edit_tool);
}
