#include "test.h"
#include "nevercode.h"
#include "defaults.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int t1(void) {
    memdir_init();
    memdir_free();
    return 0;
}
static int t2(void) {
    char d[] = "/tmp/nc_md_XXXXXX";
    if (!mkdtemp(d))
        return 0;
    memdir_header_t h[4];
    ASSERT_INT_EQ(memdir_scan(d, h, 4), 0);
    rmdir(d);
    return 0;
}
static int t3(void) {
    ASSERT_INT_EQ(memdir_scan(NULL, NULL, 0), 0);
    ASSERT_FALSE(memdir_entrypoint_exists(NULL));
    memdir_free_headers(NULL, 0);
    return 0;
}
static int t4(void) {
    char d[] = "/tmp/nc_md_XXXXXX";
    if (!mkdtemp(d))
        return 0;
    ASSERT_FALSE(memdir_entrypoint_exists(d));
    memdir_write_entrypoint(d, "# Test\n");
    ASSERT_TRUE(memdir_entrypoint_exists(d));
    char *c = NULL;
    ASSERT_INT_EQ(memdir_read_entrypoint(d, &c), 0);
    free(c);
    char ep[256];
    snprintf(ep, sizeof(ep), "%s/%s", d, MEMDIR_ENTRYPOINT);
    unlink(ep);
    rmdir(d);
    return 0;
}
static int t5(void) {
    memdir_header_t h[1];
    memset(h, 0, sizeof(h));
    h[0].filename = (char *)"a.md";
    h[0].filepath = (char *)"/tmp/a.md";
    h[0].description = (char *)"desc";
    char *s = memdir_format_listing(h, 1);
    ASSERT_STR_CONTAINS(s, "a.md");
    free(s);
    return 0;
}

int test_memdir_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("memdir");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    TEST_RETURN_FAILED();
}
