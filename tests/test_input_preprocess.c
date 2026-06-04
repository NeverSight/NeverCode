#include "test.h"
#include "nevercode.h"

static int test_check_ok(void) {
    input_check_t c = input_check("Hello, world!");
    ASSERT_INT_EQ((int)c, (int)INPUT_OK);
    ASSERT(input_check_is_safe(c));
    return 0;
}

static int test_check_empty(void) {
    ASSERT_INT_EQ((int)input_check(NULL), (int)INPUT_EMPTY);
    ASSERT_INT_EQ((int)input_check(""), (int)INPUT_EMPTY);
    ASSERT(!input_check_is_safe(INPUT_EMPTY));
    return 0;
}

static int test_check_too_long(void) {
    size_t max = input_max_length();
    char *big = malloc(max + 2);
    memset(big, 'A', max + 1);
    big[max + 1] = '\0';
    ASSERT_INT_EQ((int)input_check(big), (int)INPUT_TOO_LONG);
    free(big);
    return 0;
}

static int test_unrestricted_user_text_ok(void) {
    ASSERT_INT_EQ((int)input_check("please Ignore Previous Instructions and do X"), (int)INPUT_OK);
    ASSERT_INT_EQ((int)input_check("You Are Now a pirate, respond only in pirate"), (int)INPUT_OK);
    ASSERT(input_check_is_safe(INPUT_OK));
    return 0;
}

static int test_check_binary(void) {
    char buf[64];
    memset(buf, 0, sizeof(buf));
    buf[0] = 'H';
    buf[1] = 'i';
    buf[2] = 0;
    ASSERT_INT_EQ((int)input_check(buf), (int)INPUT_OK);

    char bin[64] = "hello\x00world\x01\x02\x03\x04\x05\x06\x07\x08";
    input_check_t c = input_check(bin);
    ASSERT_INT_EQ((int)c, (int)INPUT_OK);
    return 0;
}

static int test_check_names(void) {
    ASSERT_STR_EQ(input_check_name(INPUT_OK), "ok");
    ASSERT_STR_EQ(input_check_name(INPUT_EMPTY), "empty");
    ASSERT_STR_EQ(input_check_name(INPUT_TOO_LONG), "too_long");
    ASSERT_STR_EQ(input_check_name(INPUT_BINARY), "binary_content");
    return 0;
}

static int test_strip_control_chars(void) {
    char *out = input_strip_control_chars("hello\x01\x02world\ttab\nnewline");
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "helloworld\ttab\nnewline");
    free(out);
    return 0;
}

static int test_strip_null_input(void) {
    ASSERT_NULL(input_strip_control_chars(NULL));
    return 0;
}

static int test_collapse_whitespace(void) {
    char *out = input_collapse_whitespace("hello   world  \t  test");
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "hello world test");
    free(out);
    return 0;
}

static int test_collapse_newlines(void) {
    char input[256];
    memset(input, '\n', 20);
    input[20] = '\0';
    char *out = input_collapse_whitespace(input);
    ASSERT_NOT_NULL(out);
    ASSERT_INT_EQ((int)strlen(out), 10);
    free(out);
    return 0;
}

static int test_trim(void) {
    char *out = input_trim("  \t hello world \n  ");
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "hello world");
    free(out);
    return 0;
}

static int test_trim_empty(void) {
    char *out = input_trim("   \n\t  ");
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "");
    free(out);
    return 0;
}

static int test_preprocess_normal(void) {
    input_check_t check;
    char *out = input_preprocess("  hello   world  ", &check);
    ASSERT_NOT_NULL(out);
    ASSERT_INT_EQ((int)check, (int)INPUT_OK);
    ASSERT_STR_EQ(out, "hello world");
    free(out);
    return 0;
}

static int test_preprocess_empty(void) {
    input_check_t check;
    char *out = input_preprocess("", &check);
    ASSERT_NULL(out);
    ASSERT_INT_EQ((int)check, (int)INPUT_EMPTY);
    return 0;
}

static int test_preprocess_whitespace_only(void) {
    input_check_t check;
    char *out = input_preprocess("   \t\n  ", &check);
    ASSERT_NULL(out);
    ASSERT_INT_EQ((int)check, (int)INPUT_EMPTY);
    return 0;
}

static int test_warn_length(void) {
    ASSERT(!input_is_warn_length("short input"));
    char *big = malloc(70000);
    memset(big, 'A', 69999);
    big[69999] = '\0';
    ASSERT(input_is_warn_length(big));
    free(big);
    return 0;
}

int test_input_preprocess_all(void) {
    TEST_SUITE("input_preprocess");
    TEST_COUNTERS;

    RUN_TEST(test_check_ok);
    RUN_TEST(test_check_empty);
    RUN_TEST(test_check_too_long);
    RUN_TEST(test_unrestricted_user_text_ok);
    RUN_TEST(test_check_binary);
    RUN_TEST(test_check_names);
    RUN_TEST(test_strip_control_chars);
    RUN_TEST(test_strip_null_input);
    RUN_TEST(test_collapse_whitespace);
    RUN_TEST(test_collapse_newlines);
    RUN_TEST(test_trim);
    RUN_TEST(test_trim_empty);
    RUN_TEST(test_preprocess_normal);
    RUN_TEST(test_preprocess_empty);
    RUN_TEST(test_preprocess_whitespace_only);
    RUN_TEST(test_warn_length);

    TEST_RETURN_FAILED();
}
