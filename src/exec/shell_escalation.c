/*
 * shell_escalation.c — Privilege escalation and destructive command detection.
 *
 * Scans shell commands for sudo/doas/pkexec prefixes and known destructive
 * patterns (rm -rf /, dd if=, etc.). Provides prefix stripping for commands
 * that need de-escalation before safe execution.
 */

#include "nevercode.h"
#include "defaults.h"

static const char *ESC_CMDS[] = {"sudo", "doas", "pkexec", "su", "runas", NULL};

static const char *DNG_CMDS[] = {"rm -rf /",   "mkfs",        "dd if=",     "chmod 777 /",
                                 "chown root", ":(){:|:&};:", "> /dev/sda", NULL};

typedef enum { ESCALATE_NONE = 0, ESCALATE_DETECTED, ESCALATE_DANGER } escalation_level_t;

typedef struct {
    escalation_level_t level;
    const char *matched_cmd;
    int prefix_offset;
} escalation_result_t;

static const char *skw(const char *s) {
    while (*s && isspace((unsigned char)*s))
        s++;
    return s;
}

static bool sw(const char *l, const char *w) {
    int n = (int)strlen(w);
    return strncmp(l, w, n) == 0 && (l[n] == '\0' || isspace((unsigned char)l[n]));
}

escalation_result_t check_escalation(const char *cmd) {
    escalation_result_t r = {ESCALATE_NONE, NULL, 0};
    if (!cmd)
        return r;

    const char *t = skw(cmd);
    if (!*t)
        return r;

    /* Check destructive patterns first */
    for (int i = 0; DNG_CMDS[i]; i++)
        if (strstr(t, DNG_CMDS[i])) {
            r.level = ESCALATE_DANGER;
            r.matched_cmd = DNG_CMDS[i];
            return r;
        }

    /* Check direct escalation prefix */
    for (int i = 0; ESC_CMDS[i]; i++)
        if (sw(t, ESC_CMDS[i])) {
            r.level = ESCALATE_DETECTED;
            r.matched_cmd = ESC_CMDS[i];
            return r;
        }

    /* Check escalation after pipe */
    const char *p = t;
    while ((p = strchr(p, '|')) != NULL) {
        p++;
        const char *nx = skw(p);
        for (int i = 0; ESC_CMDS[i]; i++)
            if (sw(nx, ESC_CMDS[i])) {
                r.level = ESCALATE_DETECTED;
                r.matched_cmd = ESC_CMDS[i];
                return r;
            }
    }

    /* Check escalation after && or ; */
    p = t;
    while (*p) {
        if ((*p == '&' && *(p + 1) == '&') || *p == ';') {
            p += (*p == '&') ? 2 : 1;
            const char *nx = skw(p);
            for (int i = 0; ESC_CMDS[i]; i++)
                if (sw(nx, ESC_CMDS[i])) {
                    r.level = ESCALATE_DETECTED;
                    r.matched_cmd = ESC_CMDS[i];
                    return r;
                }
        } else {
            p++;
        }
    }

    return r;
}

const char *strip_escalation_prefix(const char *cmd) {
    if (!cmd)
        return cmd;
    const char *t = skw(cmd);
    for (int i = 0; ESC_CMDS[i]; i++) {
        if (sw(t, ESC_CMDS[i])) {
            const char *r = t + strlen(ESC_CMDS[i]);
            r = skw(r);
            /* Skip option flags (e.g. sudo -u user) */
            while (*r == '-') {
                while (*r && !isspace((unsigned char)*r))
                    r++;
                r = skw(r);
                if (*r && *r != '-')
                    break;
            }
            return r;
        }
    }
    return cmd;
}

bool is_destructive_command(const char *cmd) {
    return check_escalation(cmd).level == ESCALATE_DANGER;
}
