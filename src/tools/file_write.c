#include "tools.h"
#include "nevercode.h"

static const char *SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"Absolute path to write\"},"
    "\"content\":{\"type\":\"string\",\"description\":\"Content to write\"}"
    "},"
    "\"required\":[\"path\",\"content\"]"
    "}";

static int file_write_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *path = nc_cjson_str((cJSON *)input, "path", NULL);
    const char *content = nc_cjson_str((cJSON *)input, "content", NULL);
    if (!path) {
        *output = nc_xstrdup("error: missing 'path'");
        return 1;
    }
    if (!content) {
        *output = nc_xstrdup("error: missing 'content'");
        return 1;
    }

    if (!sandbox_check_path(path)) {
        char msg[NC_MSG_MAX];
        snprintf(msg, sizeof(msg), "error: path outside project scope: %s", path);
        *output = nc_xstrdup(msg);
        return 1;
    }

    nc_mkdir_p_for_file(path);

    FILE *f = fopen(path, "w");
    if (!f) {
        char msg[NC_MSG_MAX];
        snprintf(msg, sizeof(msg), "error: cannot write: %s (%s)", path, strerror(errno));
        *output = nc_xstrdup(msg);
        return 1;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    int flush_err = fflush(f);
    fclose(f);
    if (written < len || flush_err) {
        char msg[NC_MSG_MAX];
        snprintf(msg, sizeof(msg), "error: short write to %s (%zu/%zu bytes)", path, written, len);
        *output = nc_xstrdup(msg);
        return 1;
    }
    frc_invalidate(path);

    int new_lines = 1;
    for (size_t i = 0; i < len; i++)
        if (content[i] == '\n')
            new_lines++;
    turn_diff_record(file_history_current_turn(), path, new_lines, 0);

    char msg[NC_MSG_MAX];
    snprintf(msg, sizeof(msg), "Wrote %zu bytes to %s", len, path);
    *output = nc_xstrdup(msg);
    vtrig_record_edit(path);
    return 0;
}

static tool_def_t file_write_tool;

void tool_file_write_register(void) {
    file_write_tool.name = "Write";
    file_write_tool.description = "Write content to a file. Creates parent directories if needed. "
                                  "Overwrites existing files.";
    file_write_tool.input_schema = cJSON_Parse(SCHEMA);
    file_write_tool.execute = file_write_exec;
    file_write_tool.read_only = false;
    file_write_tool.concurrency_safe = false;
    file_write_tool.destructive = false;
    tools_register(&file_write_tool);
}
