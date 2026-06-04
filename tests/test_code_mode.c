/*
 * test_code_mode.c
 */

#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int test_enter_lang(void) {
    code_mode_init();
    ASSERT_INT_EQ(code_mode_enter("/tmp/x.c", NULL), 0);
    ASSERT(code_mode_active());
    ASSERT_STR_EQ(code_mode_language(), "c");
    char *p = code_mode_build_prompt();
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "x.c") != NULL);
    free(p);
    code_mode_exit();
    ASSERT(!code_mode_active());
    return 0;
}

static int test_intent(void) {
    ASSERT_STR_EQ(code_mode_detect_intent("please refactor this mess"), "refactor");
    ASSERT_STR_EQ(code_mode_detect_intent("fix the null deref"), "fix");
    return 0;
}

static int test_detect_ext(void) {
    ASSERT_STR_EQ(code_mode_detect_language("foo.py"), "python");
    ASSERT_STR_EQ(code_mode_detect_language("bar.rs"), "rust");
    return 0;
}

int test_code_mode_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("code_mode");
    RUN_TEST(test_enter_lang);
    RUN_TEST(test_intent);
    RUN_TEST(test_detect_ext);
    TEST_RETURN_FAILED();
}
