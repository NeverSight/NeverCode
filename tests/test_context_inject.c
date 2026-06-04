#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <unistd.h>

static int test_no_at_sign(void) {
    char *r = context_inject_refs("hello world");
    ASSERT_NULL(r);
    return 0;
}

static int test_at_nonexistent(void) {
    char *r = context_inject_refs("read @/tmp/does_not_exist_test_12345.txt");
    ASSERT_NULL(r);
    return 0;
}

static int test_at_existing_file(void) {
    const char *path = "/tmp/nevercode_test_ctx_inject.txt";
    FILE *f = fopen(path, "w");
    fputs("hello from file", f);
    fclose(f);

    char input[256];
    snprintf(input, sizeof(input), "read @%s please", path);
    char *r = context_inject_refs(input);
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "hello from file") != NULL);
    ASSERT(strstr(r, "<file path=") != NULL);
    ASSERT(strstr(r, path) != NULL);
    free(r);
    unlink(path);
    return 0;
}

static int test_at_mid_word(void) {
    char *r = context_inject_refs("email@example.com");
    ASSERT_NULL(r);
    return 0;
}

static int test_null_input(void) {
    char *r = context_inject_refs(NULL);
    ASSERT_NULL(r);
    return 0;
}

static int test_at_only(void) {
    char *r = context_inject_refs("@");
    ASSERT_NULL(r);
    return 0;
}

static int test_multiple_at_refs(void) {
    const char *p1 = "/tmp/nevercode_ctx_test1.txt";
    const char *p2 = "/tmp/nevercode_ctx_test2.txt";
    FILE *f1 = fopen(p1, "w");
    fputs("content1", f1);
    fclose(f1);
    FILE *f2 = fopen(p2, "w");
    fputs("content2", f2);
    fclose(f2);

    char input[512];
    snprintf(input, sizeof(input), "compare @%s and @%s", p1, p2);
    char *r = context_inject_refs(input);
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "content1") != NULL);
    ASSERT(strstr(r, "content2") != NULL);
    free(r);
    unlink(p1);
    unlink(p2);
    return 0;
}

int test_context_inject_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("context_inject");
    RUN_TEST(test_no_at_sign);
    RUN_TEST(test_at_nonexistent);
    RUN_TEST(test_at_existing_file);
    RUN_TEST(test_at_mid_word);
    RUN_TEST(test_null_input);
    RUN_TEST(test_at_only);
    RUN_TEST(test_multiple_at_refs);
    TEST_RETURN_FAILED();
}
