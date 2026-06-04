#include "test.h"
#include "nevercode.h"
static int t1(void) {
    magic_docs_init();
    ASSERT_TRUE(magic_docs_enabled());
    ASSERT_INT_EQ(magic_docs_count(), 0);
    magic_docs_free();
    return 0;
}
static int t2(void) {
    magic_docs_init();
    ASSERT_TRUE(magic_docs_detect("/x", "# MAGIC DOC: Arch\n\nBody"));
    ASSERT_FALSE(magic_docs_detect("/x", "# Normal\n"));
    ASSERT_FALSE(magic_docs_detect(NULL, NULL));
    magic_docs_free();
    return 0;
}
static int t3(void) {
    magic_docs_init();
    ASSERT_INT_EQ(magic_docs_register("/tmp/t.md", "# MAGIC DOC: Test Guide\n\nBody"), 0);
    ASSERT_INT_EQ(magic_docs_count(), 1);
    ASSERT_STR_EQ(magic_docs_title(0), "Test Guide");
    magic_docs_free();
    return 0;
}
static int t4(void) {
    magic_docs_init();
    magic_docs_set_enabled(false);
    ASSERT_INT_EQ(magic_docs_register("/x", "# MAGIC DOC: X\n"), -1);
    magic_docs_free();
    return 0;
}
static int t5(void) {
    magic_docs_init();
    magic_docs_register("/a", "# MAGIC DOC: Alpha\n\nC");
    magic_docs_record_update(0);
    ASSERT_INT_EQ(magic_docs_updates(), 1);
    char *s = magic_docs_format();
    ASSERT_STR_CONTAINS(s, "Alpha");
    free(s);
    magic_docs_free();
    return 0;
}
int test_magic_docs_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("magic_docs");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    TEST_RETURN_FAILED();
}
