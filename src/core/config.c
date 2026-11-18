#include "config.h"
#include "permission.h"
#include <getopt.h>

/* NC_APPEND_SYSTEM_PROMPT_MAX is in defaults.h */

config_t *g_cfg_ptr = NULL;

/* Use nc_xstrdup from nevercode.h */
#define xstrdup nc_xstrdup
static void xfree(char **p) {
    free(*p);
    *p = NULL;
}

bool nc_provider_parse(const char *value, provider_type_t *provider_out) {
    if (!value || !provider_out)
        return false;
    if (strcasecmp(value, "compat") == 0 || strcasecmp(value, PROVIDER_ALIAS_COMPAT) == 0) {
        *provider_out = PROVIDER_COMPAT;
        return true;
    }
    if (strcasecmp(value, "native") == 0 || strcasecmp(value, PROVIDER_ALIAS_NATIVE) == 0) {
        *provider_out = PROVIDER_NATIVE;
        return true;
    }
    return false;
}

const char *nc_provider_slug(provider_type_t provider) {
    switch (provider) {
    case PROVIDER_NATIVE:
        return "native";
    case PROVIDER_COMPAT:
        return "compat";
    }
    return "compat";
}

const char *nc_provider_label(provider_type_t provider) {
    switch (provider) {
    case PROVIDER_NATIVE:
        return "Native API";
    case PROVIDER_COMPAT:
        return "Compat API";
    }
    return "Compat API";
}

const char *nc_provider_default_model(provider_type_t provider) {
    switch (provider) {
    case PROVIDER_NATIVE:
        return DEFAULT_NATIVE_MODEL;
    case PROVIDER_COMPAT:
        return DEFAULT_COMPAT_MODEL;
    }
    return DEFAULT_COMPAT_MODEL;
}

static char g_config_home[NC_PATH_MAX] = {0};
static const char *k_config_dirs[] = {CONFIG_DIR_PRIMARY, CONFIG_DIR_SECONDARY, CONFIG_DIR_COMPAT};
static const int k_config_dirs_n = NC_ARRAY_LEN(k_config_dirs);

const char *config_home_dir(void) {
    if (g_config_home[0])
        return g_config_home;
    const char *home = nc_home_dir();
    if (!home) {
        snprintf(g_config_home, sizeof(g_config_home), "/tmp%s", CONFIG_DIR_PRIMARY);
        return g_config_home;
    }

    for (int i = 0; i < k_config_dirs_n; i++) {
        char path[NC_PATH_MAX];
        snprintf(path, sizeof(path), "%s%s", home, k_config_dirs[i]);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            nc_strlcpy(g_config_home, path, sizeof(g_config_home));
            return g_config_home;
        }
    }
    snprintf(g_config_home, sizeof(g_config_home), "%s%s", home, CONFIG_DIR_PRIMARY);
    return g_config_home;
}

void config_home_path(char *out, size_t sz, const char *subpath) {
    snprintf(out, sz, "%s/%s", config_home_dir(), subpath);
}

bool config_find_path(char *out, size_t sz, const char *subpath) {
    const char *home = nc_home_dir();
    if (!home) {
        config_home_path(out, sz, subpath);
        return false;
    }

    for (int i = 0; i < k_config_dirs_n; i++) {
        snprintf(out, sz, "%s%s/%s", home, k_config_dirs[i], subpath);
        struct stat st;
        if (stat(out, &st) == 0)
            return true;
    }
    config_home_path(out, sz, subpath);
    return false;
}

void config_home_reset_cache(void) {
    g_config_home[0] = '\0';
    nc_home_dir_reset();
}

void config_init(config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->provider = DEFAULT_PROVIDER;
    cfg->max_tokens = DEFAULT_MAX_TOKENS;
    cfg->trust_all_tools = false;
    cfg->approval_preset = NC_DEFAULT_APPROVAL_PRESET;
    cfg->temperature = SAMPLING_TEMPERATURE_DEFAULT;
    cfg->top_p = SAMPLING_TOP_P_DEFAULT;
    cfg->frequency_penalty = SAMPLING_FREQ_PENALTY_DEFAULT;
    cfg->presence_penalty = SAMPLING_PRES_PENALTY_DEFAULT;
    cfg->seed = SAMPLING_SEED_DEFAULT;
    cfg->batch_concurrent = BATCH_MAX_CONCURRENT;
}

/*
 * Compat-API base URL normalization:
 * 1. strip trailing '/'
 * 2. if ends with /v1/chat/completions → trim to parent
 * 3. if does NOT end with /v1 → append /v1
 */
static char *normalize_compat_url(const char *raw) {
    if (!raw)
        raw = DEFAULT_COMPAT_URL;
    char *url = nc_xstrdup(raw);
    size_t len = strlen(url);

    while (len > 0 && url[len - 1] == '/')
        url[--len] = '\0';

    const char *suffix1 = "/v1/chat/completions";
    size_t s1 = strlen(suffix1);
    if (len >= s1 && strcmp(url + len - s1, suffix1) == 0) {
        url[len - s1] = '\0';
        len -= s1;
    }

    const char *suffix2 = "/v1";
    if (len < 3 || strcmp(url + len - 3, suffix2) != 0) {
        size_t need = len + 4;
        char *tmp = realloc(url, need);
        if (!tmp) {
            free(url);
            return nc_xstrdup(DEFAULT_COMPAT_URL "/v1");
        }
        url = tmp;
        memcpy(url + len, "/v1", 4);
    }
    return url;
}

static char *normalize_alt_url(const char *raw) {
    if (!raw)
        return nc_xstrdup(DEFAULT_NATIVE_URL);
    char *url = nc_xstrdup(raw);
    size_t len = strlen(url);
    while (len > 0 && url[len - 1] == '/')
        url[--len] = '\0';
    return url;
}

static void load_config_file(config_t *cfg) {
    /* Built-in model presets + optional overrides from config.json */
    model_presets_init();

    char path[NC_PATH_MAX];
    config_find_path(path, sizeof(path), NC_CFG_CONFIG);

    char *buf = strbuf_read_file(path, NULL);
    if (!buf)
        return;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
        return;

    const char *v;
    if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(root, "provider"))))
        nc_provider_parse(v, &cfg->provider);
    if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(root, "model"))))
        cfg->model = xstrdup(v);
    if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(root, "base_url"))))
        cfg->compat_base_url = normalize_compat_url(v);
    if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(root, "native_base_url"))))
        cfg->native_base_url = normalize_alt_url(v);

    cfg->max_tokens = nc_cjson_int(root, "max_tokens", cfg->max_tokens);

    if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(root, "custom_instructions"))))
        cfg->custom_instructions = xstrdup(v);

    if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(root, "append_system_prompt_file"))))
        cfg->append_system_prompt_file = xstrdup(v);

    if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(root, "text_verbosity"))))
        cfg->text_verbosity = xstrdup(v);
    if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(root, "reasoning_summary"))))
        cfg->reasoning_summary = xstrdup(v);

    cJSON *eff = cJSON_GetObjectItem(root, "default_effort");
    if (eff && cJSON_IsString(eff))
        cfg->effort = effort_level_parse(eff->valuestring);
    else if (eff && cJSON_IsNumber(eff)) {
        /* Legacy numeric effort: 0=default, 1=low, 2=medium, 3=high */
        int legacy = (int)eff->valuedouble;
        if (legacy == 1)
            cfg->effort = EFFORT_LOW;
        else if (legacy == 2)
            cfg->effort = EFFORT_MEDIUM;
        else if (legacy == 3)
            cfg->effort = EFFORT_HIGH;
    }

    cJSON *aliases = cJSON_GetObjectItem(root, "model_aliases");
    if (aliases && cJSON_IsObject(aliases)) {
        cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, aliases) {
            if (cfg->num_aliases >= MAX_MODEL_ALIASES)
                break;
            if (!cJSON_IsString(entry))
                continue;
            model_alias_t *a = &cfg->aliases[cfg->num_aliases++];
            a->alias = xstrdup(entry->string);
            a->model = xstrdup(entry->valuestring);
        }
    }

    cfg->seed = nc_cjson_int(root, "seed", cfg->seed);
    cfg->context_window = nc_cjson_int(root, "context_window", cfg->context_window);

    {
        cJSON *stops = cJSON_GetObjectItem(root, "stop");
        if (stops && cJSON_IsArray(stops)) {
            int ns = cJSON_GetArraySize(stops);
            for (int i = 0; i < ns && cfg->num_stop_sequences < NC_MAX_STOP_SEQUENCES; i++) {
                const char *sv = cJSON_GetStringValue(cJSON_GetArrayItem(stops, i));
                if (sv)
                    cfg->stop_sequences[cfg->num_stop_sequences++] = xstrdup(sv);
            }
        }
    }

    {
        cJSON *mps = cJSON_GetObjectItem(root, "model_presets");
        if (mps && cJSON_IsArray(mps))
            model_preset_load_from_config(mps);
    }

    {
        cJSON *sb = cJSON_GetObjectItem(root, "sandbox");
        if (sb && cJSON_IsTrue(sb))
            cfg->sandbox = true;
    }

    model_presets_set_provider(cfg->provider);
    cJSON_Delete(root);
}

static void resolve_user_path(const char *src, char *out, size_t outsz) {
    if (!src || !out || outsz == 0) {
        if (out && outsz)
            out[0] = '\0';
        return;
    }
    if (src[0] == '~' && src[1] == '/') {
        const char *home = nc_home_dir();
        if (home)
            snprintf(out, outsz, "%s%s", home, src + 1);
        else
            nc_strlcpy(out, src, outsz);
    } else
        nc_strlcpy(out, src, outsz);
}

static void append_system_prompt_file_contents(strbuf_t *sb, const char *path) {
    char resolved[NC_PATH_MAX];
    resolve_user_path(path, resolved, sizeof(resolved));
    char *body = strbuf_read_file_max(resolved, NULL, NC_APPEND_SYSTEM_PROMPT_MAX);
    if (!body || !body[0]) {
        free(body);
        return;
    }
    if (sb->len > 0)
        strbuf_append_str(sb, "\n\n");
    strbuf_append_str(sb, body);
    free(body);
}

/*
 * Merges, in order: append_system_prompt_file (config.json),
 * NEVERCODE_APPEND_SYSTEM_PROMPT_FILE, --append-system-prompt-file, -A text.
 */
static void config_finalize_append_prompt(config_t *cfg) {
    char *cli_text = cfg->append_prompt;
    cfg->append_prompt = NULL;

    strbuf_t sb;
    strbuf_init(&sb);

    if (cfg->append_system_prompt_file && cfg->append_system_prompt_file[0])
        append_system_prompt_file_contents(&sb, cfg->append_system_prompt_file);

    const char *envp = getenv(ENV_APPEND_SYSPROMPT);
    if (envp && envp[0])
        append_system_prompt_file_contents(&sb, envp);

    if (cfg->append_system_prompt_file_cli && cfg->append_system_prompt_file_cli[0])
        append_system_prompt_file_contents(&sb, cfg->append_system_prompt_file_cli);

    if (cli_text && cli_text[0]) {
        if (sb.len > 0)
            strbuf_append_str(&sb, "\n\n");
        strbuf_append_str(&sb, cli_text);
    }
    free(cli_text);

    if (sb.len > 0)
        cfg->append_prompt = strbuf_detach(&sb);

    strbuf_free(&sb);
    xfree(&cfg->append_system_prompt_file);
    xfree(&cfg->append_system_prompt_file_cli);
}

static void load_env(config_t *cfg) {
    const char *v;
    if ((v = getenv(ENV_NATIVE_KEY)))
        cfg->native_api_key = xstrdup(v);
    if ((v = getenv(ENV_NATIVE_URL)))
        cfg->native_base_url = normalize_alt_url(v);

    if ((v = getenv(ENV_COMPAT_KEY)))
        cfg->compat_api_key = xstrdup(v);

    if ((v = getenv(ENV_COMPAT_URL)))
        cfg->compat_base_url = normalize_compat_url(v);

    if ((v = getenv(ENV_EFFORT)) && v[0])
        cfg->effort = effort_level_parse(v);

    if ((v = getenv(ENV_SANDBOX)) && nc_parse_bool(v, false))
        cfg->sandbox = true;
}

#define OPT_TEXT_VERBOSITY 256
#define OPT_REASONING_SUMMARY 257
#define OPT_TEMPERATURE 258
#define OPT_TOP_P 259
#define OPT_FREQ_PENALTY 260
#define OPT_PRES_PENALTY 261
#define OPT_BATCH_FILE 262
#define OPT_BATCH_CONCURRENT 263
#define OPT_SEED 264
#define OPT_CONTEXT_WINDOW 265
#define OPT_SANDBOX 266

static struct option long_opts[] = {
    {"provider", required_argument, NULL, 'P'},
    {"base-url", required_argument, NULL, 'b'},
    {"model", required_argument, NULL, 'm'},
    {"key", required_argument, NULL, 'k'},
    {"prompt", required_argument, NULL, 'p'},
    {"system-prompt", required_argument, NULL, 's'},
    {"max-tokens", required_argument, NULL, 't'},
    {"resume", required_argument, NULL, 'r'},
    {"continue", no_argument, NULL, 'c'},
    {"verbose", no_argument, NULL, 'v'},
    {"no-markdown", no_argument, NULL, 'M'},
    {"json", no_argument, NULL, 'j'},
    {"output", required_argument, NULL, 'o'},
    {"effort", required_argument, NULL, 'e'},
    {"max-turns", required_argument, NULL, 'T'},
    {"cwd", required_argument, NULL, 'd'},
    {"append-system-prompt-file", required_argument, NULL, 'f'},
    {"append-system-prompt", required_argument, NULL, 'A'},
    {"dangerously-skip-permissions", no_argument, NULL, 'D'},
    {"approval", required_argument, NULL, 'a'},
    {"schema", required_argument, NULL, 'S'},
    {"profile", required_argument, NULL, 'F'},
    {"text-verbosity", required_argument, NULL, OPT_TEXT_VERBOSITY},
    {"reasoning-summary", required_argument, NULL, OPT_REASONING_SUMMARY},
    {"temperature", required_argument, NULL, OPT_TEMPERATURE},
    {"top-p", required_argument, NULL, OPT_TOP_P},
    {"frequency-penalty", required_argument, NULL, OPT_FREQ_PENALTY},
    {"presence-penalty", required_argument, NULL, OPT_PRES_PENALTY},
    {"batch", required_argument, NULL, OPT_BATCH_FILE},
    {"batch-concurrent", required_argument, NULL, OPT_BATCH_CONCURRENT},
    {"seed", required_argument, NULL, OPT_SEED},
    {"context-window", required_argument, NULL, OPT_CONTEXT_WINDOW},
    {"sandbox", no_argument, NULL, OPT_SANDBOX},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'V'},
    {NULL, 0, NULL, 0}};

static void usage(const char *prog) {
    fprintf(
        stderr,
        "Usage: %s [OPTIONS]\n\n"
        "  -P, --provider <compat|native>     API provider (default: %s)\n"
        "  -b, --base-url <URL>               Custom base URL (auto-normalizes /v1)\n"
        "  -m, --model <name>                 Model name (default: %s)\n"
        "  -k, --key <api-key>                API key (overrides env vars)\n"
        "  -p, --prompt <text>                One-shot prompt (non-interactive)\n"
        "  -s, --system-prompt <text>         Custom system prompt\n"
        "  -f, --append-system-prompt-file <path>  Append file contents (after config/env files)\n"
        "  -A, --append-system-prompt <text>  Append text to system prompt (last)\n"
        "  -t, --max-tokens <N>               Max output tokens (default: %d)\n"
        "  -T, --max-turns <N>                Max agent loop turns (default: %d)\n"
        "  -e, --effort <level>               Reasoning effort: "
        "none|minimal|low|medium|high|xhigh|max\n"
        "      --text-verbosity <level>       Text verbosity: low|medium|high (GPT-5.x)\n"
        "      --reasoning-summary <mode>     Reasoning summary: auto|detailed\n"
        "      --temperature <0.0-2.0>        Sampling temperature (-1 = API default)\n"
        "      --top-p <0.0-1.0>             Nucleus sampling (-1 = API default)\n"
        "      --frequency-penalty <0-2>      Frequency penalty (default: 0)\n"
        "      --presence-penalty <0-2>       Presence penalty (default: 0)\n"
        "      --seed <N>                     Fixed RNG seed (-1 = API default)\n"
        "      --context-window <N>           Override model context window size\n"
        "      --batch <file.jsonl>           Batch mode: process prompts from file\n"
        "      --batch-concurrent <N>         Batch parallelism (default: %d)\n"
        "  -r, --resume <session-id>          Resume a previous session\n"
        "  -c, --continue                     Resume most recent session\n"
        "  -F, --profile <name>               Load config profile from ~/.agents/profiles/\n"
        "  -j, --json                         Output final result as JSON\n"
        "  -S, --schema <file>                JSON schema for structured output\n"
        "  -o, --output <path>                Write final result to file\n"
        "  -a, --approval <preset>            Approval preset: read-only|default|full-access\n"
        "  -D, --dangerously-skip-permissions Skip tool permission prompts\n"
        "  -d, --cwd <dir>                    Set working directory\n"
        "  -v, --verbose                      Verbose output\n"
        "  -M, --no-markdown                  Disable markdown rendering\n"
        "      --sandbox                      Enable sandbox enforcement (path + OS-level)\n"
        "  -h, --help                         Show help\n"
        "  -V, --version                      Show version\n",
        prog, nc_provider_slug(DEFAULT_PROVIDER), nc_provider_default_model(DEFAULT_PROVIDER),
        DEFAULT_MAX_TOKENS, DEFAULT_MAX_TURNS, BATCH_MAX_CONCURRENT);
}

int config_parse(config_t *cfg, int argc, char **argv) {
    load_config_file(cfg);
    load_env(cfg);

    int c;
    optind = 1;
    while ((c = getopt_long(argc, argv, "P:b:m:k:p:s:t:r:o:e:T:d:f:A:a:S:F:cvMjDhV", long_opts,
                            NULL)) != -1) {
        switch (c) {
        case 'P':
            if (!nc_provider_parse(optarg, &cfg->provider)) {
                fprintf(stderr, "Unknown provider: %s\n", optarg);
                return -1;
            }
            break;
        case 'b':
            xfree(&cfg->compat_base_url);
            cfg->compat_base_url = normalize_compat_url(optarg);
            if (cfg->provider == PROVIDER_NATIVE)
                cfg->provider = PROVIDER_COMPAT;
            break;
        case 'm':
            xfree(&cfg->model);
            cfg->model = xstrdup(optarg);
            break;
        case 'k':
            xfree(&cfg->compat_api_key);
            cfg->compat_api_key = xstrdup(optarg);
            xfree(&cfg->native_api_key);
            cfg->native_api_key = xstrdup(optarg);
            break;
        case 'p':
            xfree(&cfg->initial_prompt);
            cfg->initial_prompt = xstrdup(optarg);
            break;
        case 't':
            cfg->max_tokens = nc_safe_atoi(optarg, DEFAULT_MAX_TOKENS);
            if (cfg->max_tokens <= 0)
                cfg->max_tokens = DEFAULT_MAX_TOKENS;
            break;
        case 's':
            xfree(&cfg->system_prompt_override);
            cfg->system_prompt_override = xstrdup(optarg);
            break;
        case 'r':
            xfree(&cfg->resume_session);
            cfg->resume_session = xstrdup(optarg);
            break;
        case 'c':
            cfg->continue_last = true;
            break;
        case 'v':
            cfg->verbose = true;
            break;
        case 'M':
            cfg->no_markdown = true;
            break;
        case 'j':
            cfg->json_output = true;
            break;
        case 'o':
            xfree(&cfg->output_file);
            cfg->output_file = xstrdup(optarg);
            break;
        case 'e': {
            effort_level_t elv = effort_level_parse(optarg);
            if (elv == EFFORT_DEFAULT && strcasecmp(optarg, "default") != 0 &&
                strcasecmp(optarg, "d") != 0) {
                fprintf(stderr, "Unknown effort: %s (use none/minimal/low/medium/high/xhigh/max)\n",
                        optarg);
                return -1;
            }
            cfg->effort = elv;
            break;
        }
        case 'T':
            cfg->max_turns = nc_safe_atoi(optarg, DEFAULT_MAX_TURNS);
            if (cfg->max_turns <= 0)
                cfg->max_turns = DEFAULT_MAX_TURNS;
            break;
        case 'd':
            xfree(&cfg->cwd);
            cfg->cwd = xstrdup(optarg);
            break;
        case 'f':
            xfree(&cfg->append_system_prompt_file_cli);
            cfg->append_system_prompt_file_cli = xstrdup(optarg);
            break;
        case 'A':
            xfree(&cfg->append_prompt);
            cfg->append_prompt = xstrdup(optarg);
            break;
        case 'S':
            xfree(&cfg->output_schema);
            cfg->output_schema = xstrdup(optarg);
            break;
        case 'D':
            cfg->trust_all_tools = true;
            break;
        case 'a': {
            int ap = perm_parse_approval(optarg);
            if (ap < 0) {
                fprintf(stderr, "Unknown approval preset: %s (use read-only/default/full-access)\n",
                        optarg);
                return -1;
            }
            cfg->approval_preset = ap;
            break;
        }
        case 'F': {
            char profile_path[NC_PATH_MAX];
            snprintf(profile_path, sizeof(profile_path), "profiles/%s.json", optarg);
            char resolved[NC_PATH_MAX];
            if (config_find_path(resolved, sizeof(resolved), profile_path)) {
                char *pbuf = strbuf_read_file(resolved, NULL);
                if (pbuf) {
                    cJSON *proot = cJSON_Parse(pbuf);
                    if (proot) {
                        cJSON *pm = cJSON_GetObjectItem(proot, "model");
                        if (pm && cJSON_IsString(pm)) {
                            xfree(&cfg->model);
                            cfg->model = xstrdup(pm->valuestring);
                        }
                        cJSON *pb = cJSON_GetObjectItem(proot, "base_url");
                        if (pb && cJSON_IsString(pb)) {
                            xfree(&cfg->compat_base_url);
                            cfg->compat_base_url = normalize_compat_url(pb->valuestring);
                            cfg->provider = PROVIDER_COMPAT;
                        }
                        cJSON *pk = cJSON_GetObjectItem(proot, "api_key");
                        if (pk && cJSON_IsString(pk)) {
                            xfree(&cfg->compat_api_key);
                            cfg->compat_api_key = xstrdup(pk->valuestring);
                            xfree(&cfg->native_api_key);
                            cfg->native_api_key = xstrdup(pk->valuestring);
                        }
                        cJSON *pp = cJSON_GetObjectItem(proot, "provider");
                        if (pp && cJSON_IsString(pp))
                            nc_provider_parse(pp->valuestring, &cfg->provider);
                        cJSON *pi = cJSON_GetObjectItem(proot, "custom_instructions");
                        if (pi && cJSON_IsString(pi)) {
                            xfree(&cfg->custom_instructions);
                            cfg->custom_instructions = xstrdup(pi->valuestring);
                        }
                        cfg->max_tokens = nc_cjson_int(proot, "max_tokens", cfg->max_tokens);
                        const char *pef_str = nc_cjson_str(proot, "effort", NULL);
                        if (!pef_str)
                            pef_str = nc_cjson_str(proot, "default_effort", NULL);
                        if (pef_str)
                            cfg->effort = effort_level_parse(pef_str);
                        cfg->temperature = nc_cjson_dbl(proot, "temperature", cfg->temperature);
                        cfg->top_p = nc_cjson_dbl(proot, "top_p", cfg->top_p);
                        cfg->seed = nc_cjson_int(proot, "seed", cfg->seed);
                        cfg->context_window =
                            nc_cjson_int(proot, "context_window", cfg->context_window);
                        const char *ptv = nc_cjson_str(proot, "text_verbosity", NULL);
                        if (ptv) {
                            xfree(&cfg->text_verbosity);
                            cfg->text_verbosity = xstrdup(ptv);
                        }
                        const char *prs = nc_cjson_str(proot, "reasoning_summary", NULL);
                        if (prs) {
                            xfree(&cfg->reasoning_summary);
                            cfg->reasoning_summary = xstrdup(prs);
                        }
                        cJSON_Delete(proot);
                    }
                    free(pbuf);
                }
            } else {
                fprintf(stderr, "Profile not found: %s\n", optarg);
                return -1;
            }
            break;
        }
        case OPT_TEXT_VERBOSITY:
            xfree(&cfg->text_verbosity);
            if (strcasecmp(optarg, "low") == 0 || strcasecmp(optarg, "medium") == 0 ||
                strcasecmp(optarg, "high") == 0)
                cfg->text_verbosity = xstrdup(optarg);
            else {
                fprintf(stderr, "Unknown text-verbosity: %s (use low/medium/high)\n", optarg);
                return -1;
            }
            break;
        case OPT_REASONING_SUMMARY:
            xfree(&cfg->reasoning_summary);
            if (strcasecmp(optarg, "auto") == 0 || strcasecmp(optarg, "detailed") == 0)
                cfg->reasoning_summary = xstrdup(optarg);
            else {
                fprintf(stderr, "Unknown reasoning-summary: %s (use auto/detailed)\n", optarg);
                return -1;
            }
            break;
        case OPT_TEMPERATURE: {
            double v = nc_safe_atof(optarg, SAMPLING_TEMPERATURE_DEFAULT);
            cfg->temperature = (v < 0)                          ? SAMPLING_TEMPERATURE_DEFAULT
                               : (v > SAMPLING_TEMPERATURE_MAX) ? SAMPLING_TEMPERATURE_MAX
                                                                : v;
            break;
        }
        case OPT_TOP_P: {
            double v = nc_safe_atof(optarg, SAMPLING_TOP_P_DEFAULT);
            cfg->top_p = (v < 0)                    ? SAMPLING_TOP_P_DEFAULT
                         : (v > SAMPLING_TOP_P_MAX) ? SAMPLING_TOP_P_MAX
                                                    : v;
            break;
        }
        case OPT_FREQ_PENALTY: {
            double v = nc_safe_atof(optarg, 0.0);
            cfg->frequency_penalty = (v < -SAMPLING_PENALTY_MAX)  ? -SAMPLING_PENALTY_MAX
                                     : (v > SAMPLING_PENALTY_MAX) ? SAMPLING_PENALTY_MAX
                                                                  : v;
            break;
        }
        case OPT_PRES_PENALTY: {
            double v = nc_safe_atof(optarg, 0.0);
            cfg->presence_penalty = (v < -SAMPLING_PENALTY_MAX)  ? -SAMPLING_PENALTY_MAX
                                    : (v > SAMPLING_PENALTY_MAX) ? SAMPLING_PENALTY_MAX
                                                                 : v;
            break;
        }
        case OPT_BATCH_FILE:
            xfree(&cfg->batch_file);
            cfg->batch_file = xstrdup(optarg);
            break;
        case OPT_BATCH_CONCURRENT:
            cfg->batch_concurrent = nc_safe_atoi(optarg, BATCH_MAX_CONCURRENT);
            if (cfg->batch_concurrent < 1)
                cfg->batch_concurrent = 1;
            if (cfg->batch_concurrent > BATCH_MAX_CONCURRENT)
                cfg->batch_concurrent = BATCH_MAX_CONCURRENT;
            break;
        case OPT_SEED:
            cfg->seed = nc_safe_atoi(optarg, SAMPLING_SEED_DEFAULT);
            break;
        case OPT_CONTEXT_WINDOW:
            cfg->context_window = nc_safe_atoi(optarg, 0);
            break;
        case OPT_SANDBOX:
            cfg->sandbox = true;
            break;
        case 'V':
            printf("%s\n", NC_VERSION_DISPLAY);
            exit(0);
        case 'h':
            usage(argv[0]);
            exit(0);
        default:
            usage(argv[0]);
            return -1;
        }
    }

    config_finalize_append_prompt(cfg);

    if (!cfg->native_base_url)
        cfg->native_base_url = nc_xstrdup(DEFAULT_NATIVE_URL);

    /* auto-detect: if only compat-API key is set, switch provider automatically */
    if (cfg->provider == PROVIDER_NATIVE && !cfg->native_api_key && cfg->compat_api_key &&
        cfg->compat_base_url) {
        cfg->provider = PROVIDER_COMPAT;
    }

    if (cfg->provider == PROVIDER_NATIVE && !cfg->native_api_key) {
        fprintf(stderr, "Error: native API key not set\n\n"
                        "Set it via:\n"
                        "  export " ENV_NATIVE_KEY "='sk-...'\n"
                        "  or add to ~/.agents/config.json\n");
        return -1;
    }
    if (cfg->provider == PROVIDER_COMPAT && !cfg->compat_api_key) {
        fprintf(stderr, "Error: compat API key not set\n\n"
                        "Set it via:\n"
                        "  export " ENV_COMPAT_KEY "='sk-...'\n"
                        "  or add to ~/.agents/config.json\n");
        return -1;
    }
    if (cfg->provider == PROVIDER_COMPAT && !cfg->compat_base_url) {
        fprintf(stderr, "Error: base URL required for compat provider\n\n"
                        "Set it via:\n"
                        "  --base-url https://your-gateway.example\n"
                        "  export " ENV_COMPAT_URL "='https://...'\n"
                        "  or set base_url in ~/.agents/config.json\n");
        return -1;
    }
    model_presets_set_provider(cfg->provider);
    return 0;
}

void config_free(config_t *cfg) {
    xfree(&cfg->native_api_key);
    xfree(&cfg->native_base_url);
    xfree(&cfg->compat_api_key);
    xfree(&cfg->compat_base_url);
    xfree(&cfg->model);
    xfree(&cfg->initial_prompt);
    xfree(&cfg->system_prompt_override);
    xfree(&cfg->resume_session);
    xfree(&cfg->output_file);
    xfree(&cfg->output_schema);
    xfree(&cfg->custom_instructions);
    xfree(&cfg->cwd);
    xfree(&cfg->append_prompt);
    xfree(&cfg->append_system_prompt_file);
    xfree(&cfg->append_system_prompt_file_cli);
    xfree(&cfg->text_verbosity);
    xfree(&cfg->reasoning_summary);
    xfree(&cfg->batch_file);
    for (int i = 0; i < cfg->num_aliases; i++) {
        xfree(&cfg->aliases[i].alias);
        xfree(&cfg->aliases[i].model);
    }
    cfg->num_aliases = 0;
    for (int i = 0; i < cfg->num_stop_sequences; i++)
        xfree(&cfg->stop_sequences[i]);
    cfg->num_stop_sequences = 0;
}

const char *config_api_key(const config_t *cfg) {
    return cfg->provider == PROVIDER_NATIVE ? cfg->native_api_key : cfg->compat_api_key;
}

const char *config_base_url(const config_t *cfg) {
    return cfg->provider == PROVIDER_NATIVE ? cfg->native_base_url : cfg->compat_base_url;
}

const char *config_resolve_alias(const config_t *cfg, const char *name) {
    if (!name)
        return NULL;
    for (int i = 0; i < cfg->num_aliases; i++) {
        if (strcmp(cfg->aliases[i].alias, name) == 0)
            return cfg->aliases[i].model;
    }
    return name;
}

const char *config_model(const config_t *cfg) {
    if (!cfg)
        return nc_provider_default_model(DEFAULT_PROVIDER);
    if (cfg->model)
        return config_resolve_alias(cfg, cfg->model);
    return nc_provider_default_model(cfg->provider);
}
