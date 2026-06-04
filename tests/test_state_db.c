#include "test.h"
#include "nevercode.h"
static int t1(void) {
    state_db_init();
    ASSERT_TRUE(state_db_ready());
    state_db_free();
    ASSERT_FALSE(state_db_ready());
    return 0;
}
static int t2(void) {
    state_db_init();
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "hello", "world");
    ASSERT_INT_EQ(0, state_db_save_session("test_s1", d));
    cJSON_Delete(d);
    ASSERT_TRUE(state_db_session_exists("test_s1"));
    cJSON *r = state_db_load_session("test_s1");
    ASSERT_NOT_NULL(r);
    cJSON *v = cJSON_GetObjectItem(r, "hello");
    ASSERT_STR_EQ("world", v->valuestring);
    cJSON_Delete(r);
    state_db_delete_session("test_s1");
    ASSERT_FALSE(state_db_session_exists("test_s1"));
    state_db_free();
    return 0;
}
static int t3(void) {
    state_db_init();
    cJSON *d = cJSON_CreateObject();
    cJSON_AddNumberToObject(d, "turn", 5);
    ASSERT_INT_EQ(0, state_db_save_rollout("sess_r", 5, d));
    cJSON_Delete(d);
    ASSERT_INT_EQ(1, state_db_count_rollouts("sess_r"));
    cJSON *r = state_db_load_rollout("sess_r", 5);
    ASSERT_NOT_NULL(r);
    cJSON_Delete(r);
    state_db_free();
    return 0;
}
static int t4(void) {
    state_db_init();
    ASSERT_TRUE(state_db_try_lock("mylock", 60));
    ASSERT_FALSE(state_db_try_lock("mylock", 60));
    state_db_unlock("mylock");
    ASSERT_TRUE(state_db_try_lock("mylock", 60));
    state_db_unlock("mylock");
    state_db_free();
    return 0;
}
static int t5(void) {
    state_db_init();
    ASSERT_GE(state_db_total_writes(), 0);
    char *s = state_db_format_stats();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "State DB");
    free(s);
    state_db_free();
    return 0;
}
int test_state_db_all(void) {
    TEST_SUITE("state_db");
    TEST_COUNTERS;
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    TEST_RETURN_FAILED();
}
