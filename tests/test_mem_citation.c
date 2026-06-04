#include "test.h"
#include "nevercode.h"
#include <unistd.h>
static int t1(void) {
    mem_citation_ctx_t c;
    mem_citation_init(&c);
    ASSERT_INT_EQ(c.count, 0);
    mem_citation_free(&c);
    return 0;
}
static int t2(void) {
    mem_citation_ctx_t c;
    mem_citation_init(&c);
    ASSERT_INT_EQ(mem_citation_add(&c, "p", "s1", 5, "mk"), 0);
    ASSERT_INT_EQ(c.count, 1);
    ASSERT_STR_EQ(c.entries[0].memory_key, "p");
    mem_citation_free(&c);
    return 0;
}
static int t3(void) {
    mem_citation_ctx_t c;
    mem_citation_init(&c);
    mem_citation_add(&c, "ka", "s1", 1, "a");
    mem_citation_add(&c, "kb", "s2", 2, "b");
    mem_citation_add(&c, "ka", "s3", 3, "c");
    const mem_citation_entry_t *o[8];
    ASSERT_INT_EQ(mem_citation_find(&c, "ka", o, 8), 2);
    mem_citation_free(&c);
    return 0;
}
static int t4(void) {
    mem_citation_ctx_t c;
    mem_citation_init(&c);
    mem_citation_add(&c, "k1", "sx", 1, NULL);
    mem_citation_add(&c, "k2", "sy", 2, NULL);
    mem_citation_add(&c, "k3", "sx", 3, NULL);
    const mem_citation_entry_t *o[8];
    ASSERT_INT_EQ(mem_citation_by_session(&c, "sx", o, 8), 2);
    mem_citation_free(&c);
    return 0;
}
static int t5(void) {
    mem_citation_ctx_t c;
    mem_citation_init(&c);
    mem_citation_add(&c, "rm", "s1", 1, NULL);
    mem_citation_add(&c, "keep", "s2", 2, NULL);
    mem_citation_add(&c, "rm", "s3", 3, NULL);
    ASSERT_INT_EQ(mem_citation_remove(&c, "rm"), 2);
    ASSERT_INT_EQ(c.count, 1);
    mem_citation_free(&c);
    return 0;
}
static int t6(void) {
    char p[256];
    snprintf(p, sizeof(p), "/tmp/nc_cit_%d.json", getpid());
    mem_citation_ctx_t c1;
    mem_citation_init(&c1);
    mem_citation_add(&c1, "sk", "s100", 7, "txt");
    ASSERT_INT_EQ(mem_citation_save(&c1, p), 0);
    mem_citation_ctx_t c2;
    mem_citation_init(&c2);
    ASSERT_INT_EQ(mem_citation_load(&c2, p), 1);
    ASSERT_STR_EQ(c2.entries[0].memory_key, "sk");
    mem_citation_free(&c1);
    mem_citation_free(&c2);
    unlink(p);
    return 0;
}
static int t7(void) {
    mem_citation_ctx_t c;
    mem_citation_init(&c);
    char *s = mem_citation_format(&c);
    ASSERT_STR_CONTAINS(s, "No memory");
    free(s);
    mem_citation_add(&c, "fk", "fs", 2, NULL);
    s = mem_citation_format(&c);
    ASSERT_STR_CONTAINS(s, "fk");
    free(s);
    mem_citation_free(&c);
    return 0;
}
static int t8(void) {
    mem_citation_ctx_t c;
    mem_citation_init(&c);
    mem_citation_add(&c, "k1", "sa", 1, NULL);
    mem_citation_add(&c, "k2", "sb", 2, NULL);
    mem_citation_add(&c, "k3", "sa", 3, NULL);
    ASSERT_INT_EQ(mem_citation_unique_sessions(&c), 2);
    mem_citation_free(&c);
    return 0;
}
static int t9(void) {
    ASSERT_INT_EQ(mem_citation_count(NULL), 0);
    ASSERT_INT_EQ(mem_citation_add(NULL, "k", "s", 0, NULL), -1);
    return 0;
}
static int ta(void) {
    ASSERT_NOT_NULL(mem_citation_global());
    return 0;
}
int test_mem_citation_all(void) {
    TEST_SUITE("mem_citation");
    TEST_COUNTERS;
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    RUN_TEST(ta);
    TEST_RETURN_FAILED();
}
