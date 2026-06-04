#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int t_deterministic(void) {
    agent_color_reset();
    const char *c1 = agent_color_for("explorer");
    const char *c2 = agent_color_for("explorer");
    ASSERT(c1 == c2);
    return 0;
}

static int t_different_types(void) {
    agent_color_reset();
    const char *c1 = agent_color_for("explorer");
    const char *c2 = agent_color_for("reviewer");
    ASSERT_NOT_NULL(c1);
    ASSERT_NOT_NULL(c2);
    return 0;
}

static int t_null_input(void) {
    const char *c = agent_color_for(NULL);
    ASSERT_NOT_NULL(c);
    c = agent_color_for("");
    ASSERT_NOT_NULL(c);
    return 0;
}

static int t_reset(void) {
    agent_color_reset();
    agent_color_for("a");
    agent_color_for("b");
    agent_color_reset();
    const char *c = agent_color_for("a");
    ASSERT_NOT_NULL(c);
    return 0;
}

static int t_ansi_prefix(void) {
    agent_color_reset();
    const char *c = agent_color_for("test-agent");
    ASSERT_NOT_NULL(c);
    ASSERT(c[0] == '\033' && c[1] == '[');
    return 0;
}

int test_agent_color_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("agent_color");
    RUN_TEST(t_deterministic);
    RUN_TEST(t_different_types);
    RUN_TEST(t_null_input);
    RUN_TEST(t_reset);
    RUN_TEST(t_ansi_prefix);
    TEST_RETURN_FAILED();
}
