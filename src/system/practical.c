/*
 * practical.c — Utility functions for slash-commands and custom skills.
 *
 * Sleep inhibition is handled by sleep_inhibit.c (IOKit / systemd).
 */

#include "nevercode.h"
#include "defaults.h"

/* ==== /files: list files currently in context ==== */

char *files_in_context(const read_file_tracker_t *tracker) {
    if (!tracker || tracker->count == 0)
        return nc_xstrdup("No files in context.\n");

    strbuf_t sb;
    strbuf_init(&sb);

    char header[NC_FMT_MAX];
    snprintf(header, sizeof(header), "Files in context (%d):\n", tracker->count);
    strbuf_append_str(&sb, header);

    char cwd[NC_PATH_MAX];
    const char *cwdp = getcwd(cwd, sizeof(cwd));

    for (int i = tracker->count - 1; i >= 0; i--) {
        const char *path = tracker->paths[i];
        const char *display = path;

        if (cwdp && nc_starts_with(path, cwdp) && path[strlen(cwdp)] == '/') {
            display = path + strlen(cwdp) + 1;
        }

        strbuf_append_str(&sb, "  ");
        strbuf_append_str(&sb, display);
        strbuf_append_str(&sb, "\n");
    }
    return strbuf_detach(&sb);
}

/* ==== /pr: commit + push + create PR ==== */

char *build_pr_prompt(void) {
    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_str(&sb, "Create a pull request. Follow these steps EXACTLY:\n\n"
                           "1. Run `git status --short` and `git diff HEAD` to see all changes\n"
                           "2. Run `git branch --show-current` to identify the current branch\n"
                           "3. Run `git log --oneline -10` to understand recent commit style\n"
                           "4. If there are uncommitted changes:\n"
                           "   a. Stage relevant files with `git add`\n"
                           "   b. Create a commit with a concise message (WHY > WHAT)\n"
                           "5. Push the branch: `git push -u origin HEAD`\n"
                           "6. Create the PR: `gh pr create --title \"...\" --body \"...\"`\n\n"
                           "PR body format:\n"
                           "```\n"
                           "## Summary\n"
                           "<1-3 bullet points>\n\n"
                           "## Test plan\n"
                           "<checklist>\n"
                           "```\n\n"
                           "## Safety Rules\n"
                           "- NEVER force push\n"
                           "- NEVER push to main/master directly\n"
                           "- NEVER skip hooks (--no-verify)\n"
                           "- NEVER modify git config\n"
                           "- Do NOT commit .env or credential files\n"
                           "- Return the PR URL when done\n");

    return strbuf_detach(&sb);
}

/* ==== Custom skills from ~/.agents/skills/ ==== */

/* MAX_CUSTOM_SKILLS in defaults.h */
/* uses CUSTOM_SKILL_MAX_SIZE from defaults.h */

typedef struct {
    char name[NC_FMT_MAX];
    char *prompt;
} custom_skill_t;

static custom_skill_t g_custom_skills[MAX_CUSTOM_SKILLS];
static int g_num_custom_skills = 0;

int custom_skills_load(const char *dir) {
    DIR *d = opendir(dir);
    if (!d)
        return 0;

    struct dirent *ent;
    int loaded = 0;
    while ((ent = readdir(d)) && g_num_custom_skills < MAX_CUSTOM_SKILLS) {
        if (!str_has_suffix(ent->d_name, ".md"))
            continue;

        char path[NC_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        if (st.st_size <= 0 || st.st_size > CUSTOM_SKILL_MAX_SIZE)
            continue;

        FILE *f = fopen(path, "r");
        if (!f)
            continue;

        char *buf = malloc(st.st_size + 1);
        if (!buf) {
            fclose(f);
            continue;
        }
        size_t n = fread(buf, 1, st.st_size, f);
        fclose(f);
        buf[n] = '\0';

        custom_skill_t *sk = &g_custom_skills[g_num_custom_skills];

        size_t name_len = strlen(ent->d_name) - 3;
        if (name_len >= sizeof(sk->name))
            name_len = sizeof(sk->name) - 1;
        memcpy(sk->name, ent->d_name, name_len);
        sk->name[name_len] = '\0';

        sk->prompt = buf;
        g_num_custom_skills++;
        loaded++;
    }
    closedir(d);
    return loaded;
}

const char *custom_skill_find(const char *name) {
    if (!name)
        return NULL;
    for (int i = 0; i < g_num_custom_skills; i++) {
        if (strcmp(g_custom_skills[i].name, name) == 0)
            return g_custom_skills[i].prompt;
    }
    return NULL;
}

int custom_skill_list(char **names, int max) {
    int n = 0;
    for (int i = 0; i < g_num_custom_skills && n < max; i++)
        names[n++] = g_custom_skills[i].name;
    return n;
}

void custom_skills_free(void) {
    for (int i = 0; i < g_num_custom_skills; i++)
        free(g_custom_skills[i].prompt);
    g_num_custom_skills = 0;
}

/* ==== /mcp management ==== */

char *mcp_format_server_list(const char *config_path) {
    char *buf = strbuf_read_file(config_path, NULL);
    if (!buf) {
        struct stat st;
        if (stat(config_path, &st) != 0)
            return nc_xstrdup("No MCP configuration found.\nConfig: ~/.agents/mcp.json\n");
        if (S_ISREG(st.st_mode) && st.st_size == 0)
            return nc_xstrdup("MCP config is empty.\n");
        return nc_xstrdup("MCP config read error.\n");
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
        return nc_xstrdup("MCP config parse error.\n");

    cJSON *servers = cJSON_GetObjectItem(root, "servers");
    int ns = cJSON_GetArraySize(servers);

    strbuf_t sb;
    strbuf_init(&sb);

    char header[NC_FMT_MAX];
    snprintf(header, sizeof(header), "MCP Servers (%d):\n", ns);
    strbuf_append_str(&sb, header);

    for (int i = 0; i < ns; i++) {
        cJSON *s = cJSON_GetArrayItem(servers, i);
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(s, "name"));
        const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(s, "command"));
        if (name) {
            strbuf_append_str(&sb, "  ");
            strbuf_append_str(&sb, name);
            if (cmd) {
                strbuf_append_str(&sb, " → ");
                strbuf_append_str(&sb, cmd);
            }
            strbuf_append_str(&sb, "\n");
        }
    }

    strbuf_append_str(&sb, "\nManage: ~/.agents/mcp.json\n");
    strbuf_append_str(&sb, "Format: {\"servers\":[{\"name\":\"...\",\"command\":\"...\"}]}\n");

    cJSON_Delete(root);
    return strbuf_detach(&sb);
}

int mcp_add_server_to_config(const char *config_path, const char *name, const char *command) {
    if (!name || !command)
        return -1;

    cJSON *root = nc_json_load(config_path);
    if (!root)
        root = cJSON_CreateObject();
    cJSON *servers = cJSON_GetObjectItem(root, "servers");
    if (!servers) {
        servers = cJSON_AddArrayToObject(root, "servers");
    }

    /* Check for duplicate */
    int ns = cJSON_GetArraySize(servers);
    for (int i = 0; i < ns; i++) {
        cJSON *s = cJSON_GetArrayItem(servers, i);
        const char *sn = cJSON_GetStringValue(cJSON_GetObjectItem(s, "name"));
        if (sn && strcmp(sn, name) == 0) {
            cJSON_Delete(root);
            return -2;
        }
    }

    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "name", name);
    cJSON_AddStringToObject(entry, "command", command);
    cJSON_AddItemToArray(servers, entry);

    nc_json_save(config_path, root);
    cJSON_Delete(root);
    return 0;
}

int mcp_remove_server_from_config(const char *config_path, const char *name) {
    if (!name)
        return -1;

    cJSON *root = nc_json_load(config_path);
    if (!root)
        return -1;

    cJSON *servers = cJSON_GetObjectItem(root, "servers");
    int ns = cJSON_GetArraySize(servers);
    bool found = false;

    for (int i = 0; i < ns; i++) {
        cJSON *s = cJSON_GetArrayItem(servers, i);
        const char *sn = cJSON_GetStringValue(cJSON_GetObjectItem(s, "name"));
        if (sn && strcmp(sn, name) == 0) {
            cJSON_DeleteItemFromArray(servers, i);
            found = true;
            break;
        }
    }

    if (!found) {
        cJSON_Delete(root);
        return -1;
    }

    nc_json_save(config_path, root);
    cJSON_Delete(root);
    return 0;
}
