#include "nevercode.h"
#include <fnmatch.h>

/*
 * Network Policy System — domain-level allow/deny rules for network access.
 *
 * Provides configurable network access control that integrates with BashTool
 * to intercept curl/wget/fetch/httpie commands to unapproved domains.
 */

static network_policy_t g_net_policy;

void network_policy_init(network_policy_t *p) {
    memset(p, 0, sizeof(*p));
    p->default_action = NET_ACTION_ALLOW;
}

void network_policy_free(network_policy_t *p) {
    if (!p)
        return;
    for (int i = 0; i < p->num_rules; i++) {
        free(p->rules[i].pattern);
    }
    p->num_rules = 0;
}

int network_policy_add_rule(network_policy_t *p, const char *pattern, net_action_t action) {
    if (!p || !pattern || p->num_rules >= MAX_NET_RULES)
        return -1;
    net_rule_t *r = &p->rules[p->num_rules++];
    r->pattern = nc_xstrdup(pattern);
    r->action = action;
    return 0;
}

net_action_t network_policy_check(const network_policy_t *p, const char *host) {
    if (!p || !host)
        return NET_ACTION_ASK;

    for (int i = p->num_rules - 1; i >= 0; i--) {
        const net_rule_t *r = &p->rules[i];
        if (!r->pattern)
            continue;

        if (fnmatch(r->pattern, host, FNM_CASEFOLD) == 0)
            return r->action;

        if (r->pattern[0] == '.' && strlen(host) > strlen(r->pattern)) {
            const char *suffix = host + strlen(host) - strlen(r->pattern);
            if (strcasecmp(suffix, r->pattern) == 0)
                return r->action;
        }
    }

    return p->default_action;
}

static char *extract_host_from_url(const char *url) {
    if (!url)
        return NULL;

    const char *start = url;
    if (nc_starts_with(start, "http://"))
        start += 7;
    else if (nc_starts_with(start, "https://"))
        start += 8;
    else if (nc_starts_with(start, "ftp://"))
        start += 6;

    if (start == url)
        return NULL;

    const char *end = start;
    while (*end && *end != '/' && *end != ':' && *end != '?' && *end != '#')
        end++;

    size_t len = end - start;
    if (len == 0 || len > 253)
        return NULL;

    char *host = malloc(len + 1);
    if (!host)
        return NULL;
    memcpy(host, start, len);
    host[len] = '\0';

    for (char *p = host; *p; p++)
        *p = tolower((unsigned char)*p);

    return host;
}

net_action_t network_policy_check_url(const network_policy_t *p, const char *url) {
    char *host = extract_host_from_url(url);
    if (!host)
        return p ? p->default_action : NET_ACTION_ASK;
    net_action_t result = network_policy_check(p, host);
    free(host);
    return result;
}

static const char *net_cmd_prefixes[] = {"curl ",  "wget ",   "httpie ", "http ", "https ",
                                         "fetch ", "aria2c ", "axel ",   NULL};

char *network_policy_extract_urls_from_cmd(const char *cmd) {
    if (!cmd)
        return NULL;

    const char *p = cmd;
    while (*p == ' ')
        p++;

    bool is_net_cmd = false;
    for (int i = 0; net_cmd_prefixes[i]; i++) {
        if (nc_starts_with(p, net_cmd_prefixes[i])) {
            is_net_cmd = true;
            break;
        }
    }
    if (!is_net_cmd)
        return NULL;

    strbuf_t urls;
    strbuf_init(&urls);

    const char *scan = p;
    while (*scan) {
        if (nc_starts_with(scan, "http://") || nc_starts_with(scan, "https://")) {
            const char *url_start = scan;
            while (*scan && *scan != ' ' && *scan != '\'' && *scan != '"' && *scan != ')' &&
                   *scan != '|' && *scan != ';')
                scan++;
            size_t url_len = scan - url_start;
            if (url_len > 0) {
                if (urls.len > 0)
                    strbuf_append_str(&urls, "\n");
                strbuf_append(&urls, url_start, url_len);
            }
        } else {
            scan++;
        }
    }

    if (urls.len == 0) {
        strbuf_free(&urls);
        return NULL;
    }
    return strbuf_detach(&urls);
}

net_action_t network_policy_check_cmd(const network_policy_t *p, const char *cmd) {
    char *urls = network_policy_extract_urls_from_cmd(cmd);
    if (!urls)
        return NET_ACTION_ALLOW;

    net_action_t result = NET_ACTION_ALLOW;
    char *line = urls;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = '\0';

        net_action_t a = network_policy_check_url(p, line);
        if (a == NET_ACTION_DENY) {
            result = NET_ACTION_DENY;
            break;
        }
        if (a == NET_ACTION_ASK && result == NET_ACTION_ALLOW)
            result = NET_ACTION_ASK;

        line = next;
    }
    free(urls);
    return result;
}

int network_policy_load(network_policy_t *p, const char *path) {
    if (!p || !path)
        return -1;

    cJSON *root = nc_json_load(path);
    if (!root)
        return -1;

    cJSON *def = cJSON_GetObjectItem(root, "default");
    if (def) {
        const char *d = cJSON_GetStringValue(def);
        if (d) {
            if (strcmp(d, "allow") == 0)
                p->default_action = NET_ACTION_ALLOW;
            else if (strcmp(d, "deny") == 0)
                p->default_action = NET_ACTION_DENY;
            else
                p->default_action = NET_ACTION_ASK;
        }
    }

    cJSON *allow = cJSON_GetObjectItem(root, "allow");
    int na = cJSON_GetArraySize(allow);
    for (int i = 0; i < na; i++) {
        cJSON *item = cJSON_GetArrayItem(allow, i);
        const char *pat = cJSON_GetStringValue(item);
        if (pat)
            network_policy_add_rule(p, pat, NET_ACTION_ALLOW);
    }

    cJSON *deny = cJSON_GetObjectItem(root, "deny");
    int nd = cJSON_GetArraySize(deny);
    for (int i = 0; i < nd; i++) {
        cJSON *item = cJSON_GetArrayItem(deny, i);
        const char *pat = cJSON_GetStringValue(item);
        if (pat)
            network_policy_add_rule(p, pat, NET_ACTION_DENY);
    }

    cJSON_Delete(root);
    return p->num_rules;
}

network_policy_t *network_policy_global(void) {
    return &g_net_policy;
}

char *network_policy_format(const network_policy_t *p) {
    if (!p)
        return nc_xstrdup("(no network policy)");

    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_str(&sb, "Network Policy:\n");
    const char *def_str = "ask";
    if (p->default_action == NET_ACTION_ALLOW)
        def_str = "allow";
    else if (p->default_action == NET_ACTION_DENY)
        def_str = "deny";
    strbuf_append_str(&sb, "  Default: ");
    strbuf_append_str(&sb, def_str);
    strbuf_append_str(&sb, "\n");

    if (p->num_rules == 0) {
        strbuf_append_str(&sb, "  Rules: (none)\n");
    } else {
        char line[NC_MSG_MAX];
        for (int i = 0; i < p->num_rules; i++) {
            const char *act = "ask";
            if (p->rules[i].action == NET_ACTION_ALLOW)
                act = "allow";
            else if (p->rules[i].action == NET_ACTION_DENY)
                act = "deny";
            snprintf(line, sizeof(line), "  %s: %s\n", act, p->rules[i].pattern);
            strbuf_append_str(&sb, line);
        }
    }

    return strbuf_detach(&sb);
}
