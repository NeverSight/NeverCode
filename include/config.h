#ifndef CONFIG_H
#define CONFIG_H

#include "nevercode.h"

typedef struct {
    char *alias;
    char *model;
} model_alias_t;

typedef struct {
    provider_type_t provider;

    char *native_api_key;
    char *native_base_url;

    char *compat_api_key;
    char *compat_base_url; /* normalized, ending with /v1 */

    char *model;
    int max_tokens;
    char *initial_prompt;
    char *system_prompt_override;

    bool verbose;
    bool no_markdown;
    bool trust_all_tools;
    bool continue_last;
    char *resume_session;
    bool json_output;
    char *output_file;
    char *output_schema;

    char *custom_instructions;
    model_alias_t aliases[MAX_MODEL_ALIASES];
    int num_aliases;

    effort_level_t effort;
    int max_turns;
    char *text_verbosity;    /* NULL, "low", "medium", "high" */
    char *reasoning_summary; /* NULL, "auto", "detailed" */
    char *cwd;
    char *append_prompt;
    /* Paths read during config_parse; merged into append_prompt by finalize */
    char *append_system_prompt_file;
    char *append_system_prompt_file_cli;
    int approval_preset;

    /* Sampling parameters: negative value = omit from request (use API default) */
    double temperature;
    double top_p;
    double frequency_penalty;
    double presence_penalty;
    int seed;           /* -1 = omit (API default), >=0 = fixed seed */
    int context_window; /* 0 = use model registry default */

    /* Custom stop sequences (NULL-terminated) */
    char *stop_sequences[NC_MAX_STOP_SEQUENCES];
    int num_stop_sequences;

    /* Batch mode: path to JSONL file of prompts */
    char *batch_file;
    int batch_concurrent;

    /* Sandbox: enforce path restrictions and OS-level containment */
    bool sandbox;
} config_t;

extern config_t *g_cfg_ptr;

void prompt_append_model_extras(strbuf_t *sb, const config_t *cfg);

const char *config_resolve_alias(const config_t *cfg, const char *name);

void config_init(config_t *cfg);
int config_parse(config_t *cfg, int argc, char **argv);
void config_free(config_t *cfg);
const char *config_api_key(const config_t *cfg);
const char *config_base_url(const config_t *cfg);
const char *config_model(const config_t *cfg);

#endif /* CONFIG_H */
