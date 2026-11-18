/*
 * onboard.c — First-run detection and project onboarding
 *
 * Detects whether this is the first run (no config directory exists)
 * and performs initial setup: create config dirs, detect project type,
 * optionally import external tool configs, run config migrations.
 */

#include "nevercode.h"

/* OB_MARKER_FILENAME in defaults.h */
#define OB_MARKER_FILE OB_MARKER_FILENAME
/* OB_PROJECT_TYPES, OB_NAME_MAX in defaults.h */

typedef enum {
    OB_STATE_UNKNOWN,
    OB_STATE_FIRST_RUN,
    OB_STATE_ONBOARDED,
    OB_STATE_UPGRADED
} onboard_state_t;

typedef struct {
    char name[OB_NAME_MAX];
    bool detected;
} ob_project_type_t;

typedef struct {
    onboard_state_t state;
    ob_project_type_t projects[OB_PROJECT_TYPES];
    int project_count;
    bool has_agents_md;
    bool has_git;
    char config_home[NC_PATH_MAX];
    time_t onboard_time;
} ob_context_t;

static ob_context_t g_ob;

static bool dir_has_suffix(const char *dir, const char *suffix) {
    size_t slen = strlen(suffix);
    DIR *d = opendir(dir);
    if (!d)
        return false;
    struct dirent *de;
    bool found = false;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.')
            continue;
        size_t nl = strlen(de->d_name);
        if (nl >= slen && strcmp(de->d_name + nl - slen, suffix) == 0) {
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
}

static void detect_project_types(const char *project_root) {
    if (!project_root)
        return;

    static const struct {
        const char *file;
        const char *name;
    } indicators[] = {{"package.json", "nodejs"},
                      {"Cargo.toml", "rust"},
                      {"go.mod", "go"},
                      {"pyproject.toml", "python"},
                      {"requirements.txt", "python"},
                      {"Makefile", "make"},
                      {"CMakeLists.txt", "cmake"},
                      {"pom.xml", "java-maven"},
                      {"build.gradle", "java-gradle"},
                      {"Gemfile", "ruby"},
                      {"mix.exs", "elixir"},
                      {"composer.json", "php"},
                      {"*.csproj", "dotnet"},
                      {"Dockerfile", "docker"},
                      {NULL, NULL}};

    for (int i = 0; indicators[i].file && g_ob.project_count < OB_PROJECT_TYPES; i++) {
        bool found;
        if (indicators[i].file[0] == '*' && indicators[i].file[1] == '.') {
            found = dir_has_suffix(project_root, indicators[i].file + 1);
        } else {
            char path[NC_PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s", project_root, indicators[i].file);
            found = nc_path_exists(path);
        }
        if (found) {
            ob_project_type_t *p = &g_ob.projects[g_ob.project_count];
            nc_strlcpy(p->name, indicators[i].name, sizeof(p->name));
            p->detected = true;
            g_ob.project_count++;
        }
    }
}

onboard_state_t onboard_detect(const char *project_root) {
    memset(&g_ob, 0, sizeof(g_ob));

    const char *home = nc_home_dir();
    if (home)
        snprintf(g_ob.config_home, sizeof(g_ob.config_home), "%s%s", home, CONFIG_DIR_PRIMARY);

    /* check marker */
    char marker[NC_PATH_MAX];
    snprintf(marker, sizeof(marker), "%.2045s/%.2045s", g_ob.config_home, OB_MARKER_FILE);

    if (nc_path_exists(marker)) {
        g_ob.state = OB_STATE_ONBOARDED;
    } else if (g_ob.config_home[0] && nc_path_exists(g_ob.config_home)) {
        g_ob.state = OB_STATE_UPGRADED;
    } else {
        g_ob.state = OB_STATE_FIRST_RUN;
    }

    /* detect project */
    if (project_root) {
        detect_project_types(project_root);

        char agents_path[NC_PATH_MAX];
        snprintf(agents_path, sizeof(agents_path), "%s/AGENTS.md", project_root);
        g_ob.has_agents_md = nc_path_exists(agents_path);

        char git_path[NC_PATH_MAX];
        snprintf(git_path, sizeof(git_path), "%s/.git", project_root);
        g_ob.has_git = nc_path_exists(git_path);
    }

    return g_ob.state;
}

int onboard_setup(void) {
    if (g_ob.state != OB_STATE_FIRST_RUN)
        return 0;

    /* create config directories */
    if (g_ob.config_home[0])
        mkdir(g_ob.config_home, NC_DIR_MODE);

    char sessions[NC_PATH_MAX];
    snprintf(sessions, sizeof(sessions), "%.4084s/sessions", g_ob.config_home);
    mkdir(sessions, NC_DIR_MODE);

    char plugins[NC_PATH_MAX];
    snprintf(plugins, sizeof(plugins), "%.4085s/plugins", g_ob.config_home);
    mkdir(plugins, NC_DIR_MODE);

    /* create marker */
    char marker[NC_PATH_MAX];
    snprintf(marker, sizeof(marker), "%.2045s/%.2045s", g_ob.config_home, OB_MARKER_FILE);
    FILE *f = fopen(marker, "w");
    if (f) {
        fprintf(f, "%ld\n", nc_wall_sec());
        (void)fflush(f);
        fclose(f);
    }

    g_ob.state = OB_STATE_ONBOARDED;
    g_ob.onboard_time = nc_time_now();
    return 1;
}

bool onboard_is_first_run(void) {
    return g_ob.state == OB_STATE_FIRST_RUN;
}

int onboard_project_count(void) {
    return g_ob.project_count;
}

const char *onboard_project_name(int idx) {
    if (idx < 0 || idx >= g_ob.project_count)
        return NULL;
    return g_ob.projects[idx].name;
}

bool onboard_has_agents_md(void) {
    return g_ob.has_agents_md;
}

bool onboard_has_git(void) {
    return g_ob.has_git;
}

const char *onboard_state_name(onboard_state_t s) {
    switch (s) {
    case OB_STATE_UNKNOWN:
        return "unknown";
    case OB_STATE_FIRST_RUN:
        return "first_run";
    case OB_STATE_ONBOARDED:
        return "onboarded";
    case OB_STATE_UPGRADED:
        return "upgraded";
    default:
        return "unknown";
    }
}

char *onboard_format_status(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Onboard state: %s\n", onboard_state_name(g_ob.state));
    if (g_ob.project_count > 0) {
        strbuf_append_str(&sb, "Project types: ");
        for (int i = 0; i < g_ob.project_count; i++) {
            if (i > 0)
                strbuf_append_str(&sb, ", ");
            strbuf_append_str(&sb, g_ob.projects[i].name);
        }
        strbuf_append_str(&sb, "\n");
    }
    if (g_ob.has_agents_md)
        strbuf_append_str(&sb, "AGENTS.md: present\n");
    if (g_ob.has_git)
        strbuf_append_str(&sb, "Git: initialized\n");
    return strbuf_detach(&sb);
}
