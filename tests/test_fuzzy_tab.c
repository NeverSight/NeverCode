#include "test.h"
#include "nevercode.h"
#include "permission.h"
#include "terminal.h"
#include "config.h"

static int test_fuzzy_tab_prefix_still_works(void) {
    const char **cmds = term_get_slash_cmds();
    ASSERT_NOT_NULL(cmds);
    int found = 0;
    for (int i = 0; cmds[i]; i++)
        if (strncmp(cmds[i], "/com", 4) == 0)
            found++;
    ASSERT(found >= 2); /* /compact, /commit, /copy, /config */
    return 0;
}

static int test_fuzzy_score_practical(void) {
    int s1 = fuzzy_score("/compact", "/cmpct");
    int s2 = fuzzy_score("/version", "/cmpct");
    ASSERT(s1 < s2);
    return 0;
}

static int test_fuzzy_filter_commands(void) {
    const char **cmds = term_get_slash_cmds();
    int count = 0;
    while (cmds[count])
        count++;

    fuzzy_result_t results[16];
    int n = fuzzy_filter(cmds, count, "/vrfy", results, 16);
    ASSERT(n >= 1);
    int found_verify = 0;
    for (int i = 0; i < n; i++)
        if (strcmp(results[i].item, "/verify") == 0)
            found_verify = 1;
    ASSERT(found_verify);
    return 0;
}

static int test_approval_preset_config_init(void) {
    config_t cfg;
    config_init(&cfg);
    ASSERT_INT_EQ(cfg.approval_preset, 2); /* APPROVAL_FULL */
    config_free(&cfg);
    return 0;
}

static int test_approval_perm_ctx_init(void) {
    perm_ctx_t p;
    perm_init(&p);
    ASSERT_INT_EQ(p.approval_preset, APPROVAL_FULL);
    ASSERT_INT_EQ(p.trust_all, 1);
    perm_free(&p);
    return 0;
}

static int test_history_search_fuzzy_no_crash(void) {
    char *out = NULL;
    int n = history_search_fuzzy("nonexistent_query_xyz", &out);
    ASSERT(n >= 0);
    ASSERT_NOT_NULL(out);
    free(out);
    return 0;
}

static int test_fuzzy_match_slash_commands(void) {
    int score = 0;
    int rc = fuzzy_match("/compact", "/cpt", NULL, 0, &score);
    ASSERT(rc >= 3);
    return 0;
}

int test_fuzzy_tab_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("fuzzy_tab");

    RUN_TEST(test_fuzzy_tab_prefix_still_works);
    RUN_TEST(test_fuzzy_score_practical);
    RUN_TEST(test_fuzzy_filter_commands);
    RUN_TEST(test_approval_preset_config_init);
    RUN_TEST(test_approval_perm_ctx_init);
    RUN_TEST(test_history_search_fuzzy_no_crash);
    RUN_TEST(test_fuzzy_match_slash_commands);

    TEST_RETURN_FAILED();
}
