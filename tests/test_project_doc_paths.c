/*
 * test_project_doc_paths.c — Tests for canonical project instruction file path tables.
 */
#include "test.h"
#include "nevercode.h"
#include <string.h>

static int test_leaf_count_positive(void) {
    ASSERT_TRUE(nc_project_doc_leaf_n > 0);
    ASSERT_TRUE(nc_project_doc_leaf_n % 2 == 0);
    return 0;
}

static int test_agents_count_positive(void) {
    ASSERT_TRUE(nc_project_doc_agents_n > 0);
    ASSERT_TRUE(nc_project_doc_agents_n >= nc_project_doc_leaf_n);
    return 0;
}

static int test_extra_count_positive(void) {
    ASSERT_TRUE(nc_project_doc_extra_n > 0);
    ASSERT_INT_EQ(nc_project_doc_agents_n, nc_project_doc_extra_n);
    return 0;
}

static int test_leaf_names_non_null(void) {
    for (int i = 0; i < nc_project_doc_leaf_n; i++) {
        ASSERT_NOT_NULL(nc_project_doc_leaf_names[i]);
        ASSERT_TRUE(strlen(nc_project_doc_leaf_names[i]) > 0);
    }
    return 0;
}

static int test_agents_paths_non_null(void) {
    for (int i = 0; i < nc_project_doc_agents_n; i++) {
        ASSERT_NOT_NULL(nc_project_doc_agents_paths[i]);
        ASSERT_TRUE(strlen(nc_project_doc_agents_paths[i]) > 0);
    }
    return 0;
}

static int test_is_filename_agents(void) {
    ASSERT_TRUE(nc_project_doc_is_filename(NC_DOC_AGENTS_BASE_UPPER));
    ASSERT_TRUE(nc_project_doc_is_filename(NC_DOC_AGENTS_BASE_LOWER));
    ASSERT_TRUE(nc_project_doc_is_filename(NC_DOC_AGENTS_OVERRIDE_UPPER));
    ASSERT_TRUE(nc_project_doc_is_filename(NC_DOC_AGENTS_LOCAL_UPPER));
    return 0;
}

static int test_is_filename_compat(void) {
    ASSERT_TRUE(nc_project_doc_is_filename(NC_DOC_COMPAT_BASE_UPPER));
    ASSERT_TRUE(nc_project_doc_is_filename(NC_DOC_COMPAT_BASE_LOWER));
    ASSERT_TRUE(nc_project_doc_is_filename(NC_DOC_COMPAT_OVERRIDE_UPPER));
    ASSERT_TRUE(nc_project_doc_is_filename(NC_DOC_COMPAT_LOCAL_LOWER));
    return 0;
}

static int test_is_filename_rejects(void) {
    ASSERT_TRUE(!nc_project_doc_is_filename(NULL));
    ASSERT_TRUE(!nc_project_doc_is_filename(""));
    ASSERT_TRUE(!nc_project_doc_is_filename("README.md"));
    ASSERT_TRUE(!nc_project_doc_is_filename("AGENTS"));
    ASSERT_TRUE(!nc_project_doc_is_filename(".agents/AGENTS.md"));
    return 0;
}

static int test_dotdir_macros_in_paths(void) {
    bool found_primary = false, found_secondary = false, found_compat = false;
    for (int i = 0; i < nc_project_doc_agents_n; i++) {
        if (strstr(nc_project_doc_agents_paths[i], CONFIG_DOTDIR_PRIMARY))
            found_primary = true;
        if (strstr(nc_project_doc_agents_paths[i], CONFIG_DOTDIR_SECONDARY))
            found_secondary = true;
        if (strstr(nc_project_doc_agents_paths[i], CONFIG_DOTDIR_COMPAT))
            found_compat = true;
    }
    ASSERT_TRUE(found_primary);
    ASSERT_TRUE(found_secondary);
    ASSERT_TRUE(found_compat);
    return 0;
}

int test_project_doc_paths_all(void) {
    TEST_SUITE("project_doc_paths");
    TEST_COUNTERS;
    RUN_TEST(test_leaf_count_positive);
    RUN_TEST(test_agents_count_positive);
    RUN_TEST(test_extra_count_positive);
    RUN_TEST(test_leaf_names_non_null);
    RUN_TEST(test_agents_paths_non_null);
    RUN_TEST(test_is_filename_agents);
    RUN_TEST(test_is_filename_compat);
    RUN_TEST(test_is_filename_rejects);
    RUN_TEST(test_dotdir_macros_in_paths);
    TEST_RETURN_FAILED();
}
