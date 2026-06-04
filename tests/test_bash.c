#include "test.h"
#include "tools.h"
#include "nevercode.h"

static int test_register(void) {
    tool_bash_register();
    const tool_def_t *t = tools_find("Bash");
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t->name, "Bash");
    return 0;
}

static int test_schema(void) {
    const tool_def_t *t = tools_find("Bash");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    ASSERT_NOT_NULL(t->description);
    ASSERT_TRUE(strlen(t->description) > 10);
    return 0;
}

static int test_readonly_ls(void) {
    ASSERT_TRUE(bash_is_readonly("ls -la"));
    return 0;
}

static int test_readonly_cat(void) {
    ASSERT_TRUE(bash_is_readonly("cat README.md"));
    return 0;
}

static int test_not_readonly_rm(void) {
    ASSERT_FALSE(bash_is_readonly("rm -rf /tmp"));
    return 0;
}

static int test_not_readonly_write(void) {
    ASSERT_FALSE(bash_is_readonly("echo x > file.txt"));
    return 0;
}

static int test_check_ro_json(void) {
    cJSON *i = cJSON_CreateObject();
    cJSON_AddStringToObject(i, "command", "ls");
    ASSERT_TRUE(bash_check_readonly(i));
    cJSON_Delete(i);
    return 0;
}

static int test_check_rw_json(void) {
    cJSON *i = cJSON_CreateObject();
    cJSON_AddStringToObject(i, "command", "rm file");
    ASSERT_FALSE(bash_check_readonly(i));
    cJSON_Delete(i);
    return 0;
}

static int test_semantic_grep_no_match(void) {
    const char *label = bash_semantic_label("grep pattern file.txt", 1);
    ASSERT_NOT_NULL(label);
    ASSERT_TRUE(strstr(label, "no matches") != NULL);
    return 0;
}

static int test_semantic_grep_error(void) {
    const char *label = bash_semantic_label("grep pattern file.txt", 2);
    ASSERT_NULL(label);
    return 0;
}

static int test_semantic_diff_differs(void) {
    const char *label = bash_semantic_label("diff a.txt b.txt", 1);
    ASSERT_NOT_NULL(label);
    ASSERT_TRUE(strstr(label, "differ") != NULL);
    return 0;
}

static int test_semantic_test_false(void) {
    const char *label = bash_semantic_label("test -f nonexist", 1);
    ASSERT_NOT_NULL(label);
    ASSERT_TRUE(strstr(label, "false") != NULL);
    return 0;
}

static int test_semantic_rg_no_match(void) {
    const char *label = bash_semantic_label("rg pattern", 1);
    ASSERT_NOT_NULL(label);
    return 0;
}

static int test_semantic_ls_unrecognized(void) {
    const char *label = bash_semantic_label("ls -la", 1);
    ASSERT_NULL(label);
    return 0;
}

static int test_semantic_zero_exit(void) {
    const char *label = bash_semantic_label("grep pattern", 0);
    ASSERT_NULL(label);
    return 0;
}

static int test_semantic_null_cmd(void) {
    const char *label = bash_semantic_label(NULL, 1);
    ASSERT_NULL(label);
    return 0;
}

static int test_semantic_pipe_grep(void) {
    const char *label = bash_semantic_label("cat file.txt | grep pattern", 1);
    ASSERT_NOT_NULL(label);
    ASSERT_TRUE(strstr(label, "no matches") != NULL);
    return 0;
}

static int test_semantic_pipe_diff(void) {
    const char *label = bash_semantic_label("sort a | diff - b", 1);
    ASSERT_NOT_NULL(label);
    ASSERT_TRUE(strstr(label, "differ") != NULL);
    return 0;
}

static int test_semantic_logical_or_not_pipe(void) {
    const char *label = bash_semantic_label("ls || echo fallback", 1);
    ASSERT_NULL(label);
    return 0;
}

int test_bash_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("bash");
    RUN_TEST(test_register);
    RUN_TEST(test_schema);
    RUN_TEST(test_readonly_ls);
    RUN_TEST(test_readonly_cat);
    RUN_TEST(test_not_readonly_rm);
    RUN_TEST(test_not_readonly_write);
    RUN_TEST(test_check_ro_json);
    RUN_TEST(test_check_rw_json);
    RUN_TEST(test_semantic_grep_no_match);
    RUN_TEST(test_semantic_grep_error);
    RUN_TEST(test_semantic_diff_differs);
    RUN_TEST(test_semantic_test_false);
    RUN_TEST(test_semantic_rg_no_match);
    RUN_TEST(test_semantic_ls_unrecognized);
    RUN_TEST(test_semantic_zero_exit);
    RUN_TEST(test_semantic_null_cmd);
    RUN_TEST(test_semantic_pipe_grep);
    RUN_TEST(test_semantic_pipe_diff);
    RUN_TEST(test_semantic_logical_or_not_pipe);
    TEST_RETURN_FAILED();
}
