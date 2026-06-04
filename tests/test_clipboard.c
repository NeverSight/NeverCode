#include "test.h"
#include "nevercode.h"
#include "defaults.h"

static int test_available(void) {
    bool avail = clipboard_available();
    (void)avail;
    return 0;
}

static int test_has_image(void) {
    bool has = clipboard_has_image();
    (void)has;
    return 0;
}

static int test_read_text(void) {
    char *text = clipboard_read_text();
    if (text) {
        ASSERT(strlen(text) > 0);
        free(text);
    }
    return 0;
}

static int test_read_image(void) {
    char *path = clipboard_read_image();
    if (path) {
        ASSERT(strlen(path) > 0);
        free(path);
    }
    return 0;
}

static int test_write_text(void) {
#ifdef __APPLE__
    const char *msg = "nevercode_clipboard_test_payload";
    bool ok = clipboard_write_text(msg, strlen(msg));
    ASSERT_TRUE(ok);
    char *readback = clipboard_read_text();
    ASSERT_NOT_NULL(readback);
    ASSERT_STR_CONTAINS(readback, "nevercode_clipboard_test_payload");
    free(readback);
#endif
    return 0;
}

static int test_write_empty(void) {
    ASSERT_TRUE(!clipboard_write_text(NULL, 0));
    ASSERT_TRUE(!clipboard_write_text("x", 0));
    return 0;
}

int test_clipboard_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("clipboard");
    RUN_TEST(test_available);
    RUN_TEST(test_has_image);
    RUN_TEST(test_read_text);
    RUN_TEST(test_read_image);
    RUN_TEST(test_write_text);
    RUN_TEST(test_write_empty);
    TEST_RETURN_FAILED();
}
