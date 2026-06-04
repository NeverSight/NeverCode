#include "test.h"
#include "nevercode.h"
#include "tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char tmp_path[256];

static void write_test_file(const char *content) {
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/test_fread_%d.txt", getpid());
    FILE *f = fopen(tmp_path, "w");
    fputs(content, f);
    fclose(f);
}

static void cleanup_file(void) {
    unlink(tmp_path);
}

static int test_negative_offset_last_line(void) {
    write_test_file("line1\nline2\nline3\nline4\nline5\n");

    const tool_def_t *t = tools_find("Read");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", tmp_path);
    cJSON_AddNumberToObject(input, "offset", -1);
    cJSON_AddNumberToObject(input, "limit", 1);

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "line5") != NULL);
    ASSERT(strstr(output, "line4") == NULL);

    cJSON_Delete(input);
    free(output);
    cleanup_file();
    return 0;
}

static int test_negative_offset_last_3(void) {
    write_test_file("line1\nline2\nline3\nline4\nline5\n");

    const tool_def_t *t = tools_find("Read");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", tmp_path);
    cJSON_AddNumberToObject(input, "offset", -3);
    cJSON_AddNumberToObject(input, "limit", 3);

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "line3") != NULL);
    ASSERT(strstr(output, "line4") != NULL);
    ASSERT(strstr(output, "line5") != NULL);
    ASSERT(strstr(output, "line2") == NULL);

    cJSON_Delete(input);
    free(output);
    cleanup_file();
    return 0;
}

static int test_negative_offset_overflow(void) {
    write_test_file("line1\nline2\n");

    const tool_def_t *t = tools_find("Read");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", tmp_path);
    cJSON_AddNumberToObject(input, "offset", -100);

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "line1") != NULL);

    cJSON_Delete(input);
    free(output);
    cleanup_file();
    return 0;
}

static int test_read_tool_flags(void) {
    const tool_def_t *t = tools_find("Read");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == true);
    ASSERT(t->concurrency_safe == true);
    ASSERT(t->destructive == false);
    return 0;
}

static int test_glob_tool_flags(void) {
    const tool_def_t *t = tools_find("Glob");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == true);
    ASSERT(t->concurrency_safe == true);
    return 0;
}

static int test_grep_tool_flags(void) {
    const tool_def_t *t = tools_find("Grep");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == true);
    ASSERT(t->concurrency_safe == true);
    return 0;
}

static int test_bash_tool_flags(void) {
    const tool_def_t *t = tools_find("Bash");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == false);
    ASSERT(t->concurrency_safe == false);
    return 0;
}

static int test_write_tool_flags(void) {
    const tool_def_t *t = tools_find("Write");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == false);
    ASSERT(t->concurrency_safe == false);
    return 0;
}

int test_file_read_extra_all(void) {
    TEST_SUITE("file_read_extra");
    TEST_COUNTERS;
    RUN_TEST(test_negative_offset_last_line);
    RUN_TEST(test_negative_offset_last_3);
    RUN_TEST(test_negative_offset_overflow);
    RUN_TEST(test_read_tool_flags);
    RUN_TEST(test_glob_tool_flags);
    RUN_TEST(test_grep_tool_flags);
    RUN_TEST(test_bash_tool_flags);
    RUN_TEST(test_write_tool_flags);
    TEST_RETURN_FAILED();
}
