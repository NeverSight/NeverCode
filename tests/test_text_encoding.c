#include "test.h"
#include "nevercode.h"

static int test_ascii_text(void) {
    const unsigned char data[] = "Hello, world!\nLine 2.\n";
    ASSERT(!text_is_binary_data(data, sizeof(data) - 1));
    ASSERT(strcmp(text_encoding_name(data, sizeof(data) - 1), "ascii") == 0);
    return 0;
}

static int test_utf8_text(void) {
    const unsigned char data[] = "Hello \xe4\xb8\x96\xe7\x95\x8c!";
    ASSERT(!text_is_binary_data(data, sizeof(data) - 1));
    ASSERT(strcmp(text_encoding_name(data, sizeof(data) - 1), "utf-8") == 0);
    return 0;
}

static int test_binary_nulls(void) {
    const unsigned char data[] = {0x00, 0x01, 0x02, 0x00, 0x03, 0x00, 0x04};
    ASSERT(text_is_binary_data(data, sizeof(data)));
    return 0;
}

static int test_binary_ctrl(void) {
    unsigned char data[100];
    memset(data, 0x05, sizeof(data));
    ASSERT(text_is_binary_data(data, sizeof(data)));
    return 0;
}

static int test_empty_data(void) {
    ASSERT(!text_is_binary_data(NULL, 0));
    ASSERT(strcmp(text_encoding_name(NULL, 0), "ascii") == 0);
    return 0;
}

static int test_binary_file_extension(void) {
    ASSERT(text_is_binary_file("image.png"));
    ASSERT(text_is_binary_file("archive.zip"));
    ASSERT(text_is_binary_file("lib.so"));
    ASSERT(text_is_binary_file("doc.pdf"));
    return 0;
}

static int test_non_binary_extension(void) {
    ASSERT(!text_is_binary_file("script.py"));
    return 0;
}

static int test_valid_utf8_prefix(void) {
    const unsigned char valid[] = "hello\xe4\xb8\x96";
    size_t prefix = text_valid_utf8_prefix(valid, sizeof(valid) - 1);
    ASSERT(prefix == sizeof(valid) - 1);
    return 0;
}

static int test_invalid_utf8_stops(void) {
    const unsigned char data[] = {'h', 'i', 0xFF, 'x'};
    size_t prefix = text_valid_utf8_prefix(data, sizeof(data));
    ASSERT(prefix == 2);
    return 0;
}

static int test_sanitize_clean(void) {
    const unsigned char data[] = "hello world";
    char *out = text_sanitize_for_display(data, sizeof(data) - 1);
    ASSERT(out != NULL);
    ASSERT(strcmp(out, "hello world") == 0);
    free(out);
    return 0;
}

static int test_sanitize_binary(void) {
    const unsigned char data[] = {'h', 'i', 0xFF, 0xFE};
    char *out = text_sanitize_for_display(data, sizeof(data));
    ASSERT(out != NULL);
    ASSERT(strstr(out, "binary data") != NULL);
    free(out);
    return 0;
}

static int test_sanitize_null(void) {
    char *out = text_sanitize_for_display(NULL, 0);
    ASSERT(out != NULL);
    ASSERT(strcmp(out, "") == 0);
    free(out);
    return 0;
}

static int test_surrogates_detect(void) {
    ASSERT_TRUE(!text_has_surrogates(NULL, 0));
    ASSERT_TRUE(!text_has_surrogates("hello", 5));
    /* U+D800 in UTF-8: ED A0 80 */
    const char with_surr[] = "ab\xED\xA0\x80"
                             "cd";
    ASSERT_TRUE(text_has_surrogates(with_surr, sizeof(with_surr) - 1));
    return 0;
}

static int test_surrogates_strip(void) {
    char *r = text_strip_surrogates("hello", 5);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ("hello", r);
    free(r);

    /* U+D800 in UTF-8: ED A0 80 — should be removed */
    const char input[] = "ab\xED\xA0\x80"
                         "cd";
    r = text_strip_surrogates(input, sizeof(input) - 1);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ("abcd", r);
    free(r);

    r = text_strip_surrogates(NULL, 0);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ("", r);
    free(r);
    return 0;
}

static int test_surrogates_strip_multi(void) {
    /* Two surrogates: U+D800 and U+DFFF */
    const char input[] = "x\xED\xA0\x80y\xED\xBF\xBFz";
    char *r = text_strip_surrogates(input, sizeof(input) - 1);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ("xyz", r);
    free(r);
    return 0;
}

static int test_trunc_suffix_fits(void) {
    const char suffix[] = " [... binary data truncated]";
    ASSERT((int)sizeof(suffix) <= NC_TRUNC_SUFFIX_MAX);
    return 0;
}

static int test_sanitize_binary_prefix_preserved(void) {
    const unsigned char data[] = {'A', 'B', 'C', 0xFF, 0xFE, 0xFD};
    char *out = text_sanitize_for_display(data, sizeof(data));
    ASSERT_NOT_NULL(out);
    ASSERT(out[0] == 'A');
    ASSERT(out[1] == 'B');
    ASSERT(out[2] == 'C');
    ASSERT(strstr(out, "binary data") != NULL);
    free(out);
    return 0;
}

static int test_utf8_4byte(void) {
    /* U+1F600 (grinning face) in UTF-8: F0 9F 98 80 */
    const unsigned char data[] = "hi\xF0\x9F\x98\x80!";
    ASSERT(!text_is_binary_data(data, sizeof(data) - 1));
    ASSERT_STR_EQ("utf-8", text_encoding_name(data, sizeof(data) - 1));
    size_t prefix = text_valid_utf8_prefix(data, sizeof(data) - 1);
    ASSERT(prefix == sizeof(data) - 1);
    return 0;
}

static int test_latin1_detected(void) {
    /* High bytes that aren't valid UTF-8 continuation */
    const unsigned char data[] = "caf\xe9 cr\xe8me";
    ASSERT(!text_is_binary_data(data, sizeof(data) - 1));
    ASSERT_STR_EQ("latin-1", text_encoding_name(data, sizeof(data) - 1));
    return 0;
}

int test_text_encoding_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("text_encoding");

    RUN_TEST(test_ascii_text);
    RUN_TEST(test_utf8_text);
    RUN_TEST(test_utf8_4byte);
    RUN_TEST(test_latin1_detected);
    RUN_TEST(test_binary_nulls);
    RUN_TEST(test_binary_ctrl);
    RUN_TEST(test_empty_data);
    RUN_TEST(test_binary_file_extension);
    RUN_TEST(test_non_binary_extension);
    RUN_TEST(test_valid_utf8_prefix);
    RUN_TEST(test_invalid_utf8_stops);
    RUN_TEST(test_sanitize_clean);
    RUN_TEST(test_sanitize_binary);
    RUN_TEST(test_sanitize_binary_prefix_preserved);
    RUN_TEST(test_sanitize_null);
    RUN_TEST(test_trunc_suffix_fits);
    RUN_TEST(test_surrogates_detect);
    RUN_TEST(test_surrogates_strip);
    RUN_TEST(test_surrogates_strip_multi);

    TEST_RETURN_FAILED();
}
