/*
 * conv_export.c — Conversation export in Markdown / HTML / JSON formats
 *
 * Generates full conversation exports suitable for sharing, archival,
 * or analysis. Supports three output formats with appropriate styling.
 */

#include "nevercode.h"
#include "conversation.h"

static void append_html_escaped(strbuf_t *sb, const char *text) {
    if (!text)
        return;
    for (const char *p = text; *p; p++) {
        switch (*p) {
        case '<':
            strbuf_append_str(sb, "&lt;");
            break;
        case '>':
            strbuf_append_str(sb, "&gt;");
            break;
        case '&':
            strbuf_append_str(sb, "&amp;");
            break;
        case '"':
            strbuf_append_str(sb, "&quot;");
            break;
        default:
            strbuf_append(sb, p, 1);
            break;
        }
    }
}

char *conv_export_markdown(const void *conv_ptr, const char *title) {
    const conversation_t *conv = conv_ptr;
    if (!conv)
        return nc_xstrdup("");

    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_str(&sb, "# ");
    strbuf_append_str(&sb, title ? title : "Nevercode Session");
    strbuf_append_str(&sb, "\n\n");

    time_t now = nc_time_now();
    char timebuf[NC_FMT_MAX];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    strbuf_append_str(&sb, "_Exported: ");
    strbuf_append_str(&sb, timebuf);
    strbuf_append_str(&sb, "_\n\n---\n\n");

    for (int i = 0; i < conv->len; i++) {
        const message_t *m = &conv->msgs[i];
        const char *label;
        switch (m->role) {
        case ROLE_USER:
            label = "User";
            break;
        case ROLE_ASSISTANT:
            label = "Assistant";
            break;
        case ROLE_TOOL:
            label = "Tool Result";
            break;
        case ROLE_SYSTEM:
            label = "System";
            break;
        default:
            label = "Unknown";
            break;
        }

        strbuf_append_str(&sb, "## ");
        strbuf_append_str(&sb, label);
        strbuf_append_str(&sb, "\n\n");

        for (int j = 0; j < m->num_blocks; j++) {
            const content_block_t *b = &m->blocks[j];
            if (b->type == CONTENT_TEXT && b->text) {
                strbuf_append_str(&sb, b->text);
                strbuf_append_str(&sb, "\n\n");
            } else if (b->type == CONTENT_TOOL_USE && b->tool_name) {
                strbuf_append_str(&sb, "**Tool: ");
                strbuf_append_str(&sb, b->tool_name);
                strbuf_append_str(&sb, "**\n");
                if (b->tool_input) {
                    char *args = cJSON_Print(b->tool_input);
                    if (args) {
                        strbuf_append_str(&sb, "```json\n");
                        strbuf_append_str(&sb, args);
                        strbuf_append_str(&sb, "\n```\n\n");
                        free(args);
                    }
                }
            } else if (b->type == CONTENT_TOOL_RESULT && b->text) {
                strbuf_append_str(&sb, "```\n");
                size_t tlen = strlen(b->text);
                if (tlen > CONV_EXPORT_TRUNC_CHARS) {
                    strbuf_append(&sb, b->text, CONV_EXPORT_TRUNC_CHARS);
                    strbuf_append_str(&sb, "\n... (truncated)\n");
                } else {
                    strbuf_append_str(&sb, b->text);
                    strbuf_append_str(&sb, "\n");
                }
                strbuf_append_str(&sb, "```\n\n");
            }
        }
    }

    return strbuf_detach(&sb);
}

char *conv_export_html(const void *conv_ptr, const char *title) {
    const conversation_t *conv = conv_ptr;
    if (!conv)
        return nc_xstrdup("");

    strbuf_t sb;
    strbuf_init(&sb);

    const char *safe_title = title ? title : "Nevercode Session";

    strbuf_append_str(&sb,
                      "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
                      "<meta charset=\"UTF-8\">\n"
                      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
                      "<title>");
    append_html_escaped(&sb, safe_title);
    strbuf_append_str(&sb,
                      "</title>\n<style>\n"
                      "body{font-family:-apple-system,system-ui,sans-serif;max-width:800px;"
                      "margin:2rem auto;padding:0 1rem;background:#0d1117;color:#c9d1d9;}\n"
                      "h1{color:#58a6ff;border-bottom:1px solid #21262d;padding-bottom:.5rem;}\n"
                      ".msg{margin:1.5rem 0;padding:1rem;border-radius:8px;}\n"
                      ".user{background:#161b22;border-left:3px solid #3fb950;}\n"
                      ".assistant{background:#161b22;border-left:3px solid #58a6ff;}\n"
                      ".tool{background:#0d1117;border-left:3px solid #8b949e;font-size:.9em;}\n"
                      ".role{font-weight:600;margin-bottom:.5rem;}\n"
                      ".user .role{color:#3fb950;}\n"
                      ".assistant .role{color:#58a6ff;}\n"
                      ".tool .role{color:#8b949e;}\n"
                      "pre{background:#0d1117;padding:.8rem;border-radius:6px;overflow-x:auto;}\n"
                      "code{font-family:'SF Mono',monospace;font-size:.85em;}\n"
                      ".meta{color:#8b949e;font-size:.85em;margin-top:1rem;}\n"
                      "</style>\n</head>\n<body>\n"
                      "<h1>");
    append_html_escaped(&sb, safe_title);
    strbuf_append_str(&sb, "</h1>\n");

    for (int i = 0; i < conv->len; i++) {
        const message_t *m = &conv->msgs[i];
        const char *cls, *label;
        switch (m->role) {
        case ROLE_USER:
            cls = "user";
            label = "User";
            break;
        case ROLE_ASSISTANT:
            cls = "assistant";
            label = "Assistant";
            break;
        case ROLE_TOOL:
            cls = "tool";
            label = "Tool";
            break;
        default:
            cls = "tool";
            label = "System";
            break;
        }

        strbuf_append_str(&sb, "<div class=\"msg ");
        strbuf_append_str(&sb, cls);
        strbuf_append_str(&sb, "\">\n<div class=\"role\">");
        strbuf_append_str(&sb, label);
        strbuf_append_str(&sb, "</div>\n");

        for (int j = 0; j < m->num_blocks; j++) {
            const content_block_t *b = &m->blocks[j];
            if (b->type == CONTENT_TEXT && b->text) {
                strbuf_append_str(&sb, "<p>");
                append_html_escaped(&sb, b->text);
                strbuf_append_str(&sb, "</p>\n");
            } else if (b->type == CONTENT_TOOL_USE && b->tool_name) {
                strbuf_append_str(&sb, "<p><strong>Tool: ");
                append_html_escaped(&sb, b->tool_name);
                strbuf_append_str(&sb, "</strong></p>\n");
                if (b->tool_input) {
                    char *args = cJSON_Print(b->tool_input);
                    if (args) {
                        strbuf_append_str(&sb, "<pre><code>");
                        append_html_escaped(&sb, args);
                        strbuf_append_str(&sb, "</code></pre>\n");
                        free(args);
                    }
                }
            } else if (b->type == CONTENT_TOOL_RESULT && b->text) {
                strbuf_append_str(&sb, "<pre><code>");
                size_t tlen = strlen(b->text);
                if (tlen > CONV_EXPORT_TRUNC_CHARS) {
                    char *trunc = nc_xstrndup(b->text, CONV_EXPORT_TRUNC_CHARS);
                    if (trunc) {
                        append_html_escaped(&sb, trunc);
                        free(trunc);
                    }
                    strbuf_append_str(&sb, "\n... (truncated)");
                } else {
                    append_html_escaped(&sb, b->text);
                }
                strbuf_append_str(&sb, "</code></pre>\n");
            }
        }
        strbuf_append_str(&sb, "</div>\n");
    }

    time_t now = nc_time_now();
    char timebuf[NC_FMT_MAX];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    strbuf_append_str(&sb, "<div class=\"meta\">Exported: ");
    strbuf_append_str(&sb, timebuf);
    strbuf_append_str(&sb, " &mdash; nevercode v" NEVERCODE_VERSION "</div>\n");
    strbuf_append_str(&sb, "</body>\n</html>\n");

    return strbuf_detach(&sb);
}

char *conv_export_json(const void *conv_ptr, const char *title) {
    const conversation_t *conv = conv_ptr;
    if (!conv)
        return nc_xstrdup("{}");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "title", title ? title : "Nevercode Session");
    cJSON_AddStringToObject(root, "version", NEVERCODE_VERSION);

    time_t now = nc_time_now();
    cJSON_AddNumberToObject(root, "exported_at", (double)now);

    cJSON *msgs = cJSON_CreateArray();
    for (int i = 0; i < conv->len; i++) {
        const message_t *m = &conv->msgs[i];
        cJSON *msg = cJSON_CreateObject();

        const char *role_str;
        switch (m->role) {
        case ROLE_USER:
            role_str = "user";
            break;
        case ROLE_ASSISTANT:
            role_str = "assistant";
            break;
        case ROLE_TOOL:
            role_str = "tool";
            break;
        case ROLE_SYSTEM:
            role_str = "system";
            break;
        default:
            role_str = "unknown";
            break;
        }
        cJSON_AddStringToObject(msg, "role", role_str);

        cJSON *blocks = cJSON_CreateArray();
        for (int j = 0; j < m->num_blocks; j++) {
            const content_block_t *b = &m->blocks[j];
            cJSON *blk = cJSON_CreateObject();

            if (b->type == CONTENT_TEXT) {
                cJSON_AddStringToObject(blk, "type", "text");
                cJSON_AddStringToObject(blk, "text", b->text ? b->text : "");
            } else if (b->type == CONTENT_TOOL_USE) {
                cJSON_AddStringToObject(blk, "type", "tool_use");
                cJSON_AddStringToObject(blk, "name", b->tool_name ? b->tool_name : "");
                if (b->tool_use_id)
                    cJSON_AddStringToObject(blk, "id", b->tool_use_id);
                if (b->tool_input)
                    cJSON_AddItemToObject(blk, "input", cJSON_Duplicate(b->tool_input, 1));
            } else if (b->type == CONTENT_TOOL_RESULT) {
                cJSON_AddStringToObject(blk, "type", "tool_result");
                cJSON_AddStringToObject(blk, "text", b->text ? b->text : "");
                if (b->tool_use_id)
                    cJSON_AddStringToObject(blk, "tool_use_id", b->tool_use_id);
                cJSON_AddBoolToObject(blk, "is_error", b->is_error);
            }

            cJSON_AddItemToArray(blocks, blk);
        }
        cJSON_AddItemToObject(msg, "content", blocks);
        cJSON_AddItemToArray(msgs, msg);
    }
    cJSON_AddItemToObject(root, "messages", msgs);

    char *out = nc_cjson_detach_pretty(root);
    return out ? out : nc_xstrdup("{}");
}

int conv_export_to_file(const void *conv, const char *path, conv_export_fmt_t fmt,
                        const char *title) {
    char *content = NULL;
    switch (fmt) {
    case EXPORT_MARKDOWN:
        content = conv_export_markdown(conv, title);
        break;
    case EXPORT_HTML:
        content = conv_export_html(conv, title);
        break;
    case EXPORT_JSON:
        content = conv_export_json(conv, title);
        break;
    }
    if (!content)
        return -1;

    int rc = nc_write_file_str(path, content);
    free(content);
    return rc;
}

conv_export_fmt_t conv_export_detect_format(const char *path) {
    if (!path)
        return EXPORT_MARKDOWN;
    const char *dot = strrchr(path, '.');
    if (!dot)
        return EXPORT_MARKDOWN;
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return EXPORT_HTML;
    if (strcmp(dot, ".json") == 0)
        return EXPORT_JSON;
    return EXPORT_MARKDOWN;
}
