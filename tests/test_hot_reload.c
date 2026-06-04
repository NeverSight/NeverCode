#include "test.h"
#include "nevercode.h"
#include "../include/nevercode.h"
#include <string.h>

static int test_flag_set_consume(void) {
    hot_reload_flags_consume();
    ASSERT(!hot_reload_pending());

    hot_reload_flag_set(HOT_RELOAD_AGENTS_MD);
    ASSERT(hot_reload_pending());

    int flags = hot_reload_flags_consume();
    ASSERT(flags & (1 << HOT_RELOAD_AGENTS_MD));
    ASSERT(!hot_reload_pending());
    return 0;
}

static int test_multiple_flags(void) {
    hot_reload_flags_consume();
    hot_reload_flag_set(HOT_RELOAD_HOOKS);
    hot_reload_flag_set(HOT_RELOAD_MEMORY);
    hot_reload_flag_set(HOT_RELOAD_SKILLS);

    int flags = hot_reload_flags_consume();
    ASSERT(flags & (1 << HOT_RELOAD_HOOKS));
    ASSERT(flags & (1 << HOT_RELOAD_MEMORY));
    ASSERT(flags & (1 << HOT_RELOAD_SKILLS));
    ASSERT(!(flags & (1 << HOT_RELOAD_MCP)));
    return 0;
}

static int test_consume_clears(void) {
    hot_reload_flags_consume();
    hot_reload_flag_set(HOT_RELOAD_RULES);
    int f1 = hot_reload_flags_consume();
    ASSERT(f1 != 0);
    int f2 = hot_reload_flags_consume();
    ASSERT_INT_EQ(f2, 0);
    return 0;
}

static int test_kind_names(void) {
    ASSERT(strstr(hot_reload_kind_name(HOT_RELOAD_AGENTS_MD), "project") != NULL ||
           strstr(hot_reload_kind_name(HOT_RELOAD_AGENTS_MD), "docs") != NULL);
    ASSERT(strstr(hot_reload_kind_name(HOT_RELOAD_HOOKS), "hooks") != NULL);
    ASSERT(strstr(hot_reload_kind_name(HOT_RELOAD_MEMORY), "memory") != NULL);
    ASSERT(strstr(hot_reload_kind_name(HOT_RELOAD_SKILLS), "skills") != NULL);
    ASSERT(strstr(hot_reload_kind_name(HOT_RELOAD_RULES), "rules") != NULL);
    ASSERT(strstr(hot_reload_kind_name(HOT_RELOAD_MCP), "mcp") != NULL);
    return 0;
}

static int test_watcher_cb_agents(void) {
    hot_reload_flags_consume();
    hot_reload_watcher_cb("/project/AGENTS.md", WATCH_FILE, NULL);
    int flags = hot_reload_flags_consume();
    ASSERT(flags & (1 << HOT_RELOAD_AGENTS_MD));
    return 0;
}

static int test_watcher_cb_compat_doc(void) {
    hot_reload_flags_consume();
    hot_reload_watcher_cb("/project/CLAUDE.md", WATCH_FILE, NULL);
    int flags = hot_reload_flags_consume();
    ASSERT(flags & (1 << HOT_RELOAD_AGENTS_MD));
    return 0;
}

static int test_watcher_cb_lowercase_agents(void) {
    hot_reload_flags_consume();
    hot_reload_watcher_cb("/project/agents.md", WATCH_FILE, NULL);
    int flags = hot_reload_flags_consume();
    ASSERT(flags & (1 << HOT_RELOAD_AGENTS_MD));
    return 0;
}

static int test_watcher_cb_lowercase_claude(void) {
    hot_reload_flags_consume();
    hot_reload_watcher_cb("/project/claude.md", WATCH_FILE, NULL);
    int flags = hot_reload_flags_consume();
    ASSERT(flags & (1 << HOT_RELOAD_AGENTS_MD));
    return 0;
}

static int test_watcher_cb_override_variants(void) {
    hot_reload_flags_consume();
    hot_reload_watcher_cb("/project/AGENTS.override.md", WATCH_FILE, NULL);
    int f1 = hot_reload_flags_consume();
    ASSERT(f1 & (1 << HOT_RELOAD_AGENTS_MD));

    hot_reload_flags_consume();
    hot_reload_watcher_cb("/project/CLAUDE.override.md", WATCH_FILE, NULL);
    int f2 = hot_reload_flags_consume();
    ASSERT(f2 & (1 << HOT_RELOAD_AGENTS_MD));

    hot_reload_flags_consume();
    hot_reload_watcher_cb("/project/AGENTS.local.md", WATCH_FILE, NULL);
    int f3 = hot_reload_flags_consume();
    ASSERT(f3 & (1 << HOT_RELOAD_AGENTS_MD));
    return 0;
}

static int test_watcher_cb_dotdir_project_doc(void) {
    hot_reload_flags_consume();
    hot_reload_watcher_cb("/project/.agents/AGENTS.local.md", WATCH_FILE, NULL);
    int f1 = hot_reload_flags_consume();
    ASSERT(f1 & (1 << HOT_RELOAD_AGENTS_MD));

    hot_reload_flags_consume();
    hot_reload_watcher_cb("/project/.claude/claude.override.md", WATCH_FILE, NULL);
    int f2 = hot_reload_flags_consume();
    ASSERT(f2 & (1 << HOT_RELOAD_AGENTS_MD));
    return 0;
}

static int test_watcher_cb_hooks(void) {
    hot_reload_flags_consume();
    hot_reload_watcher_cb("/home/.agents/hooks.json", WATCH_FILE, NULL);
    int flags = hot_reload_flags_consume();
    ASSERT(flags & (1 << HOT_RELOAD_HOOKS));
    return 0;
}

static int test_watcher_cb_memory(void) {
    hot_reload_flags_consume();
    hot_reload_watcher_cb("/home/.claude/memory.json", WATCH_FILE, NULL);
    int flags = hot_reload_flags_consume();
    ASSERT(flags & (1 << HOT_RELOAD_MEMORY));
    return 0;
}

static int test_watcher_cb_skills(void) {
    hot_reload_flags_consume();
    hot_reload_watcher_cb("/project/.agents/skills", WATCH_DIR, NULL);
    int flags = hot_reload_flags_consume();
    ASSERT(flags & (1 << HOT_RELOAD_SKILLS));
    return 0;
}

static int test_watcher_cb_rules(void) {
    hot_reload_flags_consume();
    hot_reload_watcher_cb("/project/.claude/rules", WATCH_DIR, NULL);
    int flags = hot_reload_flags_consume();
    ASSERT(flags & (1 << HOT_RELOAD_RULES));
    return 0;
}

static int test_process_empty(void) {
    int n = hot_reload_process(0, NULL, NULL, NULL);
    ASSERT_INT_EQ(n, 0);
    return 0;
}

int test_hot_reload_all(void) {
    TEST_SUITE("hot_reload");
    TEST_COUNTERS;

    RUN_TEST(test_flag_set_consume);
    RUN_TEST(test_multiple_flags);
    RUN_TEST(test_consume_clears);
    RUN_TEST(test_kind_names);
    RUN_TEST(test_watcher_cb_agents);
    RUN_TEST(test_watcher_cb_compat_doc);
    RUN_TEST(test_watcher_cb_lowercase_agents);
    RUN_TEST(test_watcher_cb_lowercase_claude);
    RUN_TEST(test_watcher_cb_override_variants);
    RUN_TEST(test_watcher_cb_dotdir_project_doc);
    RUN_TEST(test_watcher_cb_hooks);
    RUN_TEST(test_watcher_cb_memory);
    RUN_TEST(test_watcher_cb_skills);
    RUN_TEST(test_watcher_cb_rules);
    RUN_TEST(test_process_empty);

    TEST_RETURN_FAILED();
}
