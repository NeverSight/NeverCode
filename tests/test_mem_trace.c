#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int test_trace_empty(void) {
    built_memory_t m[4];
    ASSERT_INT_EQ(mem_trace_build(NULL, 0, m, 4), 0);
    return 0;
}
static int test_trace_json(void) {
    const char *p = "/tmp/test_trace_nc.json";
    FILE *f = fopen(p, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "[{\"type\":\"message\",\"role\":\"user\",\"content\":\"hello\"}]");
    fclose(f);
    built_memory_t m[4];
    const char *paths[] = {p};
    ASSERT_INT_EQ(mem_trace_build(paths, 1, m, 4), 1);
    ASSERT_STR_CONTAINS(m[0].raw_text, "hello");
    mem_trace_free_memories(m, 1);
    remove(p);
    return 0;
}
static int test_trace_bom(void) {
    const char *p = "/tmp/test_trace_bom.json";
    FILE *f = fopen(p, "wb");
    ASSERT_NOT_NULL(f);
    unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    fwrite(bom, 1, 3, f);
    fprintf(f, "[{\"type\":\"message\",\"role\":\"user\",\"content\":\"bom\"}]");
    fclose(f);
    built_memory_t m[4];
    const char *paths[] = {p};
    ASSERT_INT_EQ(mem_trace_build(paths, 1, m, 4), 1);
    ASSERT_STR_CONTAINS(m[0].raw_text, "bom");
    mem_trace_free_memories(m, 1);
    remove(p);
    return 0;
}
static int test_trace_missing(void) {
    built_memory_t m[4];
    const char *paths[] = {"/tmp/does_not_exist_nc.json"};
    ASSERT_INT_EQ(mem_trace_build(paths, 1, m, 4), 0);
    return 0;
}
static int test_trace_format(void) {
    built_memory_t m;
    memset(&m, 0, sizeof(m));
    m.memory_id = nc_xstrdup("tid");
    m.source_path = nc_xstrdup("/t.json");
    m.raw_text = nc_xstrdup("x");
    char *s = mem_trace_format(&m, 1);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "tid");
    free(s);
    mem_trace_free_memories(&m, 1);
    return 0;
}
int test_mem_trace_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("mem_trace");
    RUN_TEST(test_trace_empty);
    RUN_TEST(test_trace_json);
    RUN_TEST(test_trace_bom);
    RUN_TEST(test_trace_missing);
    RUN_TEST(test_trace_format);
    TEST_RETURN_FAILED();
}
