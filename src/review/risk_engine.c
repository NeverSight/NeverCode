/*
 * risk_engine.c — local heuristic risk scoring for tools and shell
 */

#include "nevercode.h"

static const char *find_ci(const char *hay, const char *ndl) {
    if (!hay || !ndl || !*ndl)
        return NULL;
    for (; *hay; hay++) {
        const char *h = hay, *n = ndl;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++;
            n++;
        }
        if (!*n)
            return hay;
    }
    return NULL;
}

static int has_ci(const char *h, const char *n) {
    return h && n && find_ci(h, n) != NULL;
}

int risk_score_command(const char *cmd) {
    if (!cmd)
        return 0;
    int s = 0;
    if (has_ci(cmd, "rm ") && has_ci(cmd, "-rf"))
        s += 45;
    if (has_ci(cmd, "mkfs") || has_ci(cmd, "dd if="))
        s += 50;
    if (has_ci(cmd, "chmod ") && has_ci(cmd, "777"))
        s += 15;
    if (has_ci(cmd, "curl ") && (has_ci(cmd, "| sh") || has_ci(cmd, "| bash")))
        s += 40;
    if (has_ci(cmd, "wget ") && has_ci(cmd, "| "))
        s += 35;
    if (has_ci(cmd, "sudo ") || has_ci(cmd, "su "))
        s += 20;
    if (has_ci(cmd, "> /dev/") || has_ci(cmd, ":/dev/"))
        s += 25;
    if (s > 100)
        s = 100;
    return s;
}

int risk_score_path(const char *path) {
    if (!path)
        return 0;
    int s = 0;
    if (nc_starts_with(path, "/etc/") || nc_starts_with(path, "/sys/"))
        s += 30;
    if (strstr(path, ".ssh") || strstr(path, "id_rsa"))
        s += 35;
    if (strstr(path, ".gnupg") || strstr(path, "credentials"))
        s += 25;
    if (strstr(path, "/Library/Keychains") || strstr(path, "keychain"))
        s += 20;
    if (s > 100)
        s = 100;
    return s;
}

risk_level_t risk_assess_tool(const char *tool_name, const cJSON *input) {
    (void)tool_name;
    (void)input;
    return RISK_LOW;
}

const char *risk_level_name(risk_level_t level) {
    switch (level) {
    case RISK_LOW:
        return "low";
    case RISK_MEDIUM:
        return "medium";
    case RISK_HIGH:
        return "high";
    case RISK_CRITICAL:
        return "critical";
    default:
        return "?";
    }
}

char *risk_assess_describe(const char *tool_name, const cJSON *input) {
    risk_level_t lv = risk_assess_tool(tool_name, input);
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "%s: level=%s", tool_name ? tool_name : "(null)", risk_level_name(lv));
    if (input && cJSON_IsObject(input)) {
        const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(input, "command");
        if (cJSON_IsString(cmd) && cmd->valuestring)
            strbuf_appendf(&sb, " cmd_score=%d", risk_score_command(cmd->valuestring));
    }
    return strbuf_detach(&sb);
}
