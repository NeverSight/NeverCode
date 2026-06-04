#include "test.h"
#include "nevercode.h"

static int test_init_inactive(void) {
    coordinator_set_active(false);
    ASSERT(coordinator_active() == false);
    return 0;
}

static int test_activate(void) {
    coordinator_set_active(true);
    ASSERT(coordinator_active() == true);
    coordinator_set_active(false);
    return 0;
}

static int test_system_prompt(void) {
    const char *p = coordinator_system_prompt();
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "Coordinator") != NULL);
    ASSERT(strstr(p, "Worker") != NULL);
    return 0;
}

static int test_tool_allowed_inactive(void) {
    coordinator_set_active(false);
    ASSERT(coordinator_tool_allowed("Bash") == true);
    ASSERT(coordinator_tool_allowed("Read") == true);
    ASSERT(coordinator_tool_allowed("Agent") == true);
    return 0;
}

static int test_tool_allowed_active(void) {
    coordinator_set_active(true);
    ASSERT(coordinator_tool_allowed("Agent") == true);
    ASSERT(coordinator_tool_allowed("SendMessage") == true);
    ASSERT(coordinator_tool_allowed("TodoWrite") == true);
    ASSERT(coordinator_tool_allowed("Bash") == false);
    ASSERT(coordinator_tool_allowed("Read") == false);
    ASSERT(coordinator_tool_allowed("Write") == false);
    ASSERT(coordinator_tool_allowed("Edit") == false);
    coordinator_set_active(false);
    return 0;
}

static int test_add_tasks(void) {
    coordinator_reset_tasks();
    int t1 = coordinator_add_task("Implement feature A");
    int t2 = coordinator_add_task("Write tests for feature A");
    ASSERT_INT_EQ(t1, 0);
    ASSERT_INT_EQ(t2, 1);
    ASSERT_INT_EQ(coordinator_pending_count(), 2);
    ASSERT_INT_EQ(coordinator_completed_count(), 0);
    return 0;
}

static int test_assign_and_complete(void) {
    coordinator_reset_tasks();
    int t = coordinator_add_task("Fix bug #123");
    ASSERT_INT_EQ(coordinator_assign_task(t, "agent-1"), 0);
    ASSERT_INT_EQ(coordinator_pending_count(), 1);

    ASSERT_INT_EQ(coordinator_complete_task(t, "Bug fixed", true), 0);
    ASSERT_INT_EQ(coordinator_completed_count(), 1);
    ASSERT_INT_EQ(coordinator_pending_count(), 0);
    return 0;
}

static int test_find_by_agent(void) {
    coordinator_reset_tasks();
    coordinator_add_task("Task A");
    coordinator_add_task("Task B");
    coordinator_assign_task(0, "agent-x");
    coordinator_assign_task(1, "agent-y");

    ASSERT_INT_EQ(coordinator_find_task_by_agent("agent-x"), 0);
    ASSERT_INT_EQ(coordinator_find_task_by_agent("agent-y"), 1);
    ASSERT_INT_EQ(coordinator_find_task_by_agent("agent-z"), -1);
    return 0;
}

static int test_format_status(void) {
    coordinator_reset_tasks();
    coordinator_add_task("Build frontend");
    coordinator_add_task("Build backend");
    coordinator_assign_task(0, "agent-1");
    coordinator_complete_task(0, "Done: built React app", true);

    char *s = coordinator_format_status();
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "done") != NULL);
    ASSERT(strstr(s, "pending") != NULL);
    ASSERT(strstr(s, "1/2 completed") != NULL);
    free(s);
    return 0;
}

static int test_failed_task(void) {
    coordinator_reset_tasks();
    coordinator_add_task("Risky operation");
    coordinator_assign_task(0, "agent-1");
    coordinator_complete_task(0, "Compilation errors", false);

    char *s = coordinator_format_status();
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "failed") != NULL);
    free(s);
    return 0;
}

static int test_invalid_indices(void) {
    coordinator_reset_tasks();
    ASSERT_INT_EQ(coordinator_assign_task(-1, "a"), -1);
    ASSERT_INT_EQ(coordinator_assign_task(99, "a"), -1);
    ASSERT_INT_EQ(coordinator_complete_task(-1, "x", true), -1);
    ASSERT_INT_EQ(coordinator_complete_task(99, "x", true), -1);
    return 0;
}

int test_coordinator_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("coordinator");
    RUN_TEST(test_init_inactive);
    RUN_TEST(test_activate);
    RUN_TEST(test_system_prompt);
    RUN_TEST(test_tool_allowed_inactive);
    RUN_TEST(test_tool_allowed_active);
    RUN_TEST(test_add_tasks);
    RUN_TEST(test_assign_and_complete);
    RUN_TEST(test_find_by_agent);
    RUN_TEST(test_format_status);
    RUN_TEST(test_failed_task);
    RUN_TEST(test_invalid_indices);
    TEST_RETURN_FAILED();
}
