#include "test.h"
#include "nevercode.h"
#include "../include/nevercode.h"
#include "../include/conversation.h"
#include <string.h>
#include <unistd.h>

static int test_markdown_empty(void) {
    conversation_t conv;
    conversation_init(&conv);
    char *md = conv_export_markdown(&conv, "Test");
    ASSERT_NOT_NULL(md);
    ASSERT(strstr(md, "# Test") != NULL);
    ASSERT(strstr(md, "Exported:") != NULL);
    free(md);
    conversation_free(&conv);
    return 0;
}

static int test_markdown_with_messages(void) {
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "Hello world");
    message_t *am = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(am, "Hi there!");

    char *md = conv_export_markdown(&conv, NULL);
    ASSERT_NOT_NULL(md);
    ASSERT(strstr(md, "## User") != NULL);
    ASSERT(strstr(md, "Hello world") != NULL);
    ASSERT(strstr(md, "## Assistant") != NULL);
    ASSERT(strstr(md, "Hi there!") != NULL);
    free(md);
    conversation_free(&conv);
    return 0;
}

static int test_html_structure(void) {
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "Test <html> & \"quotes\"");

    char *html = conv_export_html(&conv, "HTML Test");
    ASSERT_NOT_NULL(html);
    ASSERT(strstr(html, "<!DOCTYPE html>") != NULL);
    ASSERT(strstr(html, "HTML Test") != NULL);
    ASSERT(strstr(html, "&lt;html&gt;") != NULL);
    ASSERT(strstr(html, "&amp;") != NULL);
    ASSERT(strstr(html, "&quot;quotes&quot;") != NULL);
    ASSERT(strstr(html, "</html>") != NULL);
    free(html);
    conversation_free(&conv);
    return 0;
}

static int test_html_dark_theme(void) {
    conversation_t conv;
    conversation_init(&conv);
    char *html = conv_export_html(&conv, "Theme");
    ASSERT_NOT_NULL(html);
    ASSERT(strstr(html, "#0d1117") != NULL);
    ASSERT(strstr(html, "#58a6ff") != NULL);
    free(html);
    conversation_free(&conv);
    return 0;
}

static int test_json_structure(void) {
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "Query");

    char *json = conv_export_json(&conv, "JSON Test");
    ASSERT_NOT_NULL(json);

    cJSON *root = cJSON_Parse(json);
    ASSERT_NOT_NULL(root);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(root, "title")), "JSON Test");
    ASSERT_NOT_NULL(cJSON_GetObjectItem(root, "messages"));
    ASSERT_INT_EQ(cJSON_GetArraySize(cJSON_GetObjectItem(root, "messages")), 1);

    cJSON *msg0 = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "messages"), 0);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(msg0, "role")), "user");

    cJSON_Delete(root);
    free(json);
    conversation_free(&conv);
    return 0;
}

static int test_json_tool_use(void) {
    conversation_t conv;
    conversation_init(&conv);
    message_t *am = conversation_add(&conv, ROLE_ASSISTANT);
    cJSON *inp = cJSON_Parse("{\"command\":\"ls\"}");
    message_add_tool_use(am, "call_1", "Bash", inp);
    cJSON_Delete(inp);

    char *json = conv_export_json(&conv, NULL);
    ASSERT_NOT_NULL(json);
    ASSERT(strstr(json, "tool_use") != NULL);
    ASSERT(strstr(json, "Bash") != NULL);
    free(json);
    conversation_free(&conv);
    return 0;
}

static int test_detect_format(void) {
    ASSERT_INT_EQ(conv_export_detect_format("out.md"), EXPORT_MARKDOWN);
    ASSERT_INT_EQ(conv_export_detect_format("out.html"), EXPORT_HTML);
    ASSERT_INT_EQ(conv_export_detect_format("out.htm"), EXPORT_HTML);
    ASSERT_INT_EQ(conv_export_detect_format("out.json"), EXPORT_JSON);
    ASSERT_INT_EQ(conv_export_detect_format("out.txt"), EXPORT_MARKDOWN);
    ASSERT_INT_EQ(conv_export_detect_format(NULL), EXPORT_MARKDOWN);
    return 0;
}

static int test_export_to_file(void) {
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "file export test");

    char tmpf[] = "/tmp/nevercode_export_XXXXXX";
    int fd = mkstemp(tmpf);
    ASSERT(fd >= 0);
    close(fd);

    int rc = conv_export_to_file(&conv, tmpf, EXPORT_MARKDOWN, "File Test");
    ASSERT_INT_EQ(rc, 0);

    FILE *f = fopen(tmpf, "r");
    ASSERT_NOT_NULL(f);
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    ASSERT(strstr(buf, "File Test") != NULL);
    ASSERT(strstr(buf, "file export test") != NULL);

    unlink(tmpf);
    conversation_free(&conv);
    return 0;
}

static int test_null_conv(void) {
    char *md = conv_export_markdown(NULL, NULL);
    ASSERT_NOT_NULL(md);
    free(md);
    char *html = conv_export_html(NULL, NULL);
    ASSERT_NOT_NULL(html);
    free(html);
    char *json = conv_export_json(NULL, NULL);
    ASSERT_NOT_NULL(json);
    free(json);
    return 0;
}

int test_conv_export_all(void) {
    TEST_SUITE("conv_export");
    TEST_COUNTERS;

    RUN_TEST(test_markdown_empty);
    RUN_TEST(test_markdown_with_messages);
    RUN_TEST(test_html_structure);
    RUN_TEST(test_html_dark_theme);
    RUN_TEST(test_json_structure);
    RUN_TEST(test_json_tool_use);
    RUN_TEST(test_detect_format);
    RUN_TEST(test_export_to_file);
    RUN_TEST(test_null_conv);

    TEST_RETURN_FAILED();
}
