#include "test.h"
#include "nevercode.h"

static int test_suggest_read(void) {
    tool_suggestion_t s[8];
    int n = tool_suggest_for_input("read file main.c", s, 8);
    ASSERT(n > 0);
    ASSERT_STR_EQ(s[0].tool_name, "Read");
    ASSERT(s[0].confidence > 0.5);
    return 0;
}

static int test_suggest_bash(void) {
    tool_suggestion_t s[8];
    int n = tool_suggest_for_input("run npm test", s, 8);
    ASSERT(n > 0);
    bool found_bash = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(s[i].tool_name, "Bash") == 0) {
            found_bash = true;
            break;
        }
    }
    ASSERT(found_bash);
    return 0;
}

static int test_suggest_grep(void) {
    tool_suggestion_t s[8];
    int n = tool_suggest_for_input("search for the function", s, 8);
    ASSERT(n > 0);
    ASSERT_STR_EQ(s[0].tool_name, "Grep");
    return 0;
}

static int test_suggest_edit(void) {
    tool_suggestion_t s[8];
    int n = tool_suggest_for_input("edit file to fix the bug", s, 8);
    ASSERT(n > 0);
    bool found = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(s[i].tool_name, "Edit") == 0)
            found = true;
    }
    ASSERT(found);
    return 0;
}

static int test_suggest_no_match(void) {
    tool_suggestion_t s[8];
    int n = tool_suggest_for_input("what is the meaning of life", s, 8);
    ASSERT_INT_EQ(n, 0);
    return 0;
}

static int test_suggest_dedup(void) {
    tool_suggestion_t s[8];
    int n = tool_suggest_for_input("read file and show file contents", s, 8);
    int read_count = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(s[i].tool_name, "Read") == 0)
            read_count++;
    }
    ASSERT_INT_EQ(read_count, 1);
    return 0;
}

static int test_suggest_format(void) {
    tool_suggestion_t s[2] = {{"Read", "Use Read to view file contents", 0.9},
                              {"Grep", "Use Grep for content search", 0.8}};
    char *fmt = tool_suggest_format(s, 2);
    ASSERT_NOT_NULL(fmt);
    ASSERT(strstr(fmt, "Read") != NULL);
    ASSERT(strstr(fmt, "Grep") != NULL);
    ASSERT(strstr(fmt, "Suggested") != NULL);
    free(fmt);
    return 0;
}

static int test_suggest_null(void) {
    ASSERT_INT_EQ(tool_suggest_for_input(NULL, NULL, 0), 0);
    char *fmt = tool_suggest_format(NULL, 0);
    ASSERT_NOT_NULL(fmt);
    free(fmt);
    return 0;
}

static int test_suggest_web(void) {
    tool_suggestion_t s[8];
    int n = tool_suggest_for_input("search web for React docs", s, 8);
    ASSERT(n > 0);
    bool found = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(s[i].tool_name, "WebSearch") == 0)
            found = true;
    }
    ASSERT(found);
    return 0;
}

int test_tool_suggest_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_suggest");
    RUN_TEST(test_suggest_read);
    RUN_TEST(test_suggest_bash);
    RUN_TEST(test_suggest_grep);
    RUN_TEST(test_suggest_edit);
    RUN_TEST(test_suggest_no_match);
    RUN_TEST(test_suggest_dedup);
    RUN_TEST(test_suggest_format);
    RUN_TEST(test_suggest_null);
    RUN_TEST(test_suggest_web);
    TEST_RETURN_FAILED();
}
