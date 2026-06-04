#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>

typedef struct {
    char id[64];
    char name[64];
    char description[2048];
    bool has_skills;
    bool has_mcp;
    bool installed;
    int mcp_server_count;
} discoverable_plugin_t;
typedef struct {
    discoverable_plugin_t items[64];
    int count;
} discovery_result_t;

int plugin_discover_scan(const char *dir, discovery_result_t *result);
const char *plugin_discover_capability_summary(const discoverable_plugin_t *p);
char *plugin_discover_format(const discovery_result_t *result);

static int test_scan_empty(void) {
    discovery_result_t r;
    int n = plugin_discover_scan("/tmp/nevercode_test_empty_dir_xxx", &r);
    ASSERT(n <= 0);
    return 0;
}
static int test_scan_null(void) {
    ASSERT_INT_EQ(plugin_discover_scan(NULL, NULL), -1);
    return 0;
}
static int test_cap_summary(void) {
    discoverable_plugin_t p = {0};
    ASSERT_STR_EQ(plugin_discover_capability_summary(&p), "basic");
    p.has_skills = true;
    ASSERT_STR_EQ(plugin_discover_capability_summary(&p), "skills");
    p.has_mcp = true;
    ASSERT_STR_EQ(plugin_discover_capability_summary(&p), "skills+mcp");
    p.has_skills = false;
    ASSERT_STR_EQ(plugin_discover_capability_summary(&p), "mcp");
    return 0;
}
static int test_format_empty(void) {
    discovery_result_t r = {0};
    char *s = plugin_discover_format(&r);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "No plugins");
    free(s);
    return 0;
}
static int test_format_with_items(void) {
    discovery_result_t r = {0};
    r.count = 1;
    snprintf(r.items[0].name, sizeof(r.items[0].name), "test_plugin");
    snprintf(r.items[0].id, sizeof(r.items[0].id), "test");
    char *s = plugin_discover_format(&r);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "test_plugin");
    free(s);
    return 0;
}
static int test_home_count(void) {
    int n = plugin_discover_home_plugin_count();
    ASSERT(n >= 0);
    return 0;
}

int test_plugin_discover_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("plugin_discover");
    RUN_TEST(test_scan_empty);
    RUN_TEST(test_scan_null);
    RUN_TEST(test_cap_summary);
    RUN_TEST(test_format_empty);
    RUN_TEST(test_format_with_items);
    RUN_TEST(test_home_count);
    TEST_RETURN_FAILED();
}
