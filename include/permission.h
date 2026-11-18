#ifndef PERMISSION_H
#define PERMISSION_H

#include "nevercode.h"

typedef enum { PERM_ALLOW, PERM_DENY, PERM_ASK } perm_decision_t;

typedef struct {
    char *tool_name;
    char *path_prefix; /* optional: restrict to paths */
    perm_decision_t decision;
} perm_rule_t;

/* Approval presets (process start: NC_DEFAULT_APPROVAL_PRESET in defaults.h). */
typedef enum {
    APPROVAL_DEFAULT = 0,  /* standard: workspace ops allowed; network access or files
                              outside the workspace require approval */
    APPROVAL_READONLY = 1, /* read-only tools only; prompts for mutating tools */
    APPROVAL_FULL = 2      /* permissive; no permission prompts */
} approval_preset_t;

typedef struct {
    perm_rule_t *rules;
    int num_rules;
    int cap_rules;
    bool trust_all; /* --dangerously-skip-permissions */
    approval_preset_t approval_preset;
} perm_ctx_t;

void perm_init(perm_ctx_t *p);
void perm_free(perm_ctx_t *p);
void perm_add_rule(perm_ctx_t *p, const char *tool, const char *path_prefix, perm_decision_t d);
perm_decision_t perm_check(perm_ctx_t *p, const char *tool, const char *path);
/* True if `path` resolves inside the project workspace or a scratch area. Kernel-correct (component walk, symlink-aware). */
bool perm_path_in_workspace(const char *path);
/* Returns: 0=deny, 1=allow once, 2=allow always */
int perm_prompt_user(const char *tool, const char *detail);
/* Set approval preset and apply its rules */
void perm_set_approval(perm_ctx_t *p, approval_preset_t preset);
/* Parse string to preset enum (-1 on error) */
int perm_parse_approval(const char *str);
const char *perm_approval_name(approval_preset_t preset);

#endif /* PERMISSION_H */
