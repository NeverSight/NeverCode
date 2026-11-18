/*
 * workflow.c — Bundled and user-defined automation scripts.
 *
 * Discovers workflow scripts from CONFIG_DIR_PRIMARY/workflows/
 * and executes them as tool calls. Workflows are shell scripts
 * with a YAML header defining name, description, and parameters.
 */

#include "nevercode.h"
#include "defaults.h"
#include "tools.h"

#define WF_MAX_SCRIPTS NC_WORKFLOW_MAX_SCRIPTS
#define WF_NAME_MAX NC_WORKFLOW_NAME_MAX
#define WF_DESC_MAX NC_WORKFLOW_DESC_MAX
#define WF_HEADER_SCAN_LINES NC_WORKFLOW_HEADER_LINES

typedef struct {
    char name[WF_NAME_MAX];
    char description[WF_DESC_MAX];
    char path[NC_PATH_MAX];
    bool enabled;
} workflow_entry_t;

static struct {
    workflow_entry_t scripts[WF_MAX_SCRIPTS];
    int count;
    bool initialized;
} g_wf;

static bool parse_header(const char *path, workflow_entry_t *entry) {
    FILE *f = fopen(path, "r");
    if (!f)
        return false;

    char line[NC_MSG_MAX];
    bool in_header = false;
    int lines_read = 0;

    while (fgets(line, sizeof(line), f) && lines_read < WF_HEADER_SCAN_LINES) {
        lines_read++;
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (strcmp(line, "# ---") == 0 || strcmp(line, "#---") == 0) {
            if (in_header)
                break;
            in_header = true;
            continue;
        }
        if (!in_header)
            continue;

        if (nc_starts_with(line, "# name:")) {
            const char *v = line + 7;
            while (*v == ' ')
                v++;
            nc_strlcpy(entry->name, v, sizeof(entry->name));
        } else if (nc_starts_with(line, "# description:")) {
            const char *v = line + 14;
            while (*v == ' ')
                v++;
            nc_strlcpy(entry->description, v, sizeof(entry->description));
        }
    }
    fclose(f);

    if (!entry->name[0]) {
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        nc_strlcpy(entry->name, base, sizeof(entry->name));
        char *dot = strrchr(entry->name, '.');
        if (dot)
            *dot = '\0';
    }
    return entry->name[0] != '\0';
}

void workflow_discover(void) {
    memset(&g_wf, 0, sizeof(g_wf));

    char search_dirs[3][NC_PATH_MAX];
    config_home_path(search_dirs[0], sizeof(search_dirs[0]), CONFIG_DIR_PRIMARY "/workflows");
    config_home_path(search_dirs[1], sizeof(search_dirs[1]), CONFIG_DIR_SECONDARY "/workflows");
    config_home_path(search_dirs[2], sizeof(search_dirs[2]), CONFIG_DIR_COMPAT "/workflows");

    for (int d = 0; d < 3 && g_wf.count < WF_MAX_SCRIPTS; d++) {
        DIR *dir = opendir(search_dirs[d]);
        if (!dir)
            continue;

        struct dirent *ent;
        while ((ent = readdir(dir)) && g_wf.count < WF_MAX_SCRIPTS) {
            if (ent->d_name[0] == '.')
                continue;

            char fpath[NC_PATH_MAX];
            snprintf(fpath, sizeof(fpath), "%.2045s/%.2045s", search_dirs[d], ent->d_name);

            struct stat st;
            if (stat(fpath, &st) != 0 || !S_ISREG(st.st_mode))
                continue;
            if (!(st.st_mode & S_IXUSR))
                continue;

            workflow_entry_t *e = &g_wf.scripts[g_wf.count];
            nc_strlcpy(e->path, fpath, sizeof(e->path));
            e->enabled = true;
            if (parse_header(fpath, e))
                g_wf.count++;
        }
        closedir(dir);
    }
    g_wf.initialized = true;
}

int workflow_count(void) {
    return g_wf.count;
}

const char *workflow_name(int idx) {
    return (idx >= 0 && idx < g_wf.count) ? g_wf.scripts[idx].name : NULL;
}

const char *workflow_desc(int idx) {
    return (idx >= 0 && idx < g_wf.count) ? g_wf.scripts[idx].description : NULL;
}

int workflow_run(int idx, const char *args, char **output) {
    if (idx < 0 || idx >= g_wf.count || !g_wf.scripts[idx].enabled) {
        if (output)
            *output = nc_xstrdup("Workflow not found or disabled");
        return -1;
    }

    char cmd[NC_CMD_MAX];
    if (args && args[0])
        snprintf(cmd, sizeof(cmd), "%s %s 2>&1", g_wf.scripts[idx].path, args);
    else
        snprintf(cmd, sizeof(cmd), "%s 2>&1", g_wf.scripts[idx].path);

    FILE *p = popen(cmd, "r");
    if (!p) {
        if (output)
            *output = nc_xstrdup("Failed to execute workflow");
        return -1;
    }

    strbuf_t sb;
    strbuf_init(&sb);
    char buf[NC_IO_BUF];
    while (fgets(buf, sizeof(buf), p))
        strbuf_append_str(&sb, buf);

    int status = pclose(p);
    int rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    if (output)
        *output = strbuf_detach(&sb);
    else
        strbuf_free(&sb);

    return rc;
}

int workflow_find(const char *name) {
    if (!name)
        return -1;
    for (int i = 0; i < g_wf.count; i++) {
        if (strcasecmp(g_wf.scripts[i].name, name) == 0)
            return i;
    }
    return -1;
}

char *workflow_list_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    if (g_wf.count == 0) {
        strbuf_append_str(&sb, "No workflows found. Place executable scripts in "
                               "~/.agents/workflows/\n");
    } else {
        strbuf_appendf(&sb, "Available workflows (%d):\n", g_wf.count);
        for (int i = 0; i < g_wf.count; i++) {
            strbuf_appendf(&sb, "  %-20s %s\n", g_wf.scripts[i].name,
                           g_wf.scripts[i].description[0] ? g_wf.scripts[i].description
                                                          : "(no description)");
        }
    }
    return strbuf_detach(&sb);
}

static int workflow_tool_execute(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    cJSON *jname = cJSON_GetObjectItem(input, "name");
    if (!jname || !cJSON_IsString(jname)) {
        char *list = workflow_list_format();
        if (output)
            *output = list;
        else
            free(list);
        return 0;
    }

    int idx = workflow_find(jname->valuestring);
    if (idx < 0) {
        if (output) {
            strbuf_t sb;
            strbuf_init(&sb);
            strbuf_appendf(&sb, "Workflow '%s' not found.\n\n", jname->valuestring);
            char *list = workflow_list_format();
            strbuf_append_str(&sb, list);
            free(list);
            *output = strbuf_detach(&sb);
        }
        return -1;
    }

    cJSON *jargs = cJSON_GetObjectItem(input, "args");
    const char *args = (jargs && cJSON_IsString(jargs)) ? jargs->valuestring : NULL;

    return workflow_run(idx, args, output);
}

void workflow_register_tool(void) {
    if (!g_wf.initialized)
        workflow_discover();

    tool_def_t def = {0};
    def.name = "Workflow";
    def.description = "Run user-defined automation scripts from ~/.agents/workflows/. "
                      "Omit name to list available workflows.";
    def.execute = workflow_tool_execute;
    def.read_only = false;

    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON *props = cJSON_AddObjectToObject(schema, "properties");
    cJSON *pn = cJSON_AddObjectToObject(props, "name");
    cJSON_AddStringToObject(pn, "type", "string");
    cJSON_AddStringToObject(pn, "description", "Name of the workflow to run (omit to list all)");
    cJSON *pa = cJSON_AddObjectToObject(props, "args");
    cJSON_AddStringToObject(pa, "type", "string");
    cJSON_AddStringToObject(pa, "description", "Arguments to pass to the workflow");
    def.input_schema = schema;
    tools_register(&def);
}

void tool_workflow_register(void) {
    workflow_register_tool();
}
