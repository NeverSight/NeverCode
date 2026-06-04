#include "test.h"
#include "tools.h"
#include "nevercode.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

static int test_file_edit_replace_all(void) {
    const char *path = "/tmp/nevercode_test_ra.txt";
    FILE *f = fopen(path, "w");
    fprintf(f, "foo bar foo baz foo\n");
    fclose(f);

    const tool_def_t *edit = tools_find("Edit");
    ASSERT_NOT_NULL(edit);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", path);
    cJSON_AddStringToObject(input, "old_string", "foo");
    cJSON_AddStringToObject(input, "new_string", "QUX");
    cJSON_AddTrueToObject(input, "replace_all");
    char *output = NULL;
    int rc = edit->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "3") != NULL);
    free(output);

    f = fopen(path, "r");
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    ASSERT(strstr(buf, "QUX bar QUX baz QUX") != NULL);
    ASSERT(strstr(buf, "foo") == NULL);

    unlink(path);
    return 0;
}

static int test_file_edit_not_unique(void) {
    const char *path = "/tmp/nevercode_test_nu.txt";
    FILE *f = fopen(path, "w");
    fprintf(f, "aaa\nbbb\naaa\n");
    fclose(f);

    const tool_def_t *edit = tools_find("Edit");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", path);
    cJSON_AddStringToObject(input, "old_string", "aaa");
    cJSON_AddStringToObject(input, "new_string", "ccc");
    char *output = NULL;
    int rc = edit->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "not unique") != NULL);
    free(output);

    unlink(path);
    return 0;
}

static int test_file_write_mkdir(void) {
    const char *path = "/tmp/nevercode_test_mkdir/sub/file.txt";
    const tool_def_t *write_tool = tools_find("Write");

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", path);
    cJSON_AddStringToObject(input, "content", "nested write test");
    char *output = NULL;
    int rc = write_tool->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "Wrote") != NULL);
    free(output);

    FILE *f = fopen(path, "r");
    ASSERT_NOT_NULL(f);
    char buf[64];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    ASSERT_STR_EQ(buf, "nested write test");

    unlink(path);
    rmdir("/tmp/nevercode_test_mkdir/sub");
    rmdir("/tmp/nevercode_test_mkdir");
    return 0;
}

static int test_listdir(void) {
    mkdir("/tmp/nevercode_ld_test", 0755);
    FILE *f1 = fopen("/tmp/nevercode_ld_test/file1.c", "w");
    fputs("x", f1);
    fclose(f1);
    mkdir("/tmp/nevercode_ld_test/subdir", 0755);

    const tool_def_t *ld = tools_find("ListDir");
    ASSERT_NOT_NULL(ld);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", "/tmp/nevercode_ld_test");
    char *output = NULL;
    int rc = ld->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "file1.c") != NULL);
    ASSERT(strstr(output, "subdir") != NULL);
    free(output);

    unlink("/tmp/nevercode_ld_test/file1.c");
    rmdir("/tmp/nevercode_ld_test/subdir");
    rmdir("/tmp/nevercode_ld_test");
    return 0;
}

static int test_listdir_recursive(void) {
    mkdir("/tmp/nevercode_ldr", 0755);
    mkdir("/tmp/nevercode_ldr/a", 0755);
    FILE *f = fopen("/tmp/nevercode_ldr/a/deep.txt", "w");
    fputs("deep", f);
    fclose(f);

    const tool_def_t *ld = tools_find("ListDir");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", "/tmp/nevercode_ldr");
    cJSON_AddTrueToObject(input, "recursive");
    char *output = NULL;
    int rc = ld->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "deep.txt") != NULL);
    free(output);

    unlink("/tmp/nevercode_ldr/a/deep.txt");
    rmdir("/tmp/nevercode_ldr/a");
    rmdir("/tmp/nevercode_ldr");
    return 0;
}

static int test_grep(void) {
    const char *path = "/tmp/nevercode_grep_test.txt";
    FILE *f = fopen(path, "w");
    fprintf(f, "line one: hello\nline two: world\nline three: hello world\n");
    fclose(f);

    const tool_def_t *grep = tools_find("Grep");
    ASSERT_NOT_NULL(grep);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "pattern", "hello");
    cJSON_AddStringToObject(input, "path", path);
    char *output = NULL;
    int rc = grep->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "hello") != NULL);
    free(output);

    unlink(path);
    return 0;
}

static int test_bash_multiline(void) {
    const tool_def_t *bash = tools_find("Bash");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "echo line1; echo line2; echo line3");
    char *output = NULL;
    int rc = bash->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "line1") != NULL);
    ASSERT(strstr(output, "line2") != NULL);
    ASSERT(strstr(output, "line3") != NULL);
    free(output);
    return 0;
}

static int test_bash_stderr(void) {
    const tool_def_t *bash = tools_find("Bash");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "echo out; echo err >&2");
    char *output = NULL;
    int rc = bash->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "out") != NULL);
    ASSERT(strstr(output, "err") != NULL);
    free(output);
    return 0;
}

static int test_file_read_binary(void) {
    const char *path = "/tmp/nevercode_test_bin.dat";
    FILE *f = fopen(path, "wb");
    unsigned char data[] = {0x89, 0x50, 0x4E, 0x47, 0x00, 0x01, 0x02};
    fwrite(data, 1, sizeof(data), f);
    fclose(f);

    const tool_def_t *read_tool = tools_find("Read");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", path);
    char *output = NULL;
    int rc = read_tool->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "inary") != NULL || strstr(output, "hex") != NULL);
    free(output);

    unlink(path);
    return 0;
}

static int test_todo_merge(void) {
    const tool_def_t *todo = tools_find("TodoWrite");

    cJSON *input1 = cJSON_CreateObject();
    cJSON *todos1 = cJSON_CreateArray();
    cJSON *t1 = cJSON_CreateObject();
    cJSON_AddStringToObject(t1, "id", "task1");
    cJSON_AddStringToObject(t1, "content", "First");
    cJSON_AddStringToObject(t1, "status", "pending");
    cJSON_AddItemToArray(todos1, t1);
    cJSON *t2 = cJSON_CreateObject();
    cJSON_AddStringToObject(t2, "id", "task2");
    cJSON_AddStringToObject(t2, "content", "Second");
    cJSON_AddStringToObject(t2, "status", "pending");
    cJSON_AddItemToArray(todos1, t2);
    cJSON_AddItemToObject(input1, "todos", todos1);
    char *out1 = NULL;
    todo->execute(input1, &out1, NULL);
    cJSON_Delete(input1);
    ASSERT(strstr(out1, "First") != NULL);
    ASSERT(strstr(out1, "Second") != NULL);
    free(out1);

    cJSON *input2 = cJSON_CreateObject();
    cJSON *todos2 = cJSON_CreateArray();
    cJSON *t3 = cJSON_CreateObject();
    cJSON_AddStringToObject(t3, "id", "task1");
    cJSON_AddStringToObject(t3, "content", "First");
    cJSON_AddStringToObject(t3, "status", "completed");
    cJSON_AddItemToArray(todos2, t3);
    cJSON_AddItemToObject(input2, "todos", todos2);
    cJSON_AddTrueToObject(input2, "merge");
    char *out2 = NULL;
    todo->execute(input2, &out2, NULL);
    cJSON_Delete(input2);
    ASSERT(strstr(out2, "[x]") != NULL);
    ASSERT(strstr(out2, "Second") != NULL);
    free(out2);

    return 0;
}

int test_tools_extra_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tools_extra");
    RUN_TEST(test_file_edit_replace_all);
    RUN_TEST(test_file_edit_not_unique);
    RUN_TEST(test_file_write_mkdir);
    RUN_TEST(test_listdir);
    RUN_TEST(test_listdir_recursive);
    RUN_TEST(test_grep);
    RUN_TEST(test_bash_multiline);
    RUN_TEST(test_bash_stderr);
    RUN_TEST(test_file_read_binary);
    RUN_TEST(test_todo_merge);
    TEST_RETURN_FAILED();
}
