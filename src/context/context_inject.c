/*
 * context_inject.c — @-reference expansion and inline content embedding
 *
 * Expands @file and @url references in user input into inline text.
 */

#include "nevercode.h"
#include "conversation.h"

/* CTX_MAX_IMAGE_REFS in defaults.h */
#define MAX_IMAGE_REFS CTX_MAX_IMAGE_REFS

static bool is_path_char(char c) {
    return isalnum((unsigned char)c) || c == '/' || c == '.' || c == '-' || c == '_' || c == '~' ||
           c == '+';
}

static char *read_file_contents(const char *path) {
    size_t fsz = 0;
    char *buf = strbuf_read_file(path, &fsz);
    if (!buf)
        return NULL;
    if (fsz > INLINE_FILE_SIZE_MAX) {
        free(buf);
        strbuf_t sb;
        strbuf_init(&sb);
        strbuf_appendf(&sb, "[file too large: %zu bytes, max 512KB]", fsz);
        return strbuf_detach(&sb);
    }
    return buf;
}

typedef struct {
    char *base64;
    char *media_type;
} image_ref_t;

char *context_inject_refs(const char *input) {
    if (!input || !strchr(input, '@'))
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    bool injected = false;

    const char *p = input;
    while (*p) {
        if (*p == '@' && (p == input || isspace((unsigned char)*(p - 1)))) {
            const char *start = p + 1;
            const char *end = start;
            while (*end && is_path_char(*end))
                end++;
            size_t pathlen = end - start;

            if (pathlen > 1 && pathlen < NC_PATH_MAX) {
                char path[NC_PATH_MAX];
                memcpy(path, start, pathlen);
                path[pathlen] = '\0';

                if (nc_file_exists(path)) {
                    if (is_image_file(path)) {
                        strbuf_append(&sb, input, p - input);
                        strbuf_append_str(&sb, "[image: ");
                        strbuf_append_str(&sb, path);
                        strbuf_append_str(&sb, "]");
                        input = end;
                        p = end;
                        injected = true;
                        continue;
                    }
                    char *contents = read_file_contents(path);
                    if (contents) {
                        strbuf_append(&sb, input, p - input);
                        strbuf_append_str(&sb, "\n\n<file path=\"");
                        strbuf_append_str(&sb, path);
                        strbuf_append_str(&sb, "\">\n");
                        strbuf_append_str(&sb, contents);
                        strbuf_append_str(&sb, "\n</file>\n\n");
                        free(contents);
                        input = end;
                        p = end;
                        injected = true;
                        continue;
                    }
                }
            }
        }
        p++;
    }

    if (!injected) {
        strbuf_free(&sb);
        return NULL;
    }

    strbuf_append_str(&sb, input);
    return strbuf_detach(&sb);
}

int context_inject_images(const char *input, message_t *msg) {
    if (!input || !msg || !strchr(input, '@'))
        return 0;

    int count = 0;
    strbuf_t text_sb;
    strbuf_init(&text_sb);

    const char *p = input;
    while (*p) {
        if (*p == '@' && (p == input || isspace((unsigned char)*(p - 1)))) {
            const char *start = p + 1;
            const char *end = start;
            while (*end && is_path_char(*end))
                end++;
            size_t pathlen = end - start;

            if (pathlen > 1 && pathlen < NC_PATH_MAX && count < MAX_IMAGE_REFS) {
                char path[NC_PATH_MAX];
                memcpy(path, start, pathlen);
                path[pathlen] = '\0';

                if (nc_file_exists(path) && is_image_file(path)) {
                    char *media_type = NULL;
                    char *b64 = image_read_base64(path, &media_type);
                    if (b64 && media_type) {
                        if (text_sb.len > 0) {
                            message_add_text(msg, text_sb.data);
                            strbuf_clear(&text_sb);
                        }
                        message_add_image(msg, b64, media_type);
                        count++;
                        free(b64);
                        free(media_type);
                        p = end;
                        continue;
                    }
                    free(b64);
                    free(media_type);
                }
            }
        }
        strbuf_append(&text_sb, p, 1);
        p++;
    }

    if (text_sb.len > 0)
        message_add_text(msg, text_sb.data);
    strbuf_free(&text_sb);
    return count;
}
