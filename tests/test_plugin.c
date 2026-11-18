#include "test.h"
#include "nevercode.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
static int gi = 0, gu = 0, gh = 0;
static void *mi(void) {
    gi++;
    return (void *)42;
}
static void mu(void *c) {
    gu++;
    (void)c;
}
static int mh(plugin_event_t ev, const char *d, char **r, void *c) {
    (void)d;
    (void)r;
    (void)c;
    gh++;
    return ev == PLUGIN_ON_STARTUP ? 1 : 0;
}
static int t1(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    ASSERT_INT_EQ(r.count, 0);
    ASSERT_TRUE(r.enabled);
    plugin_registry_free(&r);
    return 0;
}
static int t2(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    ASSERT_INT_EQ(plugin_register(&r, "p", mi, mu, mh), 0);
    ASSERT_INT_EQ(plugin_count(&r), 1);
    ASSERT_INT_EQ(plugin_register(&r, "p", mi, mu, mh), -1);
    plugin_registry_free(&r);
    return 0;
}
static int t3(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    gi = gu = 0;
    plugin_register(&r, "l", mi, mu, mh);
    ASSERT_INT_EQ(plugin_load(&r, "l"), 0);
    ASSERT_INT_EQ(gi, 1);
    ASSERT_INT_EQ(plugin_loaded_count(&r), 1);
    ASSERT_INT_EQ(plugin_unload(&r, "l"), 0);
    ASSERT_INT_EQ(gu, 1);
    plugin_registry_free(&r);
    return 0;
}
static int t4(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    gh = 0;
    plugin_register(&r, "d", mi, mu, mh);
    plugin_load(&r, "d");
    ASSERT_INT_EQ(plugin_dispatch(&r, PLUGIN_ON_STARTUP, NULL, NULL), 1);
    ASSERT_INT_EQ(gh, 1);
    plugin_registry_free(&r);
    return 0;
}
static int t5(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    gh = 0;
    plugin_register(&r, "x", mi, mu, mh);
    plugin_load(&r, "x");
    r.enabled = false;
    ASSERT_INT_EQ(plugin_dispatch(&r, PLUGIN_ON_STARTUP, NULL, NULL), 0);
    plugin_registry_free(&r);
    return 0;
}
static int t6(void) {
    ASSERT_STR_EQ(plugin_state_name(PLUGIN_LOADED), "loaded");
    ASSERT_STR_EQ(plugin_event_name(PLUGIN_ON_STARTUP), "startup");
    return 0;
}
static int t7(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    char *s = plugin_format_list(&r);
    ASSERT_STR_CONTAINS(s, "No plugins");
    free(s);
    plugin_register(&r, "f", mi, mu, mh);
    plugin_load(&r, "f");
    s = plugin_format_list(&r);
    ASSERT_STR_CONTAINS(s, "f");
    free(s);
    plugin_registry_free(&r);
    return 0;
}
static int t8(void) {
    ASSERT_NOT_NULL(plugin_registry_global());
    return 0;
}
static int t9(void) {
    ASSERT_INT_EQ(plugin_register(NULL, "x", NULL, NULL, NULL), -1);
    ASSERT_INT_EQ(plugin_count(NULL), 0);
    return 0;
}
static int ta(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    ASSERT_INT_EQ(plugin_load(&r, "no"), -1);
    ASSERT_INT_EQ(plugin_load_so(&r, "/no.so"), -1);
    plugin_registry_free(&r);
    return 0;
}
static int tb(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    ASSERT_GE(plugin_load_dir(&r, "/tmp"), 0);
    plugin_registry_free(&r);
    return 0;
}
static void *nil_init(void) {
    return NULL;
}
static int tc(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    plugin_register(&r, "nil", nil_init, NULL, NULL);
    ASSERT_INT_EQ(plugin_load(&r, "nil"), 0);
    ASSERT_INT_EQ(plugin_loaded_count(&r), 1);
    plugin_registry_free(&r);
    return 0;
}

static int td1(void) {
    char tmpl[] = "/tmp/nevercode_test_loadable_XXXXXX";
    int fd = mkstemp(tmpl);
    ASSERT(fd >= 0);
    close(fd);
    ASSERT_INT_EQ(plugin_file_loadable(tmpl), 0);
    unlink(tmpl);
    return 0;
}
static int td2(void) {
    char tmpl[] = "/tmp/nevercode_test_writable_XXXXXX";
    int fd = mkstemp(tmpl);
    ASSERT(fd >= 0);
    ASSERT(fchmod(fd, 0664) == 0); /* group-writable */
    close(fd);
    ASSERT_INT_EQ(plugin_file_loadable(tmpl), -1);
    unlink(tmpl);
    return 0;
}
static int td3(void) {
    char tmpl[] = "/tmp/nevercode_test_fifo_XXXXXX";
    char *dir = mkdtemp(tmpl);
    ASSERT(dir != NULL);
    char fifo[NC_PATH_MAX];
    snprintf(fifo, sizeof(fifo), "%s/p.so", dir);
    ASSERT(mkfifo(fifo, 0644) == 0);
    ASSERT_INT_EQ(plugin_file_loadable(fifo), -1);
    unlink(fifo);
    rmdir(dir);
    return 0;
}
static int td4(void) {
    ASSERT_INT_EQ(plugin_file_loadable("/tmp/nevercode-nonexistent-plugin.so"), -1);
    ASSERT_INT_EQ(plugin_file_loadable(NULL), -1);
    ASSERT_INT_EQ(plugin_file_loadable(""), -1);
    return 0;
}
static int td5(void) {
    const char *old = getenv("NEVERCODE_PROJECT_PLUGINS");
    char *backup = old ? nc_xstrdup(old) : NULL;
    unsetenv("NEVERCODE_PROJECT_PLUGINS");
    ASSERT(!plugin_project_plugins_allowed());
    setenv("NEVERCODE_PROJECT_PLUGINS", "1", 1);
    ASSERT(plugin_project_plugins_allowed());
    setenv("NEVERCODE_PROJECT_PLUGINS", "0", 1);
    ASSERT(!plugin_project_plugins_allowed());
    if (backup) {
        setenv("NEVERCODE_PROJECT_PLUGINS", backup, 1);
        free(backup);
    } else {
        unsetenv("NEVERCODE_PROJECT_PLUGINS");
    }
    return 0;
}
static int td6(void) {
    char tmpl[] = "/tmp/nevercode_test_wwdir_XXXXXX";
    char *dir = mkdtemp(tmpl);
    ASSERT(dir != NULL);
    ASSERT(chmod(dir, 0777) == 0); /* world-writable, non-sticky */
    char sobuf[NC_PATH_MAX];
    snprintf(sobuf, sizeof(sobuf), "%s/test.so", dir);
    int fd = open(sobuf, O_CREAT | O_WRONLY, 0644);
    ASSERT(fd >= 0);
    close(fd);
    ASSERT_INT_EQ(plugin_file_loadable(sobuf), -1); /* parent world-writable: refuse */
    unlink(sobuf);
    ASSERT(chmod(dir, 0700) == 0);
    ASSERT_INT_EQ(plugin_file_loadable(sobuf), -1); /* file removed: refuse */
    rmdir(dir);
    return 0;
}
int test_plugin_all(void) {
    TEST_SUITE("plugin");
    TEST_COUNTERS;
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    RUN_TEST(ta);
    RUN_TEST(tb);
    RUN_TEST(tc);
    RUN_TEST(td1);
    RUN_TEST(td2);
    RUN_TEST(td3);
    RUN_TEST(td4);
    RUN_TEST(td5);
    RUN_TEST(td6);
    TEST_RETURN_FAILED();
}
