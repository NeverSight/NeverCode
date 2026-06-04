#include "test.h"
#include "nevercode.h"

static int test_iv_missing_path(void) {
    cJSON *in = cJSON_CreateObject();
    char *out = NULL;
    int rc = image_view_exec(in, &out, NULL);
    cJSON_Delete(in);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(out);
    free(out);
    return 0;
}

static int test_iv_not_image(void) {
    cJSON *in = cJSON_CreateObject();
    cJSON_AddStringToObject(in, "path", "/tmp/not_an_image.txt");
    char *out = NULL;
    int rc = image_view_exec(in, &out, NULL);
    cJSON_Delete(in);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(out);
    free(out);
    return 0;
}

int test_image_view_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("image_view");
    RUN_TEST(test_iv_missing_path);
    RUN_TEST(test_iv_not_image);
    TEST_RETURN_FAILED();
}
