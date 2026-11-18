#include "test.h"
#include "nevercode.h"
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <unistd.h>

static int has_c_or_h_suffix(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot)
        return 0;
    return strcmp(dot, ".c") == 0 || strcmp(dot, ".h") == 0;
}

static int file_contains_any_keyword(const char *path, const char **keywords, int n) {
    char *buf = strbuf_read_file(path, NULL);
    if (!buf)
        return 0;
    for (int i = 0; i < n; i++) {
        if (strstr(buf, keywords[i])) {
            free(buf);
            return 1;
        }
    }
    free(buf);
    return 0;
}

static int scan_dir_no_telemetry(const char *dir, const char **keywords, int n, int *scanned) {
    DIR *d = opendir(dir);
    if (!d)
        return 0;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char path[NC_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (scan_dir_no_telemetry(path, keywords, n, scanned) != 0) {
                closedir(d);
                return -1;
            }
            continue;
        }
        if (!S_ISREG(st.st_mode) || !has_c_or_h_suffix(path))
            continue;

        (*scanned)++;
        if (file_contains_any_keyword(path, keywords, n)) {
            fprintf(stderr, "Found forbidden telemetry keyword in %s\n", path);
            closedir(d);
            return -1;
        }
    }

    closedir(d);
    return 0;
}

static int test_no_telemetry_or_spy_keywords_in_source(void) {
    static const char *keywords[] = {"sentry",     "datadog",     "mixpanel", "posthog",
                                     "telemetry",  "analytics",   "statsig",  "amplitude",
                                     "growthbook", "launchdarkly"};

    /* Resolve src/ relative to the executable's directory, not CWD
     * (earlier tests may chdir). */
    char exe_path[NC_PATH_MAX];
#ifdef __APPLE__
    uint32_t sz = sizeof(exe_path);
#endif
    char src_dir[NC_PATH_MAX];
#ifdef __APPLE__
    if (_NSGetExecutablePath(exe_path, &sz) == 0) {
        char *slash = strrchr(exe_path, '/');
        if (slash)
            *slash = '\0';
        snprintf(src_dir, sizeof(src_dir), "%s/src", exe_path);
    } else {
#endif
        /* Fallback (and Linux path): resolve relative to CWD, then executable dir */
        if (access("src", R_OK) == 0) {
            snprintf(src_dir, sizeof(src_dir), "src");
        } else if (realpath("/proc/self/exe", exe_path)) {
            char *slash = strrchr(exe_path, '/');
            if (slash)
                *slash = '\0';
            snprintf(src_dir, sizeof(src_dir), "%.4089s/src", exe_path);
        } else {
            snprintf(src_dir, sizeof(src_dir), "src");
        }
#ifdef __APPLE__
    }
#endif

    int scanned = 0;
    int rc = scan_dir_no_telemetry(src_dir, keywords, NC_ARRAY_LEN(keywords), &scanned);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(scanned > 0);
    return 0;
}

int test_cleanroom_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("cleanroom");
    RUN_TEST(test_no_telemetry_or_spy_keywords_in_source);
    TEST_RETURN_FAILED();
}
