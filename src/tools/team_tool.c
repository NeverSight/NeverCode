/* team_tool.c — Team creation/deletion tools for swarm orchestration. */
#include "tools.h"
#include "nevercode.h"

static const char *TC_S =
    "{\"type\":\"object\",\"properties\":{"
    "\"team_name\":{\"type\":\"string\",\"description\":\"Name for the new team\"},"
    "\"description\":{\"type\":\"string\",\"description\":\"Team purpose/goal\"},"
    "\"agent_type\":{\"type\":\"string\",\"description\":\"Type/role of the team lead\"}"
    "},\"required\":[\"team_name\"]}";

static const char *TD_S =
    "{\"type\":\"object\",\"properties\":{"
    "\"team_name\":{\"type\":\"string\",\"description\":\"Name of the team to disband\"}"
    "},\"required\":[\"team_name\"]}";

static int tc_exec(const cJSON *in, char **out, void *ctx) {
    (void)ctx;
    const char *n = nc_cjson_str((cJSON *)in, "team_name", NULL);
    const char *d = nc_cjson_str((cJSON *)in, "description", NULL);
    if (!n || !*n) {
        *out = nc_xstrdup("error: team_name required");
        return 1;
    }
    int tid = team_create(n, d ? d : "");
    if (tid < 0) {
        *out = nc_xstrdup("error: create failed");
        return 1;
    }
    const char *at = nc_cjson_str((cJSON *)in, "agent_type", NULL);
    if (!at)
        at = "lead";
    char lid[NC_TAG_MAX];
    snprintf(lid, sizeof(lid), "lead@%s", n);
    team_add_member(tid, lid, at);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "team_name", n);
    cJSON_AddStringToObject(r, "lead_agent_id", lid);
    cJSON_AddNumberToObject(r, "team_id", tid);
    *out = nc_cjson_detach_pretty(r);
    return 0;
}

static int td_exec(const cJSON *in, char **out, void *ctx) {
    (void)ctx;
    const char *n = nc_cjson_str((cJSON *)in, "team_name", NULL);
    if (!n || !*n) {
        *out = nc_xstrdup("error: team_name required");
        return 1;
    }
    int found = -1;
    for (int i = 0; i < team_count(); i++) {
        const char *tn = team_name(i);
        if (tn && strcmp(tn, n) == 0) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        *out = nc_xstrdup("error: team not found");
        return 1;
    }
    team_disband(found);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "team_name", n);
    cJSON_AddStringToObject(r, "status", "disbanded");
    *out = nc_cjson_detach_pretty(r);
    return 0;
}

static tool_def_t g_tc, g_td;

void tool_team_register(void) {
    g_tc.name = "TeamCreate";
    g_tc.description = "Create a new agent team for coordinated work.";
    g_tc.input_schema = cJSON_Parse(TC_S);
    g_tc.execute = tc_exec;
    g_tc.read_only = false;
    g_tc.concurrency_safe = false;
    tools_register(&g_tc);

    g_td.name = "TeamDelete";
    g_td.description = "Disband a team and stop all workers.";
    g_td.input_schema = cJSON_Parse(TD_S);
    g_td.execute = td_exec;
    g_td.read_only = false;
    g_td.concurrency_safe = false;
    tools_register(&g_td);
}
