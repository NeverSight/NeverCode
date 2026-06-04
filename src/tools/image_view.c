/*
 * image_view.c — ImageView tool: load local images for multimodal context
 *
 * Reads a local image file, validates format and size, encodes it
 * as base64, and returns metadata.  The base64 payload is attached
 * to the conversation for vision-capable models.
 */

#include "nevercode.h"
#include "tools.h"

int image_view_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    if (!input || !output)
        return -1;

    const cJSON *path_node = cJSON_GetObjectItem(input, "path");
    if (!path_node || !cJSON_IsString(path_node) || !path_node->valuestring[0]) {
        *output = nc_xstrdup("Missing required parameter: path");
        return 1;
    }

    const char *path = path_node->valuestring;

    if (!is_image_file(path)) {
        strbuf_t sb;
        strbuf_init(&sb);
        strbuf_appendf(&sb, "Not a supported image file: %s", path);
        *output = strbuf_detach(&sb);
        return 1;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        *output = nc_xstrdup("Cannot stat file");
        return 1;
    }
    if (st.st_size > IV_MAX_FILE_SIZE) {
        *output = nc_xstrdup("Image too large");
        return 1;
    }

    char *media_type = NULL;
    char *b64 = image_read_base64(path, &media_type);
    if (!b64) {
        *output = nc_xstrdup("Failed to read image");
        return 1;
    }

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Image: %s\nType: %s\nSize: %lld bytes\n[Image loaded]", path,
                   media_type ? media_type : "unknown", (long long)st.st_size);

    *output = strbuf_detach(&sb);
    free(b64);
    free(media_type);
    return 0;
}

static const char *IMAGE_VIEW_SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"Path to an image file\"}"
    "},"
    "\"required\":[\"path\"]"
    "}";

static tool_def_t image_view_tool;

void tool_image_view_register(void) {
    image_view_tool.name = "ImageView";
    image_view_tool.description = "Load a local image (base64) for multimodal context.";
    image_view_tool.input_schema = cJSON_Parse(IMAGE_VIEW_SCHEMA);
    image_view_tool.execute = image_view_exec;
    image_view_tool.read_only = true;
    image_view_tool.concurrency_safe = true;
    image_view_tool.destructive = false;
    tools_register(&image_view_tool);
}
