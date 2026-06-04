#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>
static int echo_handler(const cJSON *params, cJSON **result, void *ctx) {
    (void)ctx;
    *result = cJSON_Duplicate(params, 1);
    return 0;
}
static int test_dispatch(void) {
    connector_init();
    ASSERT_INT_EQ(connector_register("echo", echo_handler, NULL), 0);
    char *resp = NULL;
    ASSERT_INT_EQ(
        connector_dispatch(
            "{\"jsonrpc\":\"2.0\",\"method\":\"echo\",\"params\":{\"x\":1},\"id\":1}", &resp),
        0);
    ASSERT_NOT_NULL(resp);
    cJSON *j = cJSON_Parse(resp);
    ASSERT_NOT_NULL(j);
    cJSON *r = cJSON_GetObjectItem(j, "result");
    ASSERT_NOT_NULL(r);
    cJSON *x = cJSON_GetObjectItem(r, "x");
    ASSERT(cJSON_IsNumber(x) && x->valueint == 1);
    cJSON_Delete(j);
    free(resp);
    connector_free();
    return 0;
}
static int test_unknown_method(void) {
    connector_init();
    char *resp = NULL;
    connector_dispatch("{\"jsonrpc\":\"2.0\",\"method\":\"nope\",\"id\":2}", &resp);
    ASSERT_NOT_NULL(resp);
    ASSERT(strstr(resp, "Method not found") != NULL);
    free(resp);
    connector_free();
    return 0;
}
static int test_format(void) {
    connector_init();
    connector_register("ping", echo_handler, NULL);
    char *f = connector_format_methods();
    ASSERT_NOT_NULL(f);
    ASSERT(strstr(f, "ping") != NULL);
    free(f);
    connector_free();
    return 0;
}
int test_connector_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("connector");
    RUN_TEST(test_dispatch);
    RUN_TEST(test_unknown_method);
    RUN_TEST(test_format);
    TEST_RETURN_FAILED();
}
