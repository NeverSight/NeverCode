#include "tools.h"
#include "nevercode.h"
#include "harness.h"

static const char *SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"Absolute path to the file\"},"
    "\"offset\":{\"type\":\"integer\",\"description\":\"Line offset (1-based)\"},"
    "\"limit\":{\"type\":\"integer\",\"description\":\"Max lines to read\"},"
    "\"hex\":{\"type\":\"boolean\",\"description\":\"Read as hex dump (for binary files)\"}"
    "},"
    "\"required\":[\"path\"]"
    "}";

static char *hex_dump(const unsigned char *data, size_t len, size_t max_bytes) {
    if (len > max_bytes)
        len = max_bytes;
    strbuf_t sb;
    strbuf_init(&sb);
    for (size_t off = 0; off < len; off += 16) {
        char line[NC_TAG_MAX];
        int pos = snprintf(line, sizeof(line), "%08zx  ", off);
        for (int i = 0; i < 16; i++) {
            if (off + i < len)
                pos += snprintf(line + pos, sizeof(line) - pos, "%02x ", data[off + i]);
            else
                pos += snprintf(line + pos, sizeof(line) - pos, "   ");
            if (i == 7)
                line[pos++] = ' ';
        }
        pos += snprintf(line + pos, sizeof(line) - pos, " |");
        for (int i = 0; i < 16 && off + i < len; i++) {
            unsigned char c = data[off + i];
            line[pos++] = (c >= 32 && c < 127) ? c : '.';
        }
        line[pos++] = '|';
        line[pos++] = '\n';
        line[pos] = '\0';
        strbuf_append_str(&sb, line);
    }
    if (len < max_bytes) {
        char footer[NC_FMT_MAX];
        snprintf(footer, sizeof(footer), "\n(%zu bytes total)\n", len);
        strbuf_append_str(&sb, footer);
    }
    return strbuf_detach(&sb);
}

static int file_read_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *path = nc_cjson_str((cJSON *)input, "path", NULL);
    if (!path) {
        *output = nc_xstrdup("error: missing 'path'");
        return 1;
    }

    cJSON *hex_flag = cJSON_GetObjectItem((cJSON *)input, "hex");
    bool want_hex = hex_flag && cJSON_IsTrue(hex_flag);

    int offset = 1, limit = 0;
    bool negative_offset = false;
    cJSON *off = cJSON_GetObjectItem((cJSON *)input, "offset");
    if (off && cJSON_IsNumber(off)) {
        offset = (int)off->valuedouble;
        if (offset < 0)
            negative_offset = true;
    }
    cJSON *lim = cJSON_GetObjectItem((cJSON *)input, "limit");
    if (lim && cJSON_IsNumber(lim))
        limit = (int)lim->valuedouble;
    if (!negative_offset && offset < 1)
        offset = 1;

    struct stat st;
    if (stat(path, &st) != 0) {
        char msg[NC_MSG_MAX];
        snprintf(msg, sizeof(msg), "error: file not found: %s", path);
        *output = nc_xstrdup(msg);
        return 1;
    }
    if (S_ISDIR(st.st_mode)) {
        *output = nc_xstrdup("error: path is a directory");
        return 1;
    }

    if (want_hex) {
        FILE *f = fopen(path, "rb");
        if (!f) {
            *output = nc_xstrdup("error: cannot open file");
            return 1;
        }
        size_t max_read = BINARY_DETECT_READ_BUF;
        if (limit > 0)
            max_read = (size_t)limit;
        unsigned char *buf = malloc(max_read);
        if (!buf) {
            fclose(f);
            *output = nc_xstrdup("OOM reading binary file");
            return -1;
        }
        size_t n = fread(buf, 1, max_read, f);
        fclose(f);
        *output = hex_dump(buf, n, max_read);
        free(buf);
        return 0;
    }

    if (text_is_binary_file(path)) {
        char msg[NC_MSG_MAX];
        snprintf(msg, sizeof(msg),
                 "Binary file: %s (%lld bytes). "
                 "Use Read with hex=true, or Bash with xxd/base64.",
                 path, (long long)st.st_size);
        *output = nc_xstrdup(msg);
        return 1;
    }

    size_t filesz = (size_t)st.st_size;
    char *filebuf = NULL;
    size_t nread = 0;

    {
        size_t clen = 0;
        const char *cached = frc_get(path, &clen);
        if (cached && clen > 0) {
            filebuf = malloc(clen + 1);
            if (!filebuf) {
                *output = nc_xstrdup("error: out of memory");
                return 1;
            }
            memcpy(filebuf, cached, clen);
            filebuf[clen] = '\0';
            nread = clen;
        }
    }

    if (!filebuf) {
        FILE *f = fopen(path, "rb");
        if (!f) {
            char msg[NC_MSG_MAX];
            snprintf(msg, sizeof(msg), "error: cannot open: %s", path);
            *output = nc_xstrdup(msg);
            return 1;
        }

        filebuf = malloc(filesz + 1);
        if (!filebuf) {
            fclose(f);
            *output = nc_xstrdup("error: out of memory");
            return 1;
        }
        nread = fread(filebuf, 1, filesz, f);
        fclose(f);
        filebuf[nread] = '\0';

        /* Content-based binary detection via text_encoding */
        if (text_is_binary_data((const unsigned char *)filebuf,
                                nread < BINARY_DETECT_READ_BUF ? nread : BINARY_DETECT_READ_BUF)) {
            free(filebuf);
            char msg[NC_MSG_MAX];
            snprintf(msg, sizeof(msg),
                     "error: binary content detected: %s (%zu bytes). "
                     "Use Bash with base64/xxd to inspect binary content.",
                     path, filesz);
            *output = nc_xstrdup(msg);
            return 1;
        }

        if (nread > 0)
            frc_put(path, filebuf, nread);
    }

    /* First pass: count total lines */
    int total_lines = 0;
    {
        char *p = filebuf;
        while (*p) {
            if (*p == '\n')
                total_lines++;
            p++;
        }
        if (nread > 0 && filebuf[nread - 1] != '\n')
            total_lines++;
    }

    /* Resolve negative offset (from end of file) */
    if (negative_offset) {
        offset = total_lines + offset + 1;
        if (offset < 1)
            offset = 1;
    }

    /* Second pass: emit requested range */
    strbuf_t sb;
    strbuf_init(&sb);
    int lineno = 0, count = 0;
    char *p = filebuf;
    while (*p) {
        char *eol = strchr(p, '\n');
        size_t llen = eol ? (size_t)(eol - p) : strlen(p);
        lineno++;

        if (lineno >= offset && (limit <= 0 || count < limit)) {
            char numbered[NC_SHORT_MAX];
            snprintf(numbered, sizeof(numbered), "%6d|", lineno);
            strbuf_append_str(&sb, numbered);
            strbuf_append(&sb, p, llen);
            strbuf_append(&sb, "\n", 1);
            count++;
        }

        p = eol ? eol + 1 : p + llen;
    }
    free(filebuf);

    stall_detector_record_read(path);

    if (count == 0) {
        strbuf_append_str(&sb, "(empty file or offset beyond end)");
    } else if (limit > 0 || offset > 1) {
        char meta[NC_TAG_MAX];
        snprintf(meta, sizeof(meta), "\n(%d lines shown, %d total)\n", count, total_lines);
        strbuf_append_str(&sb, meta);
    }

    *output = strbuf_detach(&sb);
    return 0;
}

static tool_def_t file_read_tool;

void tool_file_read_register(void) {
    file_read_tool.name = "Read";
    file_read_tool.description =
        "Read a file from disk. Returns content with line numbers. "
        "Use offset and limit for large files. Negative offset reads from end. "
        "Set hex=true for binary files (returns hex dump).";
    file_read_tool.input_schema = cJSON_Parse(SCHEMA);
    file_read_tool.execute = file_read_exec;
    file_read_tool.read_only = true;
    file_read_tool.concurrency_safe = true;
    file_read_tool.destructive = false;
    tools_register(&file_read_tool);
}
