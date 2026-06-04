#include "tools.h"
#include "nevercode.h"

static const char *SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"notebook_path\":{\"type\":\"string\",\"description\":\"Absolute path to the .ipynb file\"},"
    "\"cell_index\":{\"type\":\"integer\",\"description\":\"0-based index of the cell to edit\"},"
    "\"new_source\":{\"type\":\"string\",\"description\":\"New source content for the cell\"},"
    "\"cell_type\":{\"type\":\"string\",\"enum\":[\"code\",\"markdown\"],\"description\":\"Cell "
    "type (code or markdown). Required for insert.\"},"
    "\"edit_mode\":{\"type\":\"string\",\"enum\":[\"replace\",\"insert\",\"delete\"],"
    "\"description\":\"Edit mode: replace (default), insert, or delete\"}"
    "},"
    "\"required\":[\"notebook_path\",\"cell_index\",\"new_source\"]"
    "}";

static int notebook_edit_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *path = nc_cjson_str((cJSON *)input, "notebook_path", NULL);
    if (!path) {
        *output = nc_xstrdup("error: missing 'notebook_path'");
        return 1;
    }

    size_t plen = strlen(path);
    if (plen < 6 || strcmp(path + plen - 6, ".ipynb") != 0) {
        *output = nc_xstrdup("error: file must be .ipynb. Use FileEdit for other files.");
        return 1;
    }

    cJSON *idx_j = cJSON_GetObjectItem((cJSON *)input, "cell_index");
    if (!idx_j || !cJSON_IsNumber(idx_j)) {
        *output = nc_xstrdup("error: missing 'cell_index'");
        return 1;
    }
    int cell_idx = (int)idx_j->valuedouble;

    const char *new_source = nc_cjson_str((cJSON *)input, "new_source", NULL);
    const char *cell_type = nc_cjson_str((cJSON *)input, "cell_type", NULL);
    const char *edit_mode = nc_cjson_str((cJSON *)input, "edit_mode", NULL);
    if (!edit_mode)
        edit_mode = "replace";

    char *content = strbuf_read_file_text(path, NULL);
    if (!content) {
        char msg[NC_MSG_MAX];
        snprintf(msg, sizeof(msg), "error: cannot read notebook: %s", path);
        *output = nc_xstrdup(msg);
        return 1;
    }

    cJSON *notebook = cJSON_Parse(content);
    free(content);
    if (!notebook) {
        *output = nc_xstrdup("error: notebook is not valid JSON");
        return 1;
    }

    cJSON *cells = cJSON_GetObjectItem(notebook, "cells");
    if (!cells || !cJSON_IsArray(cells)) {
        cJSON_Delete(notebook);
        *output = nc_xstrdup("error: notebook has no 'cells' array");
        return 1;
    }

    int ncells = cJSON_GetArraySize(cells);

    if (strcmp(edit_mode, "delete") == 0) {
        if (cell_idx < 0 || cell_idx >= ncells) {
            char msg[NC_TAG_MAX];
            snprintf(msg, sizeof(msg), "error: cell_index %d out of range (0..%d)", cell_idx,
                     ncells - 1);
            *output = nc_xstrdup(msg);
            cJSON_Delete(notebook);
            return 1;
        }
        cJSON_DeleteItemFromArray(cells, cell_idx);

    } else if (strcmp(edit_mode, "insert") == 0) {
        if (cell_idx < 0 || cell_idx > ncells) {
            char msg[NC_TAG_MAX];
            snprintf(msg, sizeof(msg), "error: cell_index %d out of range for insert (0..%d)",
                     cell_idx, ncells);
            *output = nc_xstrdup(msg);
            cJSON_Delete(notebook);
            return 1;
        }
        if (!cell_type)
            cell_type = "code";

        cJSON *new_cell = cJSON_CreateObject();
        cJSON_AddStringToObject(new_cell, "cell_type", cell_type);
        cJSON_AddItemToObject(new_cell, "metadata", cJSON_CreateObject());

        cJSON *src_lines = cJSON_CreateArray();
        if (new_source && *new_source) {
            const char *p = new_source;
            while (*p) {
                const char *eol = strchr(p, '\n');
                if (eol) {
                    size_t llen = eol - p + 1;
                    char *line = malloc(llen + 1);
                    if (!line)
                        break;
                    memcpy(line, p, llen);
                    line[llen] = '\0';
                    cJSON_AddItemToArray(src_lines, cJSON_CreateString(line));
                    free(line);
                    p = eol + 1;
                } else {
                    cJSON_AddItemToArray(src_lines, cJSON_CreateString(p));
                    break;
                }
            }
        }
        cJSON_AddItemToObject(new_cell, "source", src_lines);

        if (strcmp(cell_type, "code") == 0) {
            cJSON_AddNullToObject(new_cell, "execution_count");
            cJSON_AddItemToObject(new_cell, "outputs", cJSON_CreateArray());
        }

        /* cJSON_InsertItemInArray available since cJSON 1.7.13;
           fallback: add then rotate for older versions */
        if (cell_idx >= cJSON_GetArraySize(cells)) {
            cJSON_AddItemToArray(cells, new_cell);
        } else {
            cJSON_InsertItemInArray(cells, cell_idx, new_cell);
        }

    } else {
        /* replace */
        if (cell_idx < 0 || cell_idx >= ncells) {
            char msg[NC_TAG_MAX];
            snprintf(msg, sizeof(msg), "error: cell_index %d out of range (0..%d)", cell_idx,
                     ncells - 1);
            *output = nc_xstrdup(msg);
            cJSON_Delete(notebook);
            return 1;
        }
        cJSON *target = cJSON_GetArrayItem(cells, cell_idx);

        cJSON *old_source = cJSON_GetObjectItem(target, "source");
        if (old_source)
            cJSON_DeleteItemFromObject(target, "source");

        cJSON *src_lines = cJSON_CreateArray();
        if (new_source && *new_source) {
            const char *p = new_source;
            while (*p) {
                const char *eol = strchr(p, '\n');
                if (eol) {
                    size_t llen = eol - p + 1;
                    char *line = malloc(llen + 1);
                    if (!line)
                        break;
                    memcpy(line, p, llen);
                    line[llen] = '\0';
                    cJSON_AddItemToArray(src_lines, cJSON_CreateString(line));
                    free(line);
                    p = eol + 1;
                } else {
                    cJSON_AddItemToArray(src_lines, cJSON_CreateString(p));
                    break;
                }
            }
        }
        cJSON_AddItemToObject(target, "source", src_lines);

        const char *current_type = nc_cjson_str(target, "cell_type", NULL);
        if (current_type && strcmp(current_type, "code") == 0) {
            cJSON_DeleteItemFromObject(target, "execution_count");
            cJSON_AddNullToObject(target, "execution_count");
            cJSON_DeleteItemFromObject(target, "outputs");
            cJSON_AddItemToObject(target, "outputs", cJSON_CreateArray());
        }
        if (cell_type) {
            cJSON_DeleteItemFromObject(target, "cell_type");
            cJSON_AddStringToObject(target, "cell_type", cell_type);
        }
    }

    char *json_out = nc_cjson_detach_pretty(notebook);

    FILE *f = fopen(path, "w");
    if (!f) {
        free(json_out);
        *output = nc_xstrdup("error: cannot write notebook file");
        return 1;
    }
    int write_err = fputs(json_out, f);
    int flush_err = fflush(f);
    fclose(f);
    free(json_out);
    if (write_err == EOF || flush_err) {
        *output = nc_xstrdup("error: failed to write notebook file");
        return 1;
    }
    vtrig_record_edit(path);

    char msg[NC_MSG_MAX];
    snprintf(msg, sizeof(msg), "Successfully %s cell %d in %s",
             strcmp(edit_mode, "delete") == 0   ? "deleted"
             : strcmp(edit_mode, "insert") == 0 ? "inserted"
                                                : "replaced",
             cell_idx, path);
    *output = nc_xstrdup(msg);
    return 0;
}

static tool_def_t notebook_edit_tool;

void tool_notebook_edit_register(void) {
    notebook_edit_tool.name = "NotebookEdit";
    notebook_edit_tool.description =
        "Edit Jupyter notebook (.ipynb) cells. Supports replace, insert, "
        "and delete operations on individual cells.";
    notebook_edit_tool.input_schema = cJSON_Parse(SCHEMA);
    notebook_edit_tool.execute = notebook_edit_exec;
    notebook_edit_tool.read_only = false;
    notebook_edit_tool.concurrency_safe = false;
    notebook_edit_tool.destructive = false;
    tools_register(&notebook_edit_tool);
}
