#include "test.h"
#include "api_messages.h"
static int test_build_empty(void) {
    cJSON *arr = messages_build_payload(NULL, 0);
    ASSERT_NOT_NULL(arr);
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 0);
    cJSON_Delete(arr);
    return 0;
}
static int test_build_single(void) {
    message_t m;
    memset(&m, 0, sizeof(m));
    m.role = ROLE_USER;
    content_block_t blk = {0};
    blk.type = CONTENT_TEXT;
    blk.text = nc_xstrdup("hello");
    m.blocks = &blk;
    m.num_blocks = 1;
    cJSON *arr = messages_build_payload(&m, 1);
    ASSERT_NOT_NULL(arr);
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 1);
    cJSON_Delete(arr);
    free(blk.text);
    return 0;
}
int test_api_messages_basic_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("api_messages_basic");
    RUN_TEST(test_build_empty);
    RUN_TEST(test_build_single);
    TEST_RETURN_FAILED();
}
