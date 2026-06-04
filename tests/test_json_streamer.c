#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static int g_cb_cnt;
static void tcb(const char *k, const char *v, int vl, bool c, void *x) {
    (void)x;
    (void)v;
    (void)vl;
    (void)c;
    g_cb_cnt++;
}
static void t1(void) {
    g_cb_cnt = 0;
    json_streamer_t *js = json_streamer_create(tcb, NULL);
    assert(js);
    const char *d = "{\"path\":\"/src/main.c\",\"line\":42}";
    int rc = json_streamer_feed(js, d, (int)strlen(d));
    assert(rc == 1);
    assert(json_streamer_complete(js));
    assert(json_streamer_field_count(js) == 2);
    cJSON *j = json_streamer_to_json(js);
    assert(j);
    cJSON_Delete(j);
    json_streamer_free(js);
    printf("  test_basic_object                                 \033[32mPASS\033[0m\n");
}
static void t2(void) {
    json_streamer_t *js = json_streamer_create(tcb, NULL);
    json_streamer_feed(js, "{\"name\"", 7);
    assert(!json_streamer_complete(js));
    int rc = json_streamer_feed(js, ": \"hello\"}", 10);
    assert(rc == 1);
    assert(json_streamer_complete(js));
    json_streamer_free(js);
    printf("  test_incremental_feed                             \033[32mPASS\033[0m\n");
}
static void t3(void) {
    json_streamer_t *js = json_streamer_pool_acquire(tcb, NULL);
    assert(js);
    json_streamer_feed(js, "{\"x\":1}", 7);
    json_streamer_pool_release(js);
    printf("  test_pool                                         \033[32mPASS\033[0m\n");
}
int test_json_streamer_all(void) {
    printf("\n\033[1m[json_streamer]\033[0m\n");
    t1();
    t2();
    t3();
    printf("test_json_streamer: 3 tests passed\n");
    return 0;
}
