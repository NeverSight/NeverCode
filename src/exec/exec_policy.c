#include "nevercode.h"
#include <fnmatch.h>

typedef struct {
    char *pattern;
    bool writable;
} ep_path_grant_t;

typedef struct {
    char *host_pattern;
    bool allowed;
} ep_net_grant_t;

typedef struct {
    char *description;
    long timestamp;
} ep_amendment_t;

typedef struct {
    ep_mode_t mode;
    ep_path_grant_t paths[EP_MAX_PATH_GRANTS];
    int path_count;
    ep_net_grant_t nets[EP_MAX_NET_GRANTS];
    int net_count;
    ep_amendment_t amendments[EP_MAX_AMENDMENTS];
    int amendment_count;
    bool inherit_from_parent;
    bool initialized;
} exec_policy_ctx_t;

static exec_policy_ctx_t g_policy;

void exec_policy_init(void) {
    memset(&g_policy, 0, sizeof(g_policy));
    g_policy.mode = EP_FULL_ACCESS;
    g_policy.initialized = true;
}

void exec_policy_free(void) {
    for (int i = 0; i < g_policy.path_count; i++)
        free(g_policy.paths[i].pattern);
    for (int i = 0; i < g_policy.net_count; i++)
        free(g_policy.nets[i].host_pattern);
    for (int i = 0; i < g_policy.amendment_count; i++)
        free(g_policy.amendments[i].description);
    memset(&g_policy, 0, sizeof(g_policy));
}

void exec_policy_set_mode(ep_mode_t mode) {
    g_policy.mode = mode;
}

ep_mode_t exec_policy_mode(void) {
    return g_policy.mode;
}

const char *exec_policy_mode_name(ep_mode_t m) {
    switch (m) {
    case EP_FULL_ACCESS:
        return "full_access";
    case EP_READ_ONLY:
        return "read_only";
    case EP_SANDBOXED:
        return "sandboxed";
    case EP_CUSTOM:
        return "custom";
    default:
        return "unknown";
    }
}

int exec_policy_grant_path(const char *pattern, bool writable) {
    if (!pattern || g_policy.path_count >= EP_MAX_PATH_GRANTS)
        return -1;
    g_policy.paths[g_policy.path_count].pattern = nc_xstrdup(pattern);
    g_policy.paths[g_policy.path_count].writable = writable;
    g_policy.path_count++;
    return 0;
}

bool exec_policy_check_path(const char *path, bool write_op) {
    if (!g_policy.initialized)
        return false;
    if (g_policy.mode == EP_FULL_ACCESS)
        return true;
    if (g_policy.mode == EP_READ_ONLY)
        return !write_op;
    if (!path)
        return false;
    for (int i = 0; i < g_policy.path_count; i++) {
        if (fnmatch(g_policy.paths[i].pattern, path, 0) == 0) {
            if (write_op && !g_policy.paths[i].writable)
                continue;
            return true;
        }
    }
    return false;
}

int exec_policy_path_grant_count(void) {
    return g_policy.path_count;
}

int exec_policy_grant_network(const char *host_pattern, bool allowed) {
    if (!host_pattern || g_policy.net_count >= EP_MAX_NET_GRANTS)
        return -1;
    g_policy.nets[g_policy.net_count].host_pattern = nc_xstrdup(host_pattern);
    g_policy.nets[g_policy.net_count].allowed = allowed;
    g_policy.net_count++;
    return 0;
}

bool exec_policy_check_network(const char *host) {
    if (!g_policy.initialized)
        return false;
    if (g_policy.mode == EP_FULL_ACCESS)
        return true;
    if (!host)
        return false;
    for (int i = 0; i < g_policy.net_count; i++) {
        if (fnmatch(g_policy.nets[i].host_pattern, host, 0) == 0)
            return g_policy.nets[i].allowed;
    }
    return (g_policy.mode != EP_SANDBOXED);
}

char *exec_policy_serialize(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "mode", exec_policy_mode_name(g_policy.mode));
    cJSON_AddBoolToObject(root, "inheritable", g_policy.inherit_from_parent);

    cJSON *pa = cJSON_AddArrayToObject(root, "paths");
    for (int i = 0; i < g_policy.path_count; i++) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "pattern", g_policy.paths[i].pattern);
        cJSON_AddBoolToObject(p, "writable", g_policy.paths[i].writable);
        cJSON_AddItemToArray(pa, p);
    }

    cJSON *na = cJSON_AddArrayToObject(root, "networks");
    for (int i = 0; i < g_policy.net_count; i++) {
        cJSON *n = cJSON_CreateObject();
        cJSON_AddStringToObject(n, "pattern", g_policy.nets[i].host_pattern);
        cJSON_AddBoolToObject(n, "allowed", g_policy.nets[i].allowed);
        cJSON_AddItemToArray(na, n);
    }

    char *json = nc_cjson_detach(root);
    return json;
}

int exec_policy_deserialize(const char *json) {
    if (!json)
        return -1;
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return -1;

    cJSON *mj = cJSON_GetObjectItem(root, "mode");
    if (mj && cJSON_IsString(mj)) {
        if (strcmp(mj->valuestring, "full_access") == 0)
            g_policy.mode = EP_FULL_ACCESS;
        else if (strcmp(mj->valuestring, "read_only") == 0)
            g_policy.mode = EP_READ_ONLY;
        else if (strcmp(mj->valuestring, "sandboxed") == 0)
            g_policy.mode = EP_SANDBOXED;
        else
            g_policy.mode = EP_CUSTOM;
    }

    cJSON *inh = cJSON_GetObjectItem(root, "inheritable");
    if (inh)
        g_policy.inherit_from_parent = cJSON_IsTrue(inh);

    cJSON *pj = cJSON_GetObjectItem(root, "paths");
    if (pj && cJSON_IsArray(pj)) {
        cJSON *p;
        cJSON_ArrayForEach(p, pj) {
            cJSON *pat = cJSON_GetObjectItem(p, "pattern");
            cJSON *wr = cJSON_GetObjectItem(p, "writable");
            if (pat && cJSON_IsString(pat))
                exec_policy_grant_path(pat->valuestring, wr ? cJSON_IsTrue(wr) : false);
        }
    }

    cJSON *nj = cJSON_GetObjectItem(root, "networks");
    if (nj && cJSON_IsArray(nj)) {
        cJSON *n;
        cJSON_ArrayForEach(n, nj) {
            cJSON *pat = cJSON_GetObjectItem(n, "pattern");
            cJSON *al = cJSON_GetObjectItem(n, "allowed");
            if (pat && cJSON_IsString(pat))
                exec_policy_grant_network(pat->valuestring, al ? cJSON_IsTrue(al) : true);
        }
    }

    cJSON_Delete(root);
    return 0;
}

char *exec_policy_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "ExecPolicy: mode=%s paths=%d nets=%d amendments=%d inherit=%s\n",
                   exec_policy_mode_name(g_policy.mode), g_policy.path_count, g_policy.net_count,
                   g_policy.amendment_count, g_policy.inherit_from_parent ? "yes" : "no");
    return strbuf_detach(&sb);
}
