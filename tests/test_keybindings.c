#include "test.h"
#include "nevercode.h"

static int test_init_defaults(void) {
    keybindings_init();
    ASSERT_GE(keybinding_count(), 8);
    keybindings_free();
    return 0;
}

static int test_remove(void) {
    keybindings_init();
    int before = keybinding_count();
    ASSERT_INT_EQ(keybinding_remove('\t', 0), 0);
    ASSERT_INT_EQ(keybinding_count(), before - 1);
    keybindings_free();
    return 0;
}

static int test_remove_nonexistent(void) {
    keybindings_init();
    int before = keybinding_count();
    ASSERT_INT_EQ(keybinding_remove(0xFF, 99), -1);
    ASSERT_INT_EQ(keybinding_count(), before);
    keybindings_free();
    return 0;
}

static int test_format(void) {
    keybindings_init();
    char *f = keybindings_format();
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, "Keybindings:");
    ASSERT_STR_CONTAINS(f, "Submit");
    ASSERT_STR_CONTAINS(f, "Enter");
    free(f);
    keybindings_free();
    return 0;
}

static int test_double_init(void) {
    keybindings_init();
    int c1 = keybinding_count();
    keybindings_free();
    keybindings_init();
    int c2 = keybinding_count();
    ASSERT_INT_EQ(c1, c2);
    keybindings_free();
    return 0;
}

int test_keybindings_all(void) {
    TEST_SUITE("keybindings");
    TEST_COUNTERS;
    RUN_TEST(test_init_defaults);
    RUN_TEST(test_remove);
    RUN_TEST(test_remove_nonexistent);
    RUN_TEST(test_format);
    RUN_TEST(test_double_init);
    TEST_RETURN_FAILED();
}
