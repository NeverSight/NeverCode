#include "nevercode.h"

typedef struct {
    const char *name;
    const char *input_json;
    const char *output_json;
} tool_batch_entry_t;

static void summarize_single(strbuf_t *sb, const tool_batch_entry_t *e) {
    const char *nm = e->name, *inp = e->input_json;

    if (!strcmp(nm, "bash") || !strcmp(nm, "shell")) {
        cJSON *j = cJSON_Parse(inp);
        const char *cmd = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "command")) : NULL;
        if (cmd) {
            char sc[NC_FMT_MAX];
            size_t l = strlen(cmd);
            if (l > TOOL_SUMMARY_TRUNC_MAX) {
                memcpy(sc, cmd, TOOL_SUMMARY_TRUNC_MAX);
                memcpy(sc + TOOL_SUMMARY_TRUNC_MAX, "...", 4);
            } else
                nc_strlcpy(sc, cmd, sizeof(sc));
            strbuf_appendf(sb, "Ran `%s`", sc);
        } else {
            strbuf_append_str(sb, "Ran shell command");
        }
        cJSON_Delete(j);
        return;
    }

    if (!strcmp(nm, "file_read") || !strcmp(nm, "read") || !strcmp(nm, "file_write") ||
        !strcmp(nm, "write") || !strcmp(nm, "file_edit") || !strcmp(nm, "edit")) {
        const char *verb = !strcmp(nm, "file_read") || !strcmp(nm, "read")     ? "Read"
                           : !strcmp(nm, "file_write") || !strcmp(nm, "write") ? "Wrote"
                                                                               : "Edited";
        cJSON *j = cJSON_Parse(inp);
        const char *p = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "path")) : NULL;
        if (!p)
            p = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "file_path")) : NULL;
        if (p) {
            const char *b = strrchr(p, '/');
            strbuf_appendf(sb, "%s %s", verb, b ? b + 1 : p);
        } else {
            strbuf_appendf(sb, "%s file", verb);
        }
        cJSON_Delete(j);
        return;
    }

    if (!strcmp(nm, "grep")) {
        cJSON *j = cJSON_Parse(inp);
        const char *pt = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "pattern")) : NULL;
        if (pt)
            strbuf_appendf(sb, "Searched for \"%s\"", pt);
        else
            strbuf_append_str(sb, "Searched codebase");
        cJSON_Delete(j);
        return;
    }

    if (!strcmp(nm, "glob")) {
        strbuf_append_str(sb, "Listed matching files");
        return;
    }
    if (!strcmp(nm, "web_search")) {
        strbuf_append_str(sb, "Searched the web");
        return;
    }
    if (!strcmp(nm, "web_fetch")) {
        strbuf_append_str(sb, "Fetched web page");
        return;
    }
    if (!strcmp(nm, "apply_patch")) {
        strbuf_append_str(sb, "Applied patch");
        return;
    }
    if (!strcmp(nm, "git_diff")) {
        strbuf_append_str(sb, "Viewed git diff");
        return;
    }

    char lb[NC_FMT_MAX];
    nc_strlcpy(lb, nm, sizeof(lb));
    for (char *q = lb; *q; q++)
        if (*q == '_')
            *q = ' ';
    if (lb[0])
        lb[0] = (char)toupper((unsigned char)lb[0]);
    strbuf_appendf(sb, "Used %s", lb);
}

static char *summarize_heuristic(const tool_batch_entry_t *e, int n) {
    if (n <= 0)
        return NULL;
    strbuf_t sb;
    strbuf_init(&sb);

    if (n == 1) {
        summarize_single(&sb, &e[0]);
    } else {
        int rd = 0, wr = 0, ed = 0, sh = 0, se = 0, ot = 0;
        for (int i = 0; i < n; i++) {
            const char *nm = e[i].name;
            if (!strcmp(nm, "file_read") || !strcmp(nm, "read"))
                rd++;
            else if (!strcmp(nm, "file_write") || !strcmp(nm, "write"))
                wr++;
            else if (!strcmp(nm, "file_edit") || !strcmp(nm, "edit"))
                ed++;
            else if (!strcmp(nm, "bash") || !strcmp(nm, "shell"))
                sh++;
            else if (!strcmp(nm, "grep") || !strcmp(nm, "glob") || !strcmp(nm, "web_search"))
                se++;
            else
                ot++;
        }
        bool first = true;
        if (ed) {
            strbuf_appendf(&sb, "Edited %d file%s", ed, ed > 1 ? "s" : "");
            first = false;
        }
        if (wr) {
            strbuf_appendf(&sb, "%swrote %d file%s", first ? "W" : ", w", wr, wr > 1 ? "s" : "");
            first = false;
        }
        if (rd) {
            strbuf_appendf(&sb, "%sread %d file%s", first ? "R" : ", r", rd, rd > 1 ? "s" : "");
            first = false;
        }
        if (sh) {
            strbuf_appendf(&sb, "%sran %d command%s", first ? "R" : ", r", sh, sh > 1 ? "s" : "");
            first = false;
        }
        if (se) {
            strbuf_appendf(&sb, "%ssearched %d time%s", first ? "S" : ", s", se, se > 1 ? "s" : "");
            first = false;
        }
        if (ot) {
            strbuf_appendf(&sb, "%s%d other action%s", first ? "" : ", ", ot, ot > 1 ? "s" : "");
        }
    }
    return strbuf_detach(&sb);
}

char *tool_batch_summarize(const char **names, const char **inputs, const char **outputs, int n) {
    if (n <= 0)
        return NULL;
    tool_batch_entry_t *en = calloc((size_t)n, sizeof(tool_batch_entry_t));
    if (!en)
        return NULL;
    for (int i = 0; i < n; i++) {
        en[i].name = names[i];
        en[i].input_json = inputs ? inputs[i] : NULL;
        en[i].output_json = outputs ? outputs[i] : NULL;
    }
    char *r = summarize_heuristic(en, n);
    free(en);
    return r;
}

char *tool_batch_summarize_single(const char *name, const char *input, const char *output) {
    return tool_batch_summarize(&name, &input, &output, 1);
}

char *tool_summary_truncate_json(const char *json, int max_len) {
    if (!json)
        return nc_xstrdup("[null]");
    int len = (int)strlen(json);
    if (len <= max_len)
        return nc_xstrdup(json);
    char *buf = malloc((size_t)max_len + 4);
    if (!buf)
        return nc_xstrdup("...");
    memcpy(buf, json, (size_t)max_len);
    memcpy(buf + max_len, "...", 4);
    return buf;
}
