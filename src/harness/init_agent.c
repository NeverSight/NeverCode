/* init_agent.c — Initializer agent for first-run project scaffolding.
 *
 * Detects project type from indicator files (package.json, Cargo.toml, etc.),
 * generates a setup script, writes a progress tracker seed, and marks the
 * project as initialized so the agent doesn't re-scaffold on subsequent runs.
 */

#include "harness.h"
#include "nevercode.h"

/* INIT_AGENT_MARKER, INIT_AGENT_SCRIPT in defaults.h */
#define INIT_MARKER_FILE INIT_AGENT_MARKER
#define INIT_SCRIPT_NAME INIT_AGENT_SCRIPT

typedef struct {
    char name[NC_LABEL_MAX];
    char indicator[NC_PATH_MAX];
    char run_cmd[NC_CMD_MAX];
    char test_cmd[NC_CMD_MAX];
} project_recipe_t;

static const project_recipe_t s_recipes[] = {
    {"node", "package.json", "npm install && npm start", "npm test"},
    {"python", "requirements.txt", "pip install -r requirements.txt", "pytest"},
    {"python", "pyproject.toml", "pip install -e .", "pytest"},
    {"rust", "Cargo.toml", "cargo build", "cargo test"},
    {"go", "go.mod", "go build ./...", "go test ./..."},
    {"c", "Makefile", "make", "make test"},
    {"c", "CMakeLists.txt", "cmake -B build && cmake --build build", "ctest --test-dir build"},
    {"java", "pom.xml", "mvn compile", "mvn test"},
    {"java", "build.gradle", "gradle build", "gradle test"},
    {"ruby", "Gemfile", "bundle install", "bundle exec rspec"},
    {"php", "composer.json", "composer install", "vendor/bin/phpunit"},
    {"dotnet", "*.csproj", "dotnet build", "dotnet test"},
};

#define NUM_RECIPES NC_ARRAY_LEN(s_recipes)

/* Indicators like "*.csproj" mean "any file in root with that suffix". */
static bool root_has_indicator(const char *root, const char *indicator) {
    if (indicator[0] == '*' && indicator[1] == '.') {
        const char *ext = indicator + 1;
        size_t ext_len = strlen(ext);
        DIR *d = opendir(root);
        if (!d)
            return false;
        struct dirent *de;
        bool found = false;
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.')
                continue;
            size_t nl = strlen(de->d_name);
            if (nl >= ext_len && strcmp(de->d_name + nl - ext_len, ext) == 0) {
                found = true;
                break;
            }
        }
        closedir(d);
        return found;
    }

    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", root, indicator);
    return nc_path_exists(path);
}

bool init_agent_needed(const char *root) {
    if (!root || !root[0])
        return false;

    char marker[NC_PATH_MAX];
    snprintf(marker, sizeof(marker), "%s/" INIT_MARKER_FILE, root);
    if (nc_path_exists(marker))
        return false;

    char progress[NC_PATH_MAX];
    snprintf(progress, sizeof(progress), "%s/" PROGRESS_FILE, root);

    char flist[NC_PATH_MAX];
    snprintf(flist, sizeof(flist), "%s/" FLIST_FILE, root);

    if (nc_path_exists(progress) || nc_path_exists(flist))
        return false;

    return true;
}

int init_agent_detect_type(const char *root) {
    if (!root)
        return -1;

    for (int i = 0; i < NUM_RECIPES; i++) {
        if (root_has_indicator(root, s_recipes[i].indicator))
            return i;
    }
    return -1;
}

const char *init_agent_type_name(int idx) {
    if (idx < 0 || idx >= NUM_RECIPES)
        return "unknown";
    return s_recipes[idx].name;
}

const char *init_agent_run_cmd(int idx) {
    if (idx < 0 || idx >= NUM_RECIPES)
        return NULL;
    return s_recipes[idx].run_cmd;
}

const char *init_agent_test_cmd(int idx) {
    if (idx < 0 || idx >= NUM_RECIPES)
        return NULL;
    return s_recipes[idx].test_cmd;
}

int init_agent_gen_script(const char *root, int recipe_idx) {
    if (!root)
        return -1;

    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/" INIT_SCRIPT_NAME, root);
    if (nc_path_exists(path))
        return 0;

    FILE *f = fopen(path, "w");
    if (!f)
        return -1;

    fprintf(f, "#!/bin/sh\nset -e\necho \"[init] Setting up...\"\n");
    if (recipe_idx >= 0 && recipe_idx < NUM_RECIPES)
        fprintf(f, "%s\n%s || true\n", s_recipes[recipe_idx].run_cmd,
                s_recipes[recipe_idx].test_cmd);
    fprintf(f, "echo \"[init] Ready.\"\n");
    int rc = fflush(f) ? -1 : 0;
    fclose(f);
    chmod(path, NC_FILE_MODE_EXEC);
    return rc;
}

int init_agent_write_marker(const char *root) {
    if (!root)
        return -1;

    char dir[NC_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/.agents", root);
    if (!nc_dir_exists(dir))
        mkdir(dir, NC_DIR_MODE);

    char marker[NC_PATH_MAX];
    snprintf(marker, sizeof(marker), "%s/" INIT_MARKER_FILE, root);

    FILE *f = fopen(marker, "w");
    if (!f)
        return -1;

    fprintf(f, "%ld\n", nc_wall_sec());
    int rc = fflush(f) ? -1 : 0;
    fclose(f);
    return rc;
}

char *init_agent_build_context(const char *root, int recipe_idx) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "<initializer_context>\n"
                           "This is a new project environment. "
                           "The initializer agent has prepared:\n");

    char path[NC_PATH_MAX];

    snprintf(path, sizeof(path), "%s/" PROGRESS_FILE, root);
    if (nc_path_exists(path))
        strbuf_append_str(&sb, "- Progress tracker\n");

    snprintf(path, sizeof(path), "%s/" FLIST_FILE, root);
    if (nc_path_exists(path))
        strbuf_append_str(&sb, "- Feature list\n");

    snprintf(path, sizeof(path), "%s/" INIT_SCRIPT_NAME, root);
    if (nc_path_exists(path))
        strbuf_append_str(&sb, "- Setup script: " INIT_SCRIPT_NAME "\n");

    if (recipe_idx >= 0 && recipe_idx < NUM_RECIPES) {
        strbuf_append_str(&sb, "- Project type: ");
        strbuf_append_str(&sb, s_recipes[recipe_idx].name);
        strbuf_append_str(&sb, "\n");
    }

    strbuf_append_str(&sb, "\nWorkflow:\n"
                           "1. Read progress notes and git log.\n"
                           "2. Run basic tests.\n"
                           "3. Pick one failing feature.\n"
                           "4. Implement and test.\n"
                           "5. Commit progress.\n"
                           "</initializer_context>\n");

    return strbuf_detach(&sb);
}

int init_agent_run(const char *root, int *out_recipe) {
    if (out_recipe)
        *out_recipe = -1;

    if (!root || !init_agent_needed(root))
        return 0;

    int recipe = init_agent_detect_type(root);
    if (out_recipe)
        *out_recipe = recipe;

    progress_tracker_add("Project environment initialized", true);
    progress_tracker_save();

    init_agent_gen_script(root, recipe);
    init_agent_write_marker(root);
    return 1;
}

int init_agent_recipe_count(void) {
    return NUM_RECIPES;
}
