#include "test.h"
#include "nevercode.h"

static int test_commit_attr_default(void) {
    commit_attr_init(NULL);
    const char *val = commit_attr_value();
    ASSERT(val != NULL);
    ASSERT(strstr(val, NC_PRODUCT_NAME) != NULL);
    return 0;
}

static int test_commit_attr_custom(void) {
    commit_attr_init("MyBot <bot@example.com>");
    const char *val = commit_attr_value();
    ASSERT(strcmp(val, "MyBot <bot@example.com>") == 0);
    commit_attr_init(NULL);
    return 0;
}

static int test_commit_attr_trailer(void) {
    commit_attr_init(NULL);
    char *trailer = commit_attr_trailer();
    ASSERT(trailer != NULL);
    ASSERT(nc_starts_with(trailer, "Co-authored-by: "));
    free(trailer);
    return 0;
}

static int test_commit_attr_instruction(void) {
    commit_attr_init(NULL);
    char *inst = commit_attr_instruction();
    ASSERT(inst != NULL);
    ASSERT(strstr(inst, "Co-authored-by") != NULL);
    ASSERT(strstr(inst, "trailer") != NULL);
    ASSERT(strstr(inst, "duplicate") != NULL);
    free(inst);
    return 0;
}

static int test_commit_msg_has_trailer_fn(void) {
    commit_attr_init(NULL);
    char *trailer = commit_attr_trailer();
    char msg[512];
    snprintf(msg, sizeof(msg), "fix: bug\n\n%s\n", trailer);
    ASSERT(commit_msg_has_trailer(msg));
    ASSERT(!commit_msg_has_trailer("fix: bug\n"));
    free(trailer);
    return 0;
}

static int test_commit_msg_ensure_trailer_fn(void) {
    commit_attr_init(NULL);
    char *out = commit_msg_ensure_trailer("fix: bug\n");
    ASSERT(out != NULL);
    ASSERT(strstr(out, "Co-authored-by") != NULL);
    ASSERT(strstr(out, "fix: bug") != NULL);
    free(out);
    return 0;
}

static int test_commit_msg_no_double_trailer(void) {
    commit_attr_init(NULL);
    char *trailer = commit_attr_trailer();
    char msg[512];
    snprintf(msg, sizeof(msg), "fix: bug\n\n%s\n", trailer);
    char *out = commit_msg_ensure_trailer(msg);
    ASSERT(out != NULL);
    char *first = strstr(out, "Co-authored-by");
    char *second = first ? strstr(first + 1, "Co-authored-by") : NULL;
    ASSERT(second == NULL);
    free(out);
    free(trailer);
    return 0;
}

static int test_commit_msg_null(void) {
    ASSERT(!commit_msg_has_trailer(NULL));
    ASSERT(commit_msg_ensure_trailer(NULL) == NULL);
    return 0;
}

int test_commit_attr_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("commit_attr");

    RUN_TEST(test_commit_attr_default);
    RUN_TEST(test_commit_attr_custom);
    RUN_TEST(test_commit_attr_trailer);
    RUN_TEST(test_commit_attr_instruction);
    RUN_TEST(test_commit_msg_has_trailer_fn);
    RUN_TEST(test_commit_msg_ensure_trailer_fn);
    RUN_TEST(test_commit_msg_no_double_trailer);
    RUN_TEST(test_commit_msg_null);

    TEST_RETURN_FAILED();
}
