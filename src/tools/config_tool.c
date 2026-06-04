#include "tools.h"
#include "repl.h"
#include "defaults.h"

static const char *SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"action\":{\"type\":\"string\",\"enum\":[\"get\",\"set\",\"list\"],"
    "\"description\":\"Action: get a setting, set a setting, or list all\"},"
    "\"key\":{\"type\":\"string\",\"description\":"
    "\"Setting key: model, provider, effort, text_verbosity, reasoning_summary, max_turns, "
    "temperature, top_p, seed, context_window, verbose, brief, trust_all\"},"
    "\"value\":{\"type\":\"string\",\"description\":\"New value for the setting\"}"
    "},"
    "\"required\":[\"action\"]"
    "}";

static const char *valid_keys[] = {
    "model",       "provider", "effort", "text_verbosity", "reasoning_summary", "max_turns",
    "temperature", "top_p",    "seed",   "context_window", "verbose",           "brief",
    "trust_all",   NULL};

static int is_valid_key(const char *key) {
    for (int i = 0; valid_keys[i]; i++)
        if (strcmp(key, valid_keys[i]) == 0)
            return 1;
    return 0;
}

static int config_tool_exec(const cJSON *input, char **output, void *ctx) {
    const char *action = nc_cjson_str((cJSON *)input, "action", NULL);
    if (!action) {
        *output = nc_xstrdup("error: missing 'action'");
        return 1;
    }

    config_t *cfg = g_cfg_ptr;
    if (!cfg) {
        *output = nc_xstrdup("error: config not available");
        return 1;
    }

    if (strcmp(action, "list") == 0) {
        strbuf_t sb;
        strbuf_init(&sb);
        strbuf_append_str(&sb, "Current settings:\n");

        char line[NC_LABEL_MAX];
        snprintf(line, sizeof(line), "  model:              %s\n", config_model(cfg));
        strbuf_append_str(&sb, line);
        snprintf(line, sizeof(line), "  provider:           %s\n", nc_provider_slug(cfg->provider));
        strbuf_append_str(&sb, line);

        snprintf(line, sizeof(line), "  effort:             %s\n", effort_level_name(cfg->effort));
        strbuf_append_str(&sb, line);

        snprintf(line, sizeof(line), "  text_verbosity:     %s\n",
                 cfg->text_verbosity ? cfg->text_verbosity : "(auto)");
        strbuf_append_str(&sb, line);
        snprintf(line, sizeof(line), "  reasoning_summary:  %s\n",
                 cfg->reasoning_summary ? cfg->reasoning_summary : "(auto)");
        strbuf_append_str(&sb, line);

        int mt = cfg->max_turns > 0 ? cfg->max_turns : DEFAULT_MAX_TURNS;
        snprintf(line, sizeof(line), "  max_turns:          %d\n", mt);
        strbuf_append_str(&sb, line);
        snprintf(line, sizeof(line), "  verbose:            %s\n", cfg->verbose ? "true" : "false");
        strbuf_append_str(&sb, line);
        snprintf(line, sizeof(line), "  trust_all:          %s\n",
                 cfg->trust_all_tools ? "true" : "false");
        strbuf_append_str(&sb, line);
        snprintf(line, sizeof(line), "  temperature:        %s\n",
                 cfg->temperature >= 0 ? "custom" : "(api default)");
        strbuf_append_str(&sb, line);
        if (cfg->temperature >= 0) {
            snprintf(line, sizeof(line), "                      %.2f\n", cfg->temperature);
            strbuf_append_str(&sb, line);
        }
        snprintf(line, sizeof(line), "  top_p:              %s\n",
                 cfg->top_p >= 0 ? "custom" : "(api default)");
        strbuf_append_str(&sb, line);
        snprintf(line, sizeof(line), "  seed:               %s\n",
                 cfg->seed >= 0 ? "set" : "(random)");
        strbuf_append_str(&sb, line);
        if (cfg->seed >= 0) {
            snprintf(line, sizeof(line), "                      %d\n", cfg->seed);
            strbuf_append_str(&sb, line);
        }
        snprintf(line, sizeof(line), "  context_window:     %s\n",
                 cfg->context_window > 0 ? "override" : "(auto)");
        strbuf_append_str(&sb, line);
        if (cfg->context_window > 0) {
            snprintf(line, sizeof(line), "                      %d\n", cfg->context_window);
            strbuf_append_str(&sb, line);
        }

        *output = strbuf_detach(&sb);
        return 0;
    }

    const char *key = nc_cjson_str((cJSON *)input, "key", NULL);

    if (strcmp(action, "get") == 0) {
        if (!key) {
            *output = nc_xstrdup("error: missing 'key'");
            return 1;
        }
        if (!is_valid_key(key)) {
            char msg[NC_LABEL_MAX];
            snprintf(msg, sizeof(msg), "error: unknown key '%s'", key);
            *output = nc_xstrdup(msg);
            return 1;
        }

        char buf[NC_LABEL_MAX];
        if (strcmp(key, "model") == 0)
            nc_strlcpy(buf, config_model(cfg), sizeof(buf));
        else if (strcmp(key, "provider") == 0)
            nc_strlcpy(buf, nc_provider_slug(cfg->provider), sizeof(buf));
        else if (strcmp(key, "effort") == 0)
            nc_strlcpy(buf, effort_level_name(cfg->effort), sizeof(buf));
        else if (strcmp(key, "text_verbosity") == 0)
            nc_strlcpy(buf, cfg->text_verbosity ? cfg->text_verbosity : "(auto)", sizeof(buf));
        else if (strcmp(key, "reasoning_summary") == 0)
            nc_strlcpy(buf, cfg->reasoning_summary ? cfg->reasoning_summary : "(auto)",
                       sizeof(buf));
        else if (strcmp(key, "max_turns") == 0)
            snprintf(buf, sizeof(buf), "%d",
                     cfg->max_turns > 0 ? cfg->max_turns : DEFAULT_MAX_TURNS);
        else if (strcmp(key, "verbose") == 0)
            nc_strlcpy(buf, cfg->verbose ? "true" : "false", sizeof(buf));
        else if (strcmp(key, "brief") == 0)
            snprintf(buf, sizeof(buf), "false");
        else if (strcmp(key, "trust_all") == 0)
            nc_strlcpy(buf, cfg->trust_all_tools ? "true" : "false", sizeof(buf));
        else if (strcmp(key, "temperature") == 0)
            snprintf(buf, sizeof(buf), "%.2f", cfg->temperature);
        else if (strcmp(key, "top_p") == 0)
            snprintf(buf, sizeof(buf), "%.2f", cfg->top_p);
        else if (strcmp(key, "seed") == 0)
            snprintf(buf, sizeof(buf), "%d", cfg->seed);
        else if (strcmp(key, "context_window") == 0)
            snprintf(buf, sizeof(buf), "%d", cfg->context_window);
        else
            snprintf(buf, sizeof(buf), "(unknown)");

        *output = nc_xstrdup(buf);
        return 0;
    }

    if (strcmp(action, "set") == 0) {
        if (!key) {
            *output = nc_xstrdup("error: missing 'key'");
            return 1;
        }
        const char *value = nc_cjson_str((cJSON *)input, "value", NULL);
        if (!value) {
            *output = nc_xstrdup("error: missing 'value'");
            return 1;
        }
        if (!is_valid_key(key)) {
            char msg[NC_LABEL_MAX];
            snprintf(msg, sizeof(msg), "error: unknown key '%s'", key);
            *output = nc_xstrdup(msg);
            return 1;
        }

        char msg[NC_LABEL_MAX];
        if (strcmp(key, "model") == 0) {
            free(cfg->model);
            cfg->model = nc_xstrdup(value);
            rollout_record_settings("model", value);
            snprintf(msg, sizeof(msg), "Model changed to: %s", value);
        } else if (strcmp(key, "provider") == 0) {
            if (!nc_provider_parse(value, &cfg->provider)) {
                snprintf(msg, sizeof(msg),
                         "error: provider must be 'compat' or 'native' "
                         "(provider aliases are also accepted)");
                *output = nc_xstrdup(msg);
                return 1;
            }
            model_presets_set_provider(cfg->provider);
            rollout_record_settings("provider", nc_provider_slug(cfg->provider));
            snprintf(msg, sizeof(msg), "Provider changed to: %s", nc_provider_slug(cfg->provider));
        } else if (strcmp(key, "effort") == 0) {
            effort_level_t lv = effort_level_parse(value);
            if (lv == EFFORT_DEFAULT && strcmp(value, "default") != 0) {
                *output = nc_xstrdup(
                    "error: effort must be none/minimal/low/medium/high/xhigh/max/default");
                return 1;
            }
            cfg->effort = lv;
            effort_level_set(lv);
            rollout_record_settings("effort", value);
            snprintf(msg, sizeof(msg), "Effort changed to: %s", effort_level_name(lv));
        } else if (strcmp(key, "text_verbosity") == 0) {
            free(cfg->text_verbosity);
            if (strcmp(value, "off") == 0 || strcmp(value, "auto") == 0 ||
                strcmp(value, "none") == 0)
                cfg->text_verbosity = NULL;
            else
                cfg->text_verbosity = nc_xstrdup(value);
            snprintf(msg, sizeof(msg), "Text verbosity changed to: %s",
                     cfg->text_verbosity ? cfg->text_verbosity : "(auto)");
        } else if (strcmp(key, "reasoning_summary") == 0) {
            free(cfg->reasoning_summary);
            if (strcmp(value, "off") == 0 || strcmp(value, "none") == 0)
                cfg->reasoning_summary = NULL;
            else
                cfg->reasoning_summary = nc_xstrdup(value);
            snprintf(msg, sizeof(msg), "Reasoning summary changed to: %s",
                     cfg->reasoning_summary ? cfg->reasoning_summary : "(auto)");
        } else if (strcmp(key, "max_turns") == 0) {
            int v = nc_safe_atoi(value, 0);
            if (v <= 0 || v > NC_MAX_TURNS_RUNTIME_CAP) {
                char e[NC_ERRBUF_MAX];
                snprintf(e, sizeof(e), "error: max_turns must be 1-%d", NC_MAX_TURNS_RUNTIME_CAP);
                *output = nc_xstrdup(e);
                return 1;
            }
            cfg->max_turns = v;
            snprintf(msg, sizeof(msg), "Max turns changed to: %d", v);
        } else if (strcmp(key, "verbose") == 0) {
            cfg->verbose = nc_parse_bool(value, false);
            snprintf(msg, sizeof(msg), "Verbose: %s", cfg->verbose ? "enabled" : "disabled");
        } else if (strcmp(key, "trust_all") == 0) {
            cfg->trust_all_tools = nc_parse_bool(value, false);
            snprintf(msg, sizeof(msg), "Trust all tools: %s",
                     cfg->trust_all_tools ? "enabled" : "disabled");
        } else if (strcmp(key, "temperature") == 0) {
            double tv = nc_safe_atof(value, SAMPLING_TEMPERATURE_DEFAULT);
            cfg->temperature = (tv < 0)                          ? SAMPLING_TEMPERATURE_DEFAULT
                               : (tv > SAMPLING_TEMPERATURE_MAX) ? SAMPLING_TEMPERATURE_MAX
                                                                 : tv;
            snprintf(msg, sizeof(msg), "Temperature: %.2f", cfg->temperature);
        } else if (strcmp(key, "top_p") == 0) {
            double tv = nc_safe_atof(value, SAMPLING_TOP_P_DEFAULT);
            cfg->top_p = (tv < 0)                    ? SAMPLING_TOP_P_DEFAULT
                         : (tv > SAMPLING_TOP_P_MAX) ? SAMPLING_TOP_P_MAX
                                                     : tv;
            snprintf(msg, sizeof(msg), "Top-p: %.2f", cfg->top_p);
        } else if (strcmp(key, "seed") == 0) {
            cfg->seed = nc_safe_atoi(value, SAMPLING_SEED_DEFAULT);
            snprintf(msg, sizeof(msg), "Seed: %d", cfg->seed);
        } else if (strcmp(key, "context_window") == 0) {
            cfg->context_window = nc_safe_atoi(value, 0);
            snprintf(msg, sizeof(msg), "Context window: %d", cfg->context_window);
        } else {
            snprintf(msg, sizeof(msg), "error: cannot set '%s'", key);
            *output = nc_xstrdup(msg);
            return 1;
        }

        *output = nc_xstrdup(msg);
        return 0;
    }

    *output = nc_xstrdup("error: action must be get/set/list");
    return 1;
}

static tool_def_t config_tool;

void tool_config_register(void) {
    config_tool.name = "ConfigTool";
    config_tool.description =
        "Get, set, or list runtime configuration settings. "
        "Changeable: model, provider, effort, text_verbosity, reasoning_summary, "
        "max_turns, temperature, top_p, seed, context_window, verbose, trust_all.";
    config_tool.input_schema = cJSON_Parse(SCHEMA);
    config_tool.execute = config_tool_exec;
    config_tool.read_only = false;
    config_tool.concurrency_safe = true;
    config_tool.destructive = false;
    tools_register(&config_tool);
}
