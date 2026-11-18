/*
 * config_migrate.c — Configuration migration system
 *
 * Performs one-time migrations between config versions.
 * Uses marker files to ensure migrations only run once.
 * Supports migrating from legacy config formats and setting defaults
 * for users with existing sessions.
 */

#include "nevercode.h"

#define MIGRATE_MARKER_VERSION "v1\n"

typedef enum {
    MIGRATE_APPLIED,
    MIGRATE_SKIPPED_MARKER,
    MIGRATE_SKIPPED_NO_DATA,
    MIGRATE_SKIPPED_EXPLICIT,
    MIGRATE_ERROR
} migrate_status_t;

static int marker_exists(const char *home, const char *name) {
    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s%s", home, CONFIG_MIGRATE_MARKER_PREFIX, name);
    struct stat st;
    return stat(path, &st) == 0;
}

static int create_marker(const char *home, const char *name) {
    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s%s", home, CONFIG_MIGRATE_MARKER_PREFIX, name);
    FILE *f = fopen(path, "wx");
    if (!f) {
        if (errno == EEXIST)
            return 0;
        return -1;
    }
    int rc = (fputs(MIGRATE_MARKER_VERSION, f) == EOF || fflush(f)) ? -1 : 0;
    fclose(f);
    return rc;
}

static bool dir_has_sessions(const char *home) {
    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/sessions", home);
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* When hardlink fails (cross-device, EPERM, etc.), copy file contents. */
static int migrate_copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in)
        return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    char buf[NC_IO_BUF];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            unlink(dst);
            return -1;
        }
    }
    if (ferror(in)) {
        fclose(in);
        fclose(out);
        unlink(dst);
        return -1;
    }
    fclose(in);
    int rc = fflush(out) ? -1 : 0;
    fclose(out);
    if (rc) {
        unlink(dst);
        return -1;
    }
    return 0;
}

static migrate_status_t migrate_default_effort(const char *home) {
    const char *id = "default_effort";
    if (marker_exists(home, id))
        return MIGRATE_SKIPPED_MARKER;

    const char *env = getenv(ENV_EFFORT);
    if (env && env[0]) {
        create_marker(home, id);
        return MIGRATE_SKIPPED_EXPLICIT;
    }

    if (!dir_has_sessions(home)) {
        create_marker(home, id);
        return MIGRATE_SKIPPED_NO_DATA;
    }

    create_marker(home, id);
    return MIGRATE_APPLIED;
}

static migrate_status_t migrate_compat_dirs(const char *home) {
    const char *id = "compat_dirs";
    if (marker_exists(home, id))
        return MIGRATE_SKIPPED_MARKER;

    char src[NC_PATH_MAX], dst[NC_PATH_MAX];
    snprintf(src, sizeof(src), "%s%s", home, CONFIG_DIR_COMPAT);
    snprintf(dst, sizeof(dst), "%s%s", home, CONFIG_DIR_PRIMARY);

    struct stat st;
    if (stat(src, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (stat(dst, &st) != 0)
            mkdir(dst, NC_DIR_MODE);

        const char *files[] = {NC_CFG_CONFIG, NC_DOC_AGENTS_BASE_UPPER, NC_CFG_SETTINGS, NULL};
        for (int i = 0; files[i]; i++) {
            char sf[NC_PATH_MAX], df[NC_PATH_MAX];
            snprintf(sf, sizeof(sf), "%.2045s/%.2045s", src, files[i]);
            snprintf(df, sizeof(df), "%.2045s/%.2045s", dst, files[i]);
            struct stat fs;
            if (stat(sf, &fs) == 0 && stat(df, &fs) != 0) {
                if (link(sf, df) != 0)
                    (void)migrate_copy_file(sf, df);
            }
        }
    }

    create_marker(home, id);
    return MIGRATE_APPLIED;
}

typedef struct {
    const char *name;
    migrate_status_t (*fn)(const char *home);
} migration_entry_t;

static const migration_entry_t g_migrations[] = {
    {"default_effort", migrate_default_effort}, {"compat_dirs", migrate_compat_dirs}, {NULL, NULL}};

int config_migrate_run(const char *home) {
    if (!home || !home[0])
        return -1;

    int applied = 0;
    for (int i = 0; g_migrations[i].name; i++) {
        migrate_status_t st = g_migrations[i].fn(home);
        if (st == MIGRATE_APPLIED)
            applied++;
    }
    return applied;
}

const char *config_migrate_status_name(migrate_status_t s) {
    switch (s) {
    case MIGRATE_APPLIED:
        return "applied";
    case MIGRATE_SKIPPED_MARKER:
        return "skipped_marker";
    case MIGRATE_SKIPPED_NO_DATA:
        return "skipped_no_data";
    case MIGRATE_SKIPPED_EXPLICIT:
        return "skipped_explicit";
    case MIGRATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}
