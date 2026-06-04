#include "test.h"
#include "nevercode.h"
#include "../include/nevercode.h"

static int test_suggestion_init(void) {
    ctx_suggestion_t s;
    ctx_suggestion_init(&s);
    ASSERT_NULL(s.title);
    ASSERT_NULL(s.detail);
    ASSERT_INT_EQ(s.severity, 0);
    ASSERT_INT_EQ(s.savings_tokens, 0);
    return 0;
}

static int test_no_suggestions_empty(void) {
    ctx_analysis_t a;
    memset(&a, 0, sizeof(a));
    a.context_limit = 200000;
    a.usage_pct = 10;
    a.autocompact_enabled = true;

    ctx_suggestion_t out[8];
    int n = ctx_suggest_analyze(&a, out, 8);
    ASSERT_INT_EQ(n, 0);
    return 0;
}

static int test_near_capacity_warning(void) {
    ctx_analysis_t a;
    memset(&a, 0, sizeof(a));
    a.context_limit = 200000;
    a.total_tokens = 170000;
    a.usage_pct = 85;
    a.autocompact_enabled = true;

    ctx_suggestion_t out[8];
    int n = ctx_suggest_analyze(&a, out, 8);
    ASSERT(n >= 1);
    ASSERT_INT_EQ(out[0].severity, CTX_SEV_WARNING);
    ASSERT(strstr(out[0].title, "85%") != NULL);
    ASSERT(strstr(out[0].detail, "Autocompact") != NULL);
    ctx_suggestions_free(out, n);
    return 0;
}

static int test_near_capacity_no_autocompact(void) {
    ctx_analysis_t a;
    memset(&a, 0, sizeof(a));
    a.context_limit = 200000;
    a.total_tokens = 180000;
    a.usage_pct = 90;
    a.autocompact_enabled = false;

    ctx_suggestion_t out[8];
    int n = ctx_suggest_analyze(&a, out, 8);
    ASSERT(n >= 1);
    ASSERT(strstr(out[0].detail, "disabled") != NULL);
    ctx_suggestions_free(out, n);
    return 0;
}

static int test_large_bash_result(void) {
    ctx_analysis_t a;
    memset(&a, 0, sizeof(a));
    a.context_limit = 100000;
    a.usage_pct = 50;
    a.autocompact_enabled = true;
    a.num_tool_stats = 1;
    nc_strlcpy(a.tool_stats[0].name, "Bash", sizeof(a.tool_stats[0].name));
    a.tool_stats[0].call_tokens = 100;
    a.tool_stats[0].result_tokens = 20000;

    ctx_suggestion_t out[8];
    int n = ctx_suggest_analyze(&a, out, 8);
    ASSERT(n >= 1);
    bool found_bash = false;
    for (int i = 0; i < n; i++) {
        if (strstr(out[i].title, "Bash") != NULL) {
            found_bash = true;
            ASSERT_INT_EQ(out[i].severity, CTX_SEV_WARNING);
            ASSERT(out[i].savings_tokens > 0);
        }
    }
    ASSERT(found_bash);
    ctx_suggestions_free(out, n);
    return 0;
}

static int test_large_read_result(void) {
    ctx_analysis_t a;
    memset(&a, 0, sizeof(a));
    a.context_limit = 100000;
    a.usage_pct = 40;
    a.autocompact_enabled = true;
    a.num_tool_stats = 1;
    nc_strlcpy(a.tool_stats[0].name, "Read", sizeof(a.tool_stats[0].name));
    a.tool_stats[0].call_tokens = 100;
    a.tool_stats[0].result_tokens = 18000;

    ctx_suggestion_t out[8];
    int n = ctx_suggest_analyze(&a, out, 8);
    ASSERT(n >= 1);
    ctx_suggestions_free(out, n);
    return 0;
}

static int test_memory_bloat(void) {
    ctx_analysis_t a;
    memset(&a, 0, sizeof(a));
    a.context_limit = 100000;
    a.memory_tokens = 8000;
    a.usage_pct = 40;
    a.autocompact_enabled = true;

    ctx_suggestion_t out[8];
    int n = ctx_suggest_analyze(&a, out, 8);
    ASSERT(n >= 1);
    bool found = false;
    for (int i = 0; i < n; i++) {
        if (strstr(out[i].title, "Memory") != NULL)
            found = true;
    }
    ASSERT(found);
    ctx_suggestions_free(out, n);
    return 0;
}

static int test_autocompact_disabled_mid(void) {
    ctx_analysis_t a;
    memset(&a, 0, sizeof(a));
    a.context_limit = 200000;
    a.usage_pct = 60;
    a.autocompact_enabled = false;

    ctx_suggestion_t out[8];
    int n = ctx_suggest_analyze(&a, out, 8);
    ASSERT(n >= 1);
    bool found = false;
    for (int i = 0; i < n; i++) {
        if (strstr(out[i].title, "disabled") != NULL)
            found = true;
    }
    ASSERT(found);
    ctx_suggestions_free(out, n);
    return 0;
}

static int test_format_null(void) {
    char *r = ctx_suggest_format(NULL, 0);
    ASSERT_NULL(r);
    return 0;
}

static int test_format_suggestions(void) {
    ctx_suggestion_t s[2];
    s[0].severity = CTX_SEV_WARNING;
    s[0].title = nc_xstrdup("Context is 85% full");
    s[0].detail = nc_xstrdup("Use /compact");
    s[0].savings_tokens = 0;
    s[1].severity = CTX_SEV_INFO;
    s[1].title = nc_xstrdup("Bash results large");
    s[1].detail = nc_xstrdup("Pipe through head");
    s[1].savings_tokens = 5000;

    char *fmt = ctx_suggest_format(s, 2);
    ASSERT_NOT_NULL(fmt);
    ASSERT(strstr(fmt, "85%") != NULL);
    ASSERT(strstr(fmt, "saveable") != NULL);
    free(fmt);
    ctx_suggestions_free(s, 2);
    return 0;
}

static int test_sort_warnings_first(void) {
    ctx_analysis_t a;
    memset(&a, 0, sizeof(a));
    a.context_limit = 100000;
    a.usage_pct = 85;
    a.autocompact_enabled = true;
    a.memory_tokens = 8000;

    ctx_suggestion_t out[8];
    int n = ctx_suggest_analyze(&a, out, 8);
    ASSERT(n >= 2);
    ASSERT_INT_EQ(out[0].severity, CTX_SEV_WARNING);
    ctx_suggestions_free(out, n);
    return 0;
}

int test_context_suggest_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("context_suggest");

    RUN_TEST(test_suggestion_init);
    RUN_TEST(test_no_suggestions_empty);
    RUN_TEST(test_near_capacity_warning);
    RUN_TEST(test_near_capacity_no_autocompact);
    RUN_TEST(test_large_bash_result);
    RUN_TEST(test_large_read_result);
    RUN_TEST(test_memory_bloat);
    RUN_TEST(test_autocompact_disabled_mid);
    RUN_TEST(test_format_null);
    RUN_TEST(test_format_suggestions);
    RUN_TEST(test_sort_warnings_first);

    TEST_RETURN_FAILED();
}
