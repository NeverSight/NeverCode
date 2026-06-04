#include "test.h"
extern void personality_set(int id);
extern int personality_get(void);
extern int personality_parse(const char *name);
extern const char *personality_name(int id);
extern const char *personality_prompt(void);
extern int personality_count(void);
extern char *personality_list(void);
static int t1(void) {
    personality_set(0);
    ASSERT_INT_EQ(personality_get(), 0);
    ASSERT_NULL(personality_prompt());
    return 0;
}
static int t2(void) {
    int id = personality_parse("concise");
    ASSERT_TRUE(id > 0);
    personality_set(id);
    ASSERT_NOT_NULL(personality_prompt());
    return 0;
}
static int t3(void) {
    ASSERT_INT_EQ(personality_parse("nonexistent"), -1);
    return 0;
}
static int t4(void) {
    ASSERT_INT_EQ(personality_parse(NULL), 0);
    return 0;
}
static int t5(void) {
    personality_set(0);
    char *l = personality_list();
    ASSERT_NOT_NULL(l);
    ASSERT_TRUE(strstr(l, "default") != NULL);
    ASSERT_TRUE(strstr(l, "(active)") != NULL);
    free(l);
    return 0;
}
static int t6(void) {
    int id = personality_parse("ctf");
    ASSERT_TRUE(id > 0);
    personality_set(id);
    ASSERT_NOT_NULL(personality_prompt());
    personality_set(0);
    return 0;
}
static int t7(void) {
    ASSERT_GT(personality_count(), 5);
    return 0;
}
static int t8(void) {
    int prev = personality_get();
    personality_set(-1);
    ASSERT_INT_EQ(personality_get(), prev);
    personality_set(9999);
    ASSERT_INT_EQ(personality_get(), prev);
    return 0;
}
static int t9(void) {
    ASSERT_INT_EQ(personality_parse("  mentor \t"), personality_parse("mentor"));
    ASSERT_TRUE(personality_parse("mentor") > 0);
    return 0;
}
int test_personality_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("personality");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    TEST_RETURN_FAILED();
}
