#include "test.h"
#include "nevercode.h"
#include "../include/nevercode.h"
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

static int test_init_free(void) {
    procgroup_ctx_t pg;
    procgroup_init(&pg);
    ASSERT_INT_EQ(procgroup_count(&pg), 0);
    ASSERT_INT_EQ(procgroup_active_count(&pg), 0);
    procgroup_free(&pg);
    return 0;
}

static int test_register_invalid(void) {
    procgroup_ctx_t pg;
    procgroup_init(&pg);
    ASSERT(procgroup_register(&pg, 0, "test", PROCGROUP_TOOL) < 0);
    ASSERT(procgroup_register(&pg, -1, "test", PROCGROUP_TOOL) < 0);
    ASSERT_INT_EQ(procgroup_count(&pg), 0);
    procgroup_free(&pg);
    return 0;
}

static int test_register_and_count(void) {
    procgroup_ctx_t pg;
    procgroup_init(&pg);

    pid_t fake_pid = 99999;
    int idx = procgroup_register(&pg, fake_pid, "bash_cmd", PROCGROUP_TOOL);
    ASSERT(idx >= 0);
    ASSERT_INT_EQ(procgroup_count(&pg), 1);
    ASSERT_INT_EQ(procgroup_active_count(&pg), 1);
    ASSERT(procgroup_has_pid(&pg, fake_pid));
    ASSERT(!procgroup_has_pid(&pg, 12345));

    procgroup_free(&pg);
    return 0;
}

static int test_register_duplicate(void) {
    procgroup_ctx_t pg;
    procgroup_init(&pg);

    int i1 = procgroup_register(&pg, 12345, "test", PROCGROUP_TOOL);
    int i2 = procgroup_register(&pg, 12345, "test", PROCGROUP_TOOL);
    ASSERT(i1 >= 0);
    ASSERT_INT_EQ(i1, i2);
    ASSERT_INT_EQ(procgroup_count(&pg), 1);

    procgroup_free(&pg);
    return 0;
}

static int test_unregister(void) {
    procgroup_ctx_t pg;
    procgroup_init(&pg);

    procgroup_register(&pg, 11111, "a", PROCGROUP_TOOL);
    procgroup_register(&pg, 22222, "b", PROCGROUP_AGENT);
    ASSERT_INT_EQ(procgroup_count(&pg), 2);

    ASSERT_INT_EQ(procgroup_unregister(&pg, 11111), 0);
    ASSERT_INT_EQ(procgroup_count(&pg), 1);
    ASSERT(!procgroup_has_pid(&pg, 11111));

    ASSERT(procgroup_unregister(&pg, 99999) < 0);

    procgroup_free(&pg);
    return 0;
}

static int test_count_by_type(void) {
    procgroup_ctx_t pg;
    procgroup_init(&pg);

    procgroup_register(&pg, 111, "a", PROCGROUP_TOOL);
    procgroup_register(&pg, 222, "b", PROCGROUP_TOOL);
    procgroup_register(&pg, 333, "c", PROCGROUP_AGENT);
    procgroup_register(&pg, 444, "d", PROCGROUP_REPL);

    ASSERT_INT_EQ(procgroup_count_by_type(&pg, PROCGROUP_TOOL), 2);
    ASSERT_INT_EQ(procgroup_count_by_type(&pg, PROCGROUP_AGENT), 1);
    ASSERT_INT_EQ(procgroup_count_by_type(&pg, PROCGROUP_REPL), 1);
    ASSERT_INT_EQ(procgroup_count_by_type(&pg, PROCGROUP_MCP), 0);

    procgroup_free(&pg);
    return 0;
}

static int test_real_process_reap(void) {
    procgroup_ctx_t pg;
    procgroup_init(&pg);

    pid_t child = fork();
    if (child == 0) {
        _exit(42);
    }
    ASSERT(child > 0);

    procgroup_register(&pg, child, "child_exit_42", PROCGROUP_TOOL);
    usleep(200000);
    int reaped = procgroup_reap(&pg);
    ASSERT(reaped >= 1);

    ASSERT(pg.entries[0].exited);
    ASSERT_INT_EQ(pg.entries[0].exit_code, 42);

    procgroup_free(&pg);
    return 0;
}

static int test_cleanup_exited(void) {
    procgroup_ctx_t pg;
    procgroup_init(&pg);

    pid_t child = fork();
    if (child == 0)
        _exit(0);
    ASSERT(child > 0);

    procgroup_register(&pg, child, "temp", PROCGROUP_TOOL);
    usleep(200000);
    procgroup_reap(&pg);
    int removed = procgroup_cleanup_exited(&pg);
    ASSERT(removed >= 1);
    ASSERT_INT_EQ(procgroup_count(&pg), 0);

    procgroup_free(&pg);
    return 0;
}

static int test_format_empty(void) {
    procgroup_ctx_t pg;
    procgroup_init(&pg);
    char *s = procgroup_format(&pg);
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "No tracked") != NULL);
    free(s);
    procgroup_free(&pg);
    return 0;
}

static int test_format_with_entries(void) {
    procgroup_ctx_t pg;
    procgroup_init(&pg);
    procgroup_register(&pg, 99998, "test_proc", PROCGROUP_AGENT);
    char *s = procgroup_format(&pg);
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "test_proc") != NULL);
    ASSERT(strstr(s, "agent") != NULL);
    free(s);
    procgroup_free(&pg);
    return 0;
}

static int test_elapsed(void) {
    procgroup_ctx_t pg;
    procgroup_init(&pg);
    procgroup_register(&pg, 88888, "timer", PROCGROUP_TOOL);
    long e = procgroup_elapsed_s(&pg, 88888);
    ASSERT(e >= 0);
    ASSERT(procgroup_elapsed_s(&pg, 77777) < 0);
    procgroup_free(&pg);
    return 0;
}

static int test_global_singleton(void) {
    procgroup_ctx_t *g = procgroup_global();
    ASSERT_NOT_NULL(g);
    procgroup_ctx_t *g2 = procgroup_global();
    ASSERT(g == g2);
    return 0;
}

int test_procgroup_v2_all(void) {
    TEST_SUITE("procgroup_v2");
    TEST_COUNTERS;

    RUN_TEST(test_init_free);
    RUN_TEST(test_register_invalid);
    RUN_TEST(test_register_and_count);
    RUN_TEST(test_register_duplicate);
    RUN_TEST(test_unregister);
    RUN_TEST(test_count_by_type);
    RUN_TEST(test_real_process_reap);
    RUN_TEST(test_cleanup_exited);
    RUN_TEST(test_format_empty);
    RUN_TEST(test_format_with_entries);
    RUN_TEST(test_elapsed);
    RUN_TEST(test_global_singleton);

    TEST_RETURN_FAILED();
}
