#include "test.h"
#include "nevercode.h"
#include "../include/nevercode.h"
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

static int test_fw_pg_combined_init(void) {
    file_watcher_t *fw = file_watcher_global();
    ASSERT_NOT_NULL(fw);
    ASSERT_INT_EQ(fw->num_watches, 0);

    procgroup_ctx_t *pg = procgroup_global();
    ASSERT_NOT_NULL(pg);
    ASSERT_INT_EQ(procgroup_count(pg), 0);
    return 0;
}

static int test_pg_bash_flow(void) {
    procgroup_ctx_t *pg = procgroup_global();

    pid_t child = fork();
    if (child == 0) {
        usleep(50000);
        _exit(0);
    }
    ASSERT(child > 0);

    int idx = procgroup_register(pg, child, "echo hello", PROCGROUP_TOOL);
    ASSERT(idx >= 0);
    ASSERT(procgroup_has_pid(pg, child));
    ASSERT_INT_EQ(procgroup_count_by_type(pg, PROCGROUP_TOOL), 1);

    usleep(200000);
    procgroup_reap(pg);
    ASSERT(pg->entries[0].exited);
    ASSERT_INT_EQ(pg->entries[0].exit_code, 0);

    procgroup_unregister(pg, child);
    return 0;
}

static int test_diag_gcc_output(void) {
    const char *output = "main.c:10:5: error: implicit declaration of function 'foo'\n"
                         "main.c:20:3: warning: unused variable 'x'\n";

    diagnostic_entry_t diags[16];
    int nd = diagnostic_parse_output(output, diags, 16);
    ASSERT_INT_EQ(nd, 2);

    int ann = diagnostic_annotate_suggestions(diags, nd);
    ASSERT(ann >= 1);

    ASSERT_NOT_NULL(diags[0].suggestion);
    ASSERT(strstr(diags[0].suggestion, "include") != NULL);
    ASSERT_NOT_NULL(diags[1].suggestion);

    char *fmt = diagnostic_format(diags, nd);
    ASSERT_NOT_NULL(fmt);
    ASSERT(strstr(fmt, "error") != NULL);
    ASSERT(strstr(fmt, "Hint") != NULL);
    free(fmt);

    diagnostics_free(diags, nd);
    return 0;
}

static int test_diag_python_tsc_mixed(void) {
    const char *output = "  File \"app.py\", line 42, in main\n"
                         "src/app.ts(10,5): error TS2304: Cannot find name 'foo'\n";

    diagnostic_entry_t diags[16];
    int nd = diagnostic_parse_output(output, diags, 16);
    ASSERT(nd >= 2);

    ASSERT_STR_EQ(diags[0].file, "app.py");
    ASSERT_INT_EQ(diags[0].line, 42);

    ASSERT_STR_EQ(diags[1].file, "src/app.ts");
    ASSERT_INT_EQ(diags[1].line, 10);
    ASSERT(strstr(diags[1].message, "Cannot find name") != NULL);

    diagnostics_free(diags, nd);
    return 0;
}

static int test_fw_config_paths(void) {
    file_watcher_t w;
    file_watcher_init(&w);
    int added = file_watcher_add_config_paths(&w, "/tmp");
    ASSERT(added >= 0);
    file_watcher_free(&w);
    return 0;
}

static int test_pg_multiple_types(void) {
    procgroup_ctx_t pg;
    procgroup_init(&pg);

    procgroup_register(&pg, 10001, "bash cmd", PROCGROUP_TOOL);
    procgroup_register(&pg, 10002, "sub-agent", PROCGROUP_AGENT);
    procgroup_register(&pg, 10003, "python repl", PROCGROUP_REPL);
    procgroup_register(&pg, 10004, "mcp server", PROCGROUP_MCP);

    ASSERT_INT_EQ(procgroup_count(&pg), 4);
    ASSERT_INT_EQ(procgroup_count_by_type(&pg, PROCGROUP_TOOL), 1);
    ASSERT_INT_EQ(procgroup_count_by_type(&pg, PROCGROUP_AGENT), 1);
    ASSERT_INT_EQ(procgroup_count_by_type(&pg, PROCGROUP_REPL), 1);
    ASSERT_INT_EQ(procgroup_count_by_type(&pg, PROCGROUP_MCP), 1);

    char *fmt = procgroup_format(&pg);
    ASSERT_NOT_NULL(fmt);
    ASSERT(strstr(fmt, "bash cmd") != NULL);
    ASSERT(strstr(fmt, "sub-agent") != NULL);
    ASSERT(strstr(fmt, "repl") != NULL);
    ASSERT(strstr(fmt, "mcp") != NULL);
    free(fmt);

    procgroup_free(&pg);
    return 0;
}

static int test_diag_summary_format(void) {
    diagnostic_entry_t diags[3];
    diagnostic_init(&diags[0]);
    diags[0].severity = DIAG_ERROR;
    diags[0].message = nc_xstrdup("test error 1");
    diags[0].file = nc_xstrdup("a.c");
    diags[0].line = 1;

    diagnostic_init(&diags[1]);
    diags[1].severity = DIAG_WARNING;
    diags[1].message = nc_xstrdup("test warning");
    diags[1].file = nc_xstrdup("b.c");
    diags[1].line = 2;

    diagnostic_init(&diags[2]);
    diags[2].severity = DIAG_ERROR;
    diags[2].message = nc_xstrdup("test error 2");
    diags[2].file = nc_xstrdup("c.c");
    diags[2].line = 3;

    ASSERT_INT_EQ(diagnostic_count_errors(diags, 3), 2);
    ASSERT_INT_EQ(diagnostic_count_warnings(diags, 3), 1);

    char *summary = diagnostic_summary(diags, 3);
    ASSERT_NOT_NULL(summary);
    ASSERT(strstr(summary, "2 error") != NULL);
    ASSERT(strstr(summary, "1 warning") != NULL);
    free(summary);

    diagnostics_free(diags, 3);
    return 0;
}

static int test_fw_poll_no_change(void) {
    char tmpf[] = "/tmp/nevercode_v5_fw_XXXXXX";
    int fd = mkstemp(tmpf);
    ASSERT(fd >= 0);
    write(fd, "test", 4);
    close(fd);

    file_watcher_t w;
    file_watcher_init(&w);
    file_watcher_add(&w, tmpf, WATCH_FILE);
    int changed = file_watcher_check_poll(&w);
    ASSERT_INT_EQ(changed, 0);
    file_watcher_free(&w);
    unlink(tmpf);
    return 0;
}

int test_integration_v5_all(void) {
    TEST_SUITE("integration_v5");
    TEST_COUNTERS;

    RUN_TEST(test_fw_pg_combined_init);
    RUN_TEST(test_pg_bash_flow);
    RUN_TEST(test_diag_gcc_output);
    RUN_TEST(test_diag_python_tsc_mixed);
    RUN_TEST(test_fw_config_paths);
    RUN_TEST(test_pg_multiple_types);
    RUN_TEST(test_diag_summary_format);
    RUN_TEST(test_fw_poll_no_change);

    TEST_RETURN_FAILED();
}
