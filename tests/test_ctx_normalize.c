#include "test.h"
#include "nevercode.h"
#include "../third_party/cJSON/cJSON.h"

/* Forward declarations from ctx_normalize.c */
int ctx_normalize_ensure_outputs(cJSON *items);
int ctx_normalize_remove_orphan_outputs(cJSON *items);
char *ctx_normalize_strip_citations(const char *text);
char *ctx_normalize_strip_plan_tags(const char *text);
char *ctx_normalize_strip_markup(const char *text, int plan_mode);
int ctx_normalize_replace_images(cJSON *items, int model_supports_vision);
int ctx_normalize_all(cJSON *items, int plan_mode, int has_vision);

static int test_strip_citations(void) {
    char *r = ctx_normalize_strip_citations("Hello 【cite1】 world 【cite2】!");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "Hello  world !");
    free(r);
    return 0;
}

static int test_strip_citations_none(void) {
    char *r = ctx_normalize_strip_citations("No citations here");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "No citations here");
    free(r);
    return 0;
}

static int test_strip_plan_tags(void) {
    char *r =
        ctx_normalize_strip_plan_tags("before<proposed_plan>plan content</proposed_plan>after");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "beforeafter");
    free(r);
    return 0;
}

static int test_strip_markup_no_plan(void) {
    char *r = ctx_normalize_strip_markup("Text 【cite】 here", 0);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "Text  here");
    free(r);
    return 0;
}

static int test_strip_markup_with_plan(void) {
    char *r = ctx_normalize_strip_markup("A 【c】<proposed_plan>P</proposed_plan>B", 1);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "A B");
    free(r);
    return 0;
}

static int test_strip_null(void) {
    ASSERT_NULL(ctx_normalize_strip_citations(NULL));
    ASSERT_NULL(ctx_normalize_strip_plan_tags(NULL));
    ASSERT_NULL(ctx_normalize_strip_markup(NULL, 0));
    return 0;
}

static int test_ensure_outputs_none_missing(void) {
    cJSON *arr = cJSON_CreateArray();
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", "hello");
    cJSON_AddItemToArray(arr, msg);

    int r = ctx_normalize_ensure_outputs(arr);
    ASSERT_INT_EQ(r, 0);
    cJSON_Delete(arr);
    return 0;
}

static int test_replace_images_no_vision(void) {
    cJSON *arr = cJSON_CreateArray();
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON *content = cJSON_AddArrayToObject(msg, "content");
    cJSON *part = cJSON_CreateObject();
    cJSON_AddStringToObject(part, "type", "image_url");
    cJSON *img = cJSON_CreateObject();
    cJSON_AddStringToObject(img, "url", "data:image/png;base64,xxx");
    cJSON_AddItemToObject(part, "image_url", img);
    cJSON_AddItemToArray(content, part);
    cJSON_AddItemToArray(arr, msg);

    int r = ctx_normalize_replace_images(arr, 0);
    ASSERT_INT_EQ(r, 1);

    cJSON *type = cJSON_GetObjectItem(part, "type");
    ASSERT_STR_EQ(type->valuestring, "text");
    cJSON_Delete(arr);
    return 0;
}

static int test_remove_orphan_outputs(void) {
    cJSON *arr = cJSON_CreateArray();

    /* assistant with a tool_call id=tc_1 */
    cJSON *amsg = cJSON_CreateObject();
    cJSON_AddStringToObject(amsg, "role", "assistant");
    cJSON *tc = cJSON_AddArrayToObject(amsg, "tool_calls");
    cJSON *call = cJSON_CreateObject();
    cJSON_AddStringToObject(call, "id", "tc_1");
    cJSON_AddItemToArray(tc, call);
    cJSON_AddItemToArray(arr, amsg);

    /* matching tool output for tc_1 */
    cJSON *t1 = cJSON_CreateObject();
    cJSON_AddStringToObject(t1, "role", "tool");
    cJSON_AddStringToObject(t1, "call_id", "tc_1");
    cJSON_AddStringToObject(t1, "content", "ok");
    cJSON_AddItemToArray(arr, t1);

    /* orphan tool output with no matching call */
    cJSON *orphan = cJSON_CreateObject();
    cJSON_AddStringToObject(orphan, "role", "tool");
    cJSON_AddStringToObject(orphan, "call_id", "tc_missing");
    cJSON_AddStringToObject(orphan, "content", "stale data");
    cJSON_AddItemToArray(arr, orphan);

    int r = ctx_normalize_remove_orphan_outputs(arr);
    ASSERT_INT_EQ(r, 1);
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 2);
    cJSON_Delete(arr);
    return 0;
}

static int test_remove_orphan_outputs_none(void) {
    cJSON *arr = cJSON_CreateArray();
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", "hello");
    cJSON_AddItemToArray(arr, msg);

    int r = ctx_normalize_remove_orphan_outputs(arr);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 1);
    cJSON_Delete(arr);
    return 0;
}

static int test_replace_images_with_vision(void) {
    cJSON *arr = cJSON_CreateArray();
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON *content = cJSON_AddArrayToObject(msg, "content");
    cJSON *part = cJSON_CreateObject();
    cJSON_AddStringToObject(part, "type", "image_url");
    cJSON_AddItemToArray(content, part);
    cJSON_AddItemToArray(arr, msg);

    int r = ctx_normalize_replace_images(arr, 1);
    ASSERT_INT_EQ(r, 0);
    cJSON_Delete(arr);
    return 0;
}

static int test_norm_macros_defined(void) {
    ASSERT(strlen(CTX_NORM_ABORTED_TEXT) > 0);
    ASSERT(strlen(CTX_NORM_IMG_OMIT) > 0);
    ASSERT(strlen(CTX_NORM_CITATION_OPEN) > 0);
    ASSERT(strlen(CTX_NORM_CITATION_CLOSE) > 0);
    ASSERT(strlen(CTX_NORM_PLAN_OPEN) > 0);
    ASSERT(strlen(CTX_NORM_PLAN_CLOSE) > 0);
    return 0;
}

int test_ctx_normalize_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("ctx_normalize");
    RUN_TEST(test_strip_citations);
    RUN_TEST(test_strip_citations_none);
    RUN_TEST(test_strip_plan_tags);
    RUN_TEST(test_strip_markup_no_plan);
    RUN_TEST(test_strip_markup_with_plan);
    RUN_TEST(test_strip_null);
    RUN_TEST(test_ensure_outputs_none_missing);
    RUN_TEST(test_replace_images_no_vision);
    RUN_TEST(test_replace_images_with_vision);
    RUN_TEST(test_remove_orphan_outputs);
    RUN_TEST(test_remove_orphan_outputs_none);
    RUN_TEST(test_norm_macros_defined);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("ctx_normalize");
    RUN_TEST(test_strip_citations);
    RUN_TEST(test_strip_citations_none);
    RUN_TEST(test_strip_plan_tags);
    RUN_TEST(test_strip_markup_no_plan);
    RUN_TEST(test_strip_markup_with_plan);
    RUN_TEST(test_strip_null);
    RUN_TEST(test_ensure_outputs_none_missing);
    RUN_TEST(test_replace_images_no_vision);
    RUN_TEST(test_replace_images_with_vision);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
