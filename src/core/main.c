/* main.c — Entry point, REPL loop, and one-shot execution. */

#include "repl.h"
#include "api.h"
#include "terminal.h"
#include "tools.h"
#include "harness.h"
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/select.h>
#include <curl/curl.h>
#ifdef USE_READLINE
#include <readline/readline.h>
#endif

/* g_mcp_servers and g_num_mcp_servers defined in repl_globals.c */

static void print_turn_info(const stream_state_t *st, int sess_in, int sess_out) {
    double turn_elapsed = term_stream_elapsed();
    strbuf_t tinfo;
    strbuf_init(&tinfo);
    strbuf_appendf(&tinfo, "[%.1fs | %d in, %d out", turn_elapsed, st->input_tokens,
                   st->output_tokens);
    if (st->cache_read_tokens > 0 || st->cache_creation_tokens > 0)
        strbuf_appendf(&tinfo, " | cache: %d read, %d created", st->cache_read_tokens,
                       st->cache_creation_tokens);
    strbuf_appendf(&tinfo, " | session: %d in, %d out]\n", sess_in, sess_out);
    term_color(T_DIM, "%s", tinfo.data);
    strbuf_free(&tinfo);
}

static int mcp_tool_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *full_name = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)input, "_tool_name"));
    if (!full_name || !nc_starts_with(full_name, "mcp_")) {
        *output = nc_xstrdup("error: internal MCP routing failure");
        return 1;
    }
    for (int i = 0; i < g_num_mcp_servers; i++) {
        char prefix[NC_LABEL_MAX];
        snprintf(prefix, sizeof(prefix), "mcp_%s_", g_mcp_servers[i].name);
        size_t plen = strlen(prefix);
        if (strncmp(full_name, prefix, plen) == 0) {
            const char *tool_name = full_name + plen;
            cJSON *clean = cJSON_Duplicate(input, 1);
            cJSON_DeleteItemFromObject(clean, "_tool_name");
            int rc = mcp_call_tool(&g_mcp_servers[i], tool_name, clean, output);
            cJSON_Delete(clean);
            return rc;
        }
    }
    *output = nc_xstrdup("error: MCP server not found");
    return 1;
}

static void load_mcp_config(void) {
    char path[NC_PATH_MAX];
    if (!config_find_path(path, sizeof(path), NC_CFG_MCP))
        return;

    char *buf = strbuf_read_file(path, NULL);
    if (!buf)
        return;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
        return;

    cJSON *servers = cJSON_GetObjectItem(root, "servers");
    int ns = cJSON_GetArraySize(servers);
    for (int i = 0; i < ns && g_num_mcp_servers < MAX_MCP_SERVERS; i++) {
        cJSON *s = cJSON_GetArrayItem(servers, i);
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(s, "name"));
        const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(s, "command"));
        if (!name || !cmd)
            continue;

        mcp_server_t *srv = &g_mcp_servers[g_num_mcp_servers];
        if (mcp_start(srv, name, cmd) != 0) {
            term_color(T_YELLOW, "[MCP] Failed to start: %s\n", name);
            continue;
        }

        int nt = mcp_discover_tools(srv);
        if (nt <= 0) {
            term_color(T_DIM, "[MCP] %s: no tools found\n", name);
            g_num_mcp_servers++;
            continue;
        }

        for (int j = 0; j < srv->num_tools; j++) {
            srv->tools[j].execute = mcp_tool_exec;
            tools_register(&srv->tools[j]);
        }
        term_color(T_DIM, "[MCP] %s: %d tools registered\n", name, nt);
        g_num_mcp_servers++;
    }
    cJSON_Delete(root);
}

/* Global state defined in repl_globals.c */

/* stream_state_t, on_stream_event — in stream_handler.c */
/* execute_pending_tools, auto_extract_memories — in tool_executor.c */
/* context_limit_for_model, auto_compact, classify_api_error — in context_manager.c */
/* handle_slash_command — in slash_commands.c */

/* Modular functions in their respective files:
   stream_handler.c, tool_executor.c, context_manager.c, slash_commands.c */

/* ---- REPL ---- */

/* g_interrupted defined in repl_globals.c */
static volatile sig_atomic_t g_in_api_call = 0;

static void sigint_handler(int sig) {
    (void)sig;

    if (!g_in_api_call) {
        /* At the prompt: exit immediately. */
        const char msg[] = "\n";
        (void)!write(STDOUT_FILENO, msg, sizeof(msg) - 1);
        _exit(0);
    }

    /* During streaming / tool execution: second press force-exits. */
    if (g_interrupted) {
        const char msg[] = "\n[force exit]\n";
        (void)!write(STDOUT_FILENO, msg, sizeof(msg) - 1);
        _exit(NC_EXIT_SIGNAL(SIGINT));
    }

    g_interrupted = 1;
}

void nc_sigint_arm(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

#ifdef USE_READLINE
/* libedit (macOS) uses int (*)(const char *, int); GNU readline uses int (*)(void). */
#ifdef __APPLE__
static int nc_rl_sigint_rearm(const char *unused, int ign) {
    (void)unused;
    (void)ign;
    nc_sigint_arm();
    return 0;
}
#else
static int nc_rl_sigint_rearm(void) {
    nc_sigint_arm();
    return 0;
}
#endif
#endif

static int run_repl(config_t *cfg) {
    g_cfg_ptr = cfg;
    conversation_t conv;
    conversation_init(&conv);

#ifdef USE_READLINE
    rl_startup_hook = nc_rl_sigint_rearm;
#endif

    warmup_init(PREWARM_TIMEOUT_MS);
    warmup_start();

    event_dispatch_init(cfg->json_output ? OUTPUT_JSONL : OUTPUT_QUIET);

    /* Permissions + approval preset */
    perm_init(&g_perms);
    if (cfg->trust_all_tools)
        g_perms.trust_all = true;
    if (cfg->approval_preset > 0)
        perm_set_approval(&g_perms, (approval_preset_t)cfg->approval_preset);

    /* Detect project root; sandbox enforcement is opt-in via --sandbox */
    const char *proj_root = project_root_detect(NULL);
    sandbox_init(proj_root);
    if (cfg->sandbox) {
        sandbox_enable();
        os_sandbox_init();
        os_sandbox_setup_paths(proj_root);
    }
    if (proj_root) {
        warmup_mark_workspace_scanned();
        term_color(T_DIM, "[project] %s\n", proj_root);
    }
    if (cfg->sandbox)
        term_color(T_DIM, "[sandbox] enforcing (%s)\n",
                   os_sandbox_available() ? os_sandbox_type_name() : "path-only");

    /* Initialize cost tracker, file history, tips, read tracker, streaming exec */
    cost_tracker_init();
    file_history_init();
    git_undo_init();
    {
        int tr_cleaned = tool_result_cleanup();
        if (tr_cleaned > 0)
            term_color(T_DIM, "[cleanup] %d stale tool result%s removed\n", tr_cleaned,
                       tr_cleaned > 1 ? "s" : "");
    }
    tips_init();
    turn_timing_init();
    read_file_tracker_init(&g_read_tracker);
    streaming_exec_init(streaming_exec_global());

    /* Branch tracker + model routing + recovery + token budget */
    conv_branch_init();
    model_router_init();
    conv_recovery_init();
    token_budget_init(0);

    /* Multi-provider fallback: load from env + config */
    multi_provider_init();
    {
        int nfb = multi_provider_load_from_env();
        char fb_cfg_path[NC_PATH_MAX];
        if (config_find_path(fb_cfg_path, sizeof(fb_cfg_path), NC_CFG_CONFIG)) {
            char *fbuf = strbuf_read_file(fb_cfg_path, NULL);
            if (fbuf) {
                cJSON *froot = cJSON_Parse(fbuf);
                if (froot) {
                    nfb += multi_provider_load_from_config(froot);
                    cJSON_Delete(froot);
                }
                free(fbuf);
            }
        }
        if (nfb > 0)
            term_color(T_DIM, "[fallback] %d provider%s configured\n", nfb, nfb > 1 ? "s" : "");
    }

    /* Session statistics: load cumulative stats */
    session_stats_init();
    session_stats_load();

    /* Session scratch directory (per-session temp workspace) */
    session_scratch_init(NULL);

    /* Idle guard: auto-exit for daemon/server mode */
    {
        const char *idle_env = getenv(IDLE_GUARD_ENV_VAR);
        int idle_ms = idle_env ? nc_safe_atoi(idle_env, 0) : IDLE_GUARD_DEFAULT_MS;
        idle_guard_init(idle_ms);
    }
    if (idle_guard_enabled())
        idle_guard_start();

    /* Session memory + Away summary + prompt suggestion + effort level */
    session_memory_init();
    away_summary_init();
    prompt_suggest_init(true);
    effort_level_init();

    /* Shell detection + approval cache + commit attribution */
    shell_detect_init();
    approval_cache_init();
    commit_attr_init(NULL);

    /* Load network policy */
    {
        char np_path[NC_PATH_MAX];
        config_find_path(np_path, sizeof(np_path), NC_CFG_NETWORK);
        network_policy_t *np = network_policy_global();
        network_policy_init(np);
        int nr = network_policy_load(np, np_path);
        if (nr > 0)
            term_color(T_DIM, "[network] %d rule(s) loaded\n", nr);
    }

    /* Load exec policy rules */
    {
        exec_rules_ctx_t *er = exec_rules_global();
        exec_rules_init(er);
        int nr = exec_rules_load_from_config(er);
        if (nr > 0)
            term_color(T_DIM, "[exec-policy] %d rule(s) loaded\n", nr);
    }

    warmup_mark_config_loaded();

    /* Load custom skills from config directories */
    {
        char skill_dir[NC_PATH_MAX];
        config_find_path(skill_dir, sizeof(skill_dir), "skills");
        int ns = custom_skills_load(skill_dir);
        if (ns > 0)
            term_color(T_DIM, "[skills] %d custom skill%s loaded\n", ns, ns > 1 ? "s" : "");
    }

    /* File watcher: monitor config files for hot-reload */
    {
        file_watcher_t *fw = file_watcher_global();
        char cwd[NC_PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) {
            int nw = file_watcher_add_config_paths(fw, cwd);
            if (nw > 0) {
                file_watcher_add_callback(fw, hot_reload_watcher_cb, NULL);
                file_watcher_start(fw);
                if (file_watcher_active(fw))
                    term_color(T_DIM, "[watcher] monitoring %d config path%s\n", nw,
                               nw > 1 ? "s" : "");
            }
        }
    }

    /* Long-running harness: checkpoints, handoff, stall detection */
    harness_init(harness_global());
    stall_detector_init();
    context_fatigue_init(context_limit_for_model(config_model(cfg)));
    failure_router_init();
    tool_verify_init();
    progress_tracker_init();
    progress_tracker_load();
    hypothesis_debug_init();
    episode_memory_init();
    episode_memory_load();

    /* Initializer agent: scaffold new projects on first run */
    {
        char ia_cwd[NC_PATH_MAX];
        if (getcwd(ia_cwd, sizeof(ia_cwd)) && init_agent_needed(ia_cwd)) {
            int ia_recipe = -1;
            int ia_rc = init_agent_run(ia_cwd, &ia_recipe);
            if (ia_rc > 0)
                term_color(T_DIM, "[init] project scaffolded (%s)\n",
                           init_agent_type_name(ia_recipe));
        }
    }

    /* Coordinator mode detection */
    coordinator_init();
    if (coordinator_active())
        term_color(T_BOLD, "[coordinator] mode active — delegating to workers\n");

    plugin_inject_init();
    session_index_init();
    mem_phase_init();
    interrupt_ctx_init();
    autofix_init();

    /* Subsystem inits: event bus, process mgr, protocol, transcript */
    event_bus_init();
    pm_init();
    proto_init();
    transcript_init();
    abort_handler_init();
    post_compact_init();
    compact_tpl_init();
    auto_compact_init(context_limit_for_model(config_model(cfg)));
    {
        int ilim = model_input_limit(config_model(cfg));
        if (ilim > 0)
            auto_compact_set_input_limit(ilim);
    }
    scratchpad_init();
    env_ctx_init();
    thread_state_init();
    agent_health_init();
    agent_summary_init();
    output_style_init();
    palette_init();
    sbar_init();
    sbar_set_model(config_model(cfg));
    conv_rewind_init();
    keybindings_init();
    vim_mode_init();
    turn_meta_init();
    turn_diff_init();
    tool_call_rt_init();
    state_db_init();
    ff_init();
    provider_auth_init();
    realtime_ctx_init();

    /* Register tools first so the system prompt can list them */
    tools_register_all();
    psec_init();
    oa_init();
    vtrig_init();
    tool_router_init();
    exec_policy_init();
    process_state_init();
    delegate_init();
    load_mcp_config();
    tool_router_add_mcp_prefix("mcp_");
    warmup_set_tools(tools_count());

    {
        int np = plugin_discover_home_plugin_count();
        if (np > 0)
            term_color(T_DIM, "[plugins] %d discoverable manifest(s) in home plugins/marketplace\n",
                       np);
    }

    /* Load hooks + memory from config directories */
    hooks_init(&g_hooks);
    memory_init(&g_memory);
    {
        char hooks_path[NC_PATH_MAX];
        config_find_path(hooks_path, sizeof(hooks_path), NC_CFG_HOOKS);
        int nh = hooks_load(&g_hooks, hooks_path);
        if (nh > 0)
            term_color(T_DIM, "[hooks] %d hook(s) loaded\n", nh);

        char mem_path[NC_PATH_MAX];
        config_find_path(mem_path, sizeof(mem_path), NC_CFG_MEMORY);
        int nm = memory_load(&g_memory, mem_path);
        if (nm > 0)
            term_color(T_DIM, "[memory] %d memor%s loaded\n", nm, nm > 1 ? "ies" : "y");
    }

    /* Build system prompt with tool descriptions + AGENTS.md */
    if (cfg->system_prompt_override) {
        strbuf_t sp_ov;
        strbuf_init(&sp_ov);
        strbuf_append_str(&sp_ov, cfg->system_prompt_override);
        if (cfg->custom_instructions) {
            strbuf_append_str(&sp_ov, "\n\n# Custom Instructions\n\n");
            strbuf_append_str(&sp_ov, cfg->custom_instructions);
        }
        if (cfg->append_prompt) {
            strbuf_append_str(&sp_ov, "\n\n");
            strbuf_append_str(&sp_ov, cfg->append_prompt);
        }
        if (cfg->output_schema) {
            size_t schema_len = 0;
            char *sbuf = strbuf_read_file(cfg->output_schema, &schema_len);
            if (sbuf && schema_len < SCHEMA_MAX_SIZE) {
                cJSON *schema = cJSON_Parse(sbuf);
                if (schema) {
                    char *injected = structured_output_inject_prompt(sp_ov.data, schema);
                    if (injected) {
                        strbuf_clear(&sp_ov);
                        strbuf_append_str(&sp_ov, injected);
                        free(injected);
                    }
                    cJSON_Delete(schema);
                }
            }
            free(sbuf);
        }
        prompt_append_model_extras(&sp_ov, cfg);
        conv.system_prompt = strbuf_detach(&sp_ov);
    } else {
        strbuf_t sp;
        strbuf_init(&sp);

        /* Coordinator mode uses its own system prompt */
        if (coordinator_active()) {
            strbuf_append_str(&sp, coordinator_system_prompt());
        } else {
            char *base_prompt = build_system_prompt();
            strbuf_append_str(&sp, base_prompt);
            free(base_prompt);
        }

        char *agents_md = agents_md_load();
        if (agents_md) {
            warmup_mark_project_docs();
            strbuf_append_str(&sp, "\n\n---\n\n");
            strbuf_append_str(&sp, agents_md);
            free(agents_md);
        }
        char *mem_prompt = memory_format_for_prompt(&g_memory);
        if (mem_prompt) {
            strbuf_append_str(&sp, "\n\n---\n\n");
            strbuf_append_str(&sp, mem_prompt);
            free(mem_prompt);
        }
        /* Inject session preflight context (git branch, dirty count, features, progress) */
        {
            preflight_report_t pf_for_ctx = session_preflight_run();
            char *pfctx = session_preflight_context(&pf_for_ctx);
            if (pfctx && pfctx[0]) {
                strbuf_append_str(&sp, "\n\n");
                strbuf_append_str(&sp, pfctx);
            }
            free(pfctx);
        }
        /* Inject progress context if project has a progress log */
        {
            char *pctx = progress_tracker_context();
            if (pctx) {
                strbuf_append_str(&sp, "\n\n");
                strbuf_append_str(&sp, pctx);
                free(pctx);
            }
        }
        /* Inject compact repo map for structural awareness */
        {
            char rmap_cwd[NC_PATH_MAX];
            if (getcwd(rmap_cwd, sizeof(rmap_cwd))) {
                char *rmap = repo_map_generate(rmap_cwd, RMAP_BUDGET_TOKENS);
                if (rmap && strlen(rmap) > 50) {
                    strbuf_append_str(&sp, "\n\n");
                    strbuf_append_str(&sp, rmap);
                }
                free(rmap);
            }
        }
        if (cfg->custom_instructions) {
            strbuf_append_str(&sp, "\n\n# Custom Instructions\n\n");
            strbuf_append_str(&sp, cfg->custom_instructions);
        }
        if (cfg->append_prompt) {
            strbuf_append_str(&sp, "\n\n");
            strbuf_append_str(&sp, cfg->append_prompt);
        }
        /* --schema: inject JSON schema constraint */
        if (cfg->output_schema) {
            size_t schema_len = 0;
            char *sbuf = strbuf_read_file(cfg->output_schema, &schema_len);
            if (sbuf && schema_len < SCHEMA_MAX_SIZE) {
                cJSON *schema = cJSON_Parse(sbuf);
                if (schema) {
                    char *injected = structured_output_inject_prompt(sp.data, schema);
                    if (injected) {
                        strbuf_clear(&sp);
                        strbuf_append_str(&sp, injected);
                        free(injected);
                    }
                    cJSON_Delete(schema);
                }
            }
            free(sbuf);
        }
        prompt_append_model_extras(&sp, cfg);
        conv.system_prompt = strbuf_detach(&sp);
    }

    /* Session */
    char *session_id = NULL;
    if (cfg->continue_last && !cfg->resume_session) {
        session_id = history_find_latest();
        if (session_id && history_load(&conv, session_id) == 0) {
            term_color(T_DIM, "Continued session: %s (%d messages)\n", session_id, conv.len);
            history_file_snapshot_status_t hs = history_restore_file_snapshot(session_id);
            char *hs_msg = history_file_snapshot_status_message(session_id, hs);
            if (hs_msg) {
                term_color(hs == HISTORY_FILE_SNAPSHOT_RESTORED ? T_GREEN : T_YELLOW, "%s", hs_msg);
                free(hs_msg);
            }
            char *recap = generate_resume_summary(conv.msgs, conv.len, NULL);
            if (recap) {
                term_color(T_CYAN, "%s", recap);
                free(recap);
            }
        } else {
            free(session_id);
            session_id = history_gen_session_id();
            term_color(T_DIM, "(no previous session found)\n");
        }
    } else if (cfg->resume_session) {
        session_id = nc_xstrdup(cfg->resume_session);
        if (history_load(&conv, session_id) == 0) {
            term_color(T_DIM, "Resumed session: %s (%d messages)\n", session_id, conv.len);
            history_file_snapshot_status_t hs = history_restore_file_snapshot(session_id);
            char *hs_msg = history_file_snapshot_status_message(session_id, hs);
            if (hs_msg) {
                term_color(hs == HISTORY_FILE_SNAPSHOT_RESTORED ? T_GREEN : T_YELLOW, "%s", hs_msg);
                free(hs_msg);
            }
            char *recap = generate_resume_summary(conv.msgs, conv.len, NULL);
            if (recap) {
                term_color(T_CYAN, "%s", recap);
                free(recap);
            }
        }
    } else {
        session_id = history_gen_session_id();
    }

    session_memory_set_session(session_id);
    session_memory_load();
    watchdog_init(WATCHDOG_DEFAULT_DEADLINE_MS);
    rollout_tracker_init(session_id);
    rollout_record_settings("model", config_model(cfg));
    rollout_record_settings("provider", nc_provider_slug(cfg->provider));
    ghost_snapshot_init(session_id);
    checkpoint_discipline_init(session_id, HARNESS_CHECKPOINT_INTERVAL);
    doom_loop_init();
    feature_list_init();
    feature_list_load();
    session_preflight_init();
    output_eval_init();
    {
        char cwd_at[NC_PATH_MAX];
        auto_test_init(getcwd(cwd_at, sizeof(cwd_at)));
    }

    /* Session index registration */
    rollout_meta_t rollout_meta;
    {
        char cwd_buf[NC_PATH_MAX];
        const char *ws = getcwd(cwd_buf, sizeof(cwd_buf));
        session_index_add(session_id, ws ? ws : ".", config_model(cfg));
        rollout_meta_init(&rollout_meta);
        rollout_meta_set_session(&rollout_meta, session_id, config_model(cfg), ws ? ws : ".");
    }

    {
        const char *k = config_api_key(cfg);
        if (k && k[0])
            warmup_mark_api_verified();
        else
            warmup_cancel();
        (void)warmup_is_ready();
    }

    {
        dispatch_event_t ev = {.kind = EV_SESSION_START, .timestamp = nc_wall_sec()};
        event_dispatch_emit(&ev);
    }

    bool batch_mode = (cfg->batch_file != NULL);
    int batch_total = batch_mode ? batch_count() : 0;
    int batch_idx = 0;
    if (batch_mode) {
        batch_idx = batch_resume_index();
        if (batch_idx < 0 || batch_idx >= batch_total)
            batch_idx = 0;
        if (conv.len > 0) {
            conversation_clear_messages(&conv);
            if (!cfg->json_output)
                term_color(T_DIM, "[batch] cleared loaded session for isolated prompts\n");
        }
    }

    bool one_shot = !batch_mode && cfg->initial_prompt != NULL;
    bool quiet_mode = one_shot || cfg->json_output || batch_mode;

    if (!quiet_mode) {
        const char *prov_name = nc_provider_label(cfg->provider);
        term_color(T_BOLD, "%s", NC_VERSION_DISPLAY);
        printf(" (%s, %s)\n", prov_name, config_model(cfg));

        preflight_report_t pf = session_preflight_run();
        char *pf_msg = session_preflight_format(&pf);
        if (pf_msg && pf_msg[0])
            term_color(T_DIM, "%s", pf_msg);
        free(pf_msg);

        term_color(T_DIM, "Type your message. Press Ctrl-D to exit.\n");
        term_separator();
    }

    while (!g_interrupted) {
        if (idle_guard_is_expired()) {
            if (!quiet_mode)
                term_color(T_DIM, "Idle timeout — exiting.\n");
            break;
        }

        /* Hot-reload: check if file watcher flagged any config changes */
        if (hot_reload_pending()) {
            int flags = hot_reload_flags_consume();
            if (flags) {
                int n = hot_reload_process(flags, &g_hooks, &g_memory, &conv);
                if (n > 0 && !quiet_mode) {
                    for (int k = 0; k <= HOT_RELOAD_SETTINGS; k++) {
                        if (flags & (1 << k))
                            term_color(T_DIM, "[hot-reload] %s reloaded\n",
                                       hot_reload_kind_name((hot_reload_kind_t)k));
                    }
                }
            }
        }

        /* Away summary: check if user was idle */
        if (away_summary_should_generate() && conv.len > 0) {
            char *mem_text = memory_format_for_prompt(&g_memory);
            char *summary = away_summary_generate(&conv, mem_text, config_base_url(cfg),
                                                  config_api_key(cfg), cfg->model, cfg->provider);
            free(mem_text);
            if (summary) {
                char *formatted = away_summary_format(summary);
                if (formatted) {
                    term_color(T_CYAN, "%s\n", formatted);
                    free(formatted);
                }
                free(summary);
            }
        }

        /* Prompt suggestion: show if available */
        const char *suggestion = prompt_suggest_current();
        if (suggestion && suggestion[0] && !quiet_mode)
            term_color(T_DIM, "  [suggestion: %s]\n", suggestion);

        char *input = NULL;

        if (batch_mode) {
            if (batch_idx >= batch_total)
                break;
            const char *bp = batch_prompt_at(batch_idx);
            input = bp ? nc_xstrdup(bp) : nc_xstrdup("");
            if (!quiet_mode)
                term_color(T_GREEN, "> [batch %d/%d] %s\n", batch_idx + 1, batch_total, input);
        } else if (cfg->initial_prompt) {
            input = nc_xstrdup(cfg->initial_prompt);
            free(cfg->initial_prompt);
            cfg->initial_prompt = NULL;
            term_color(T_GREEN, "> %s\n", input);
        } else {
            input = term_readline("\001" T_GREEN "\002> \001" T_RESET "\002");
        }

        /* Touch activity timer on user input */
        away_summary_touch();
        idle_guard_touch();

        if (!input)
            break;
        if (input[0] == '\0') {
            free(input);
            continue;
        }

        /* Input preprocessing: length/binary check + whitespace normalize (skip slash) */
        if (input[0] != '/') {
            input_check_t icheck;
            char *preprocessed = input_preprocess(input, &icheck);
            if (icheck == INPUT_TOO_LONG) {
                term_color(T_YELLOW, "(input too long — max %zuKB)\n", input_max_length() / 1024);
                free(input);
                continue;
            }
            if (icheck == INPUT_BINARY) {
                term_color(T_YELLOW, "(binary content detected — skipping)\n");
                free(input);
                continue;
            }
            if (preprocessed) {
                free(input);
                input = preprocessed;
            } else if (icheck == INPUT_EMPTY) {
                free(input);
                continue;
            }
            if (input_is_warn_length(input) && !quiet_mode)
                term_color(T_YELLOW, "(large input: %zuKB)\n", strlen(input) / 1024);
        }

        /* multi-line: if line ends with '\', read continuation lines */
        while (1) {
            size_t len = strlen(input);
            if (len == 0 || input[len - 1] != '\\')
                break;
            input[len - 1] = '\n';
            char *cont = term_readline("\001" T_DIM "\002. \001" T_RESET "\002");
            if (!cont)
                break;
            size_t clen = strlen(cont);
            char *expanded = realloc(input, len + clen + 1);
            if (!expanded) {
                free(cont);
                break;
            }
            input = expanded;
            memcpy(input + len, cont, clen + 1);
            free(cont);
        }

        if (input[0] != '/') {
            long bud = budget_parse_shorthand(input);
            if (bud > 0) {
                int bi = bud > INT_MAX ? INT_MAX : (int)bud;
                token_budget_set(bi);
                if (!quiet_mode)
                    term_color(T_DIM, "(per-turn output token budget set to %d from message)\n",
                               bi);
            }
        }

        /* Slash commands — delegated to slash_commands.c */
        if (input[0] == '/') {
            slash_result_t sr = handle_slash_command(input, &conv, cfg, &session_id, quiet_mode);
            if (sr == CMD_EXIT) {
                free(input);
                break;
            }
            if (sr == CMD_CONTINUE) {
                free(input);
                continue;
            }
            if (sr == CMD_API_CALL) {
                free(input);
                goto api_call;
            }
            /* CMD_NOT_HANDLED — unknown slash command */
            {
                term_color(T_YELLOW, "Unknown command: %s\n", input);
                const char **cmds = term_get_slash_cmds();
                int ncmds = 0;
                while (cmds[ncmds])
                    ncmds++;
                cmd_fuzzy_result_t hints[CMD_FUZZY_MAX_RESULTS];
                int nh = cmd_fuzzy_match(input, cmds, ncmds, hints, CMD_FUZZY_MAX_RESULTS);
                if (nh > 0) {
                    term_color(T_DIM, "Did you mean:");
                    for (int hi = 0; hi < nh && hi < 3; hi++)
                        printf(" %s", hints[hi].name);
                    printf("\n");
                }
                term_color(T_DIM, "Type /help for available commands.\n");
                free(input);
                continue;
            }
        }

        /* Expand @file references and @image attachments in user input */
        {
            bool has_image = false;
            if (strchr(input, '@')) {
                const char *p = input;
                while (*p) {
                    if (*p == '@' && (p == input || isspace((unsigned char)*(p - 1)))) {
                        const char *s = p + 1, *e = s;
                        while (*e && (isalnum((unsigned char)*e) || *e == '/' || *e == '.' ||
                                      *e == '-' || *e == '_' || *e == '~' || *e == '+'))
                            e++;
                        if (e - s > 1) {
                            char tmp[NC_PATH_MAX];
                            size_t pl = e - s;
                            if (pl < sizeof(tmp)) {
                                memcpy(tmp, s, pl);
                                tmp[pl] = '\0';
                                if (is_image_file(tmp)) {
                                    has_image = true;
                                    break;
                                }
                            }
                        }
                    }
                    p++;
                }
            }

            if (has_image) {
                message_t *msg = conversation_add_user_multimodal(&conv);
                int n_img = context_inject_images(input, msg);
                if (!quiet_mode && n_img > 0)
                    term_color(T_DIM, "(%d image%s attached)\n", n_img, n_img > 1 ? "s" : "");
            } else {
                char *expanded = mention_parse_enhanced(input);
                if (!expanded)
                    expanded = context_inject_refs(input);
                if (expanded) {
                    int nrefs = mention_count_refs(input);
                    if (!quiet_mode && nrefs > 0)
                        term_color(T_DIM, "(%d reference%s resolved)\n", nrefs,
                                   nrefs > 1 ? "s" : "");
                    conversation_add_user_text(&conv, expanded);
                    free(expanded);
                } else {
                    conversation_add_user_text(&conv, input);
                }
            }
        }
        /* Record user turn in rollout tracker */
        if (conv.len > 0) {
            const message_t *last = &conv.msgs[conv.len - 1];
            if (last->role == ROLE_USER && last->num_blocks > 0 &&
                last->blocks[0].type == CONTENT_TEXT)
                rollout_record_user_message(last->blocks[0].text);
        }
        conv_rewind_checkpoint((int)turn_timing_count(), conv.len, NULL, NULL);
        free(input);
        printf("\n");

    api_call:
        (void)0;

        /* Feature list: inject structured progress anchor if present */
        if (feature_list_count() > 0 && conv.len > 0) {
            char *fl = feature_list_build_prompt();
            if (fl && fl[0]) {
                message_t *flm = conversation_add(&conv, ROLE_USER);
                message_add_text(flm, fl);
                if (!quiet_mode)
                    term_color(T_DIM, "[features] %d/%d passing\n", feature_list_passing_count(),
                               feature_list_count());
            }
            free(fl);
        }

        /* Episode memory: inject prior experience for recurring patterns */
        if (episode_memory_count() > 0 && conv.len > 0) {
            const message_t *last = &conv.msgs[conv.len - 1];
            if (last->role == ROLE_USER && last->num_blocks > 0 &&
                last->blocks[0].type == CONTENT_TEXT && last->blocks[0].text) {
                char *ep = episode_memory_build_prompt(last->blocks[0].text);
                if (ep && ep[0]) {
                    message_t *ctx = conversation_add(&conv, ROLE_USER);
                    message_add_text(ctx, ep);
                    if (!quiet_mode)
                        term_color(T_DIM, "[episode] prior experience injected\n");
                }
                free(ep);
            }
        }

        /* Pre-call micro-compact: when idle time exceeds cache TTL,
         * stale tool results are proactively cleared before the API call. */
        {
            int mc_freed = context_compact_pre_call(&conv, PRE_CALL_IDLE_THRESHOLD_MIN,
                                                    MICRO_COMPACT_KEEP_RECENT);
            if (mc_freed > 0)
                term_color(T_DIM, "(pre-call micro-compact: cleared ~%d tokens)\n",
                           context_estimate_freed_tokens(mc_freed));
        }

        /* Pre-flight context zone check */
        {
            int pre_est = estimate_conversation_tokens(conv.msgs, conv.len, conv.system_prompt);
            int pre_limit = context_limit_for_model(config_model(cfg));
            context_zone_t pre_zone = get_context_zone(pre_est, pre_limit);
            if (pre_zone == CTX_ZONE_BLOCKING) {
                int pct = pre_limit > 0 ? pre_est * 100 / pre_limit : 100;
                term_color(T_RED, "Context at %d%% (%dk/%dk) — auto-compacting before request...\n",
                           pct, pre_est / 1000, pre_limit / 1000);
                if (auto_compact(&conv))
                    rollout_meta_record_compact(&rollout_meta);
            }
        }

        file_history_next_turn();
        token_budget_start_turn();
        conv_recovery_save(&conv, session_id);
        int max_turns = cfg->max_turns > 0 ? cfg->max_turns : DEFAULT_MAX_TURNS;
        int context_retries = 0;
        int premature_stop_nudges = 0;
        auto_continue_init(MAX_CONTINUATION_RETRIES);
        bool prev_turn_had_tools = false;
        int total_stalls = 0;
        while (max_turns-- > 0 && !g_interrupted) {
            stream_state_t st;
            stream_state_init(&st);
            st.turn_num = conv.len / 2 + 1;
            streaming_exec_reset(streaming_exec_global());
            term_title_set(NC_TERM_TITLE_PREFIX " — thinking (turn %d)", st.turn_num);
            term_stream_start();

            if (!quiet_mode) {
                const char *tip = tips_get_random();
                if (tip)
                    term_color(T_DIM, "💡 %s\n", tip);
            }

            /* Brief mode: use modified system prompt */
            const char *effective_sysprompt = conv.system_prompt;
            char *brief_prompt = NULL;
            if (g_brief_mode && conv.system_prompt) {
                size_t slen = strlen(conv.system_prompt);
                static const char suffix[] = BRIEF_MODE_SUFFIX;
                brief_prompt = malloc(slen + sizeof(suffix));
                if (brief_prompt) {
                    memcpy(brief_prompt, conv.system_prompt, slen);
                    memcpy(brief_prompt + slen, suffix, sizeof(suffix));
                    effective_sysprompt = brief_prompt;
                }
            }

            char *verify_sysprompt = NULL;
            if (vtrig_should_verify()) {
                char *vsum = vtrig_summary();
                if (vsum && vsum[0]) {
                    strbuf_t vsb;
                    strbuf_init(&vsb);
                    strbuf_append_str(&vsb, effective_sysprompt);
                    strbuf_append_str(&vsb, VTRIG_VERIFY_HEADING VTRIG_VERIFY_BODY);
                    strbuf_append_str(&vsb, vsum);
                    verify_sysprompt = strbuf_detach(&vsb);
                    effective_sysprompt = verify_sysprompt;
                }
                free(vsum);
            }

            typedef int (*api_stream_fn_t)(const message_t *, int, const char *, const void *,
                                           const tool_def_t *, int, stream_callback_t, void *);
            watchdog_begin_turn(st.turn_num);
            sleep_inhibit_set_turn(true);
            g_in_api_call = 1;
            api_stream_record_cache(cfg, tools_get_all(), tools_count());
            int rc;
            if (multi_provider_has_fallback()) {
                rc = multi_provider_stream_with_fallback(conv.msgs, conv.len, effective_sysprompt,
                                                         cfg, tools_get_all(), tools_count(),
                                                         on_stream_event, &st);
            } else {
                rc = api_retry_stream((api_stream_fn_t)api_stream_request, conv.msgs, conv.len,
                                      effective_sysprompt, cfg, tools_get_all(), tools_count(),
                                      on_stream_event, &st);
            }
            free(verify_sysprompt);
            free(brief_prompt);
            g_in_api_call = 0;
            sleep_inhibit_set_turn(false);
            watchdog_end_turn(rc == 0 && !st.had_error);

            term_stream_end();
            term_title_set(NC_TERM_TITLE_PREFIX " — ready");

            if (g_interrupted) {
                g_interrupted = 0;
                interrupt_record(st.turn_num, INTR_USER_CANCEL, st.num_tc, st.text.len > 0);
                if (st.text.len > 0) {
                    message_t *partial = conversation_add(&conv, ROLE_ASSISTANT);
                    message_add_text(partial, st.text.data);
                    term_color(T_YELLOW, "\n(interrupted — %zu chars saved)\n", st.text.len);
                } else {
                    term_color(T_YELLOW, "\n(interrupted)\n");
                }
                {
                    char *marker = interrupt_build_marker(st.turn_num, INTR_USER_CANCEL);
                    if (marker) {
                        message_t *m = conversation_add(&conv, ROLE_USER);
                        message_add_text(m, marker);
                        free(marker);
                    }
                }
                conv_recovery_save(&conv, session_id);
                stream_state_free(&st);
                break;
            }

            if (rc != 0 || st.had_error) {
                api_error_class_t ecls = classify_api_error(st.errmsg);

                if (ecls == API_ERR_CONTEXT_OVERFLOW && context_retries < MAX_CONTEXT_RETRIES) {
                    context_retries++;
                    stream_state_free(&st);
                    if (auto_compact(&conv)) {
                        rollout_meta_record_compact(&rollout_meta);
                        continue;
                    }
                }

                if (ecls == API_ERR_WATCHDOG && context_retries < MAX_WATCHDOG_RETRIES) {
                    context_retries++;
                    term_color(T_YELLOW,
                               "[watchdog] turn deadline exceeded — injecting recovery prompt and "
                               "retrying (%d/%d)\n",
                               context_retries, MAX_WATCHDOG_RETRIES);
                    if (st.text.len > 0) {
                        message_t *partial = conversation_add(&conv, ROLE_ASSISTANT);
                        message_add_text(partial, st.text.data);
                    }
                    char *recovery = watchdog_recovery_prompt(WD_VERDICT_TIMEOUT);
                    if (recovery && recovery[0]) {
                        conversation_add_user_text(&conv, recovery);
                    }
                    free(recovery);
                    stream_state_free(&st);
                    continue;
                }

                const char *err_text = st.errmsg[0] ? st.errmsg : "API request failed";
                term_color(T_RED, "Error: %s\n", err_text);
                const char *hint = api_error_hint(ecls);
                if (hint)
                    term_color(T_YELLOW, "Hint: %s\n", hint);
                stream_state_free(&st);
                break;
            }
            context_retries = 0;

            g_last_assistant_time = nc_time_now();
            message_t *am = conversation_add(&conv, ROLE_ASSISTANT);
            if (st.text.len > 0)
                message_add_text(am, st.text.data);
            for (int i = 0; i < st.num_tc; i++) {
                cJSON *inp = st.pending_tc[i].args.data
                                 ? tool_args_parse_json(st.pending_tc[i].args.data)
                                 : NULL;
                message_add_tool_use(am, st.pending_tc[i].id, st.pending_tc[i].name, inp);
                rollout_record_tool_call(st.pending_tc[i].name, st.pending_tc[i].id, inp);
                if (inp)
                    cJSON_Delete(inp);
            }
            if (st.text.len > 0)
                rollout_record_assistant_message(st.text.data);

            g_total_input_tokens += st.input_tokens;
            g_total_output_tokens += st.output_tokens;
            {
                double cost_before = cost_tracker_total_cost();
                cost_tracker_add_ext(config_model(cfg), st.input_tokens, st.output_tokens,
                                     st.cache_read_tokens, st.cache_creation_tokens, 0, 0);
                sbar_set_tokens((int)g_total_input_tokens, (int)g_total_output_tokens);
                sbar_set_cost(cost_tracker_total_cost());
                sbar_set_turns((int)turn_timing_count());
                rollout_meta_record_turn(&rollout_meta, st.num_tc, st.input_tokens,
                                         st.output_tokens, cost_tracker_total_cost() - cost_before);
            }
            event_dispatch_turn_complete(st.turn_num, st.input_tokens, st.output_tokens,
                                         cost_tracker_total_cost());

            /* Auto-title the session after the first successful exchange */
            if (st.turn_num == 1 && session_id) {
                const char *first_msg = NULL;
                for (int fi = 0; fi < conv.len; fi++) {
                    if (conv.msgs[fi].role == ROLE_USER && conv.msgs[fi].num_blocks > 0 &&
                        conv.msgs[fi].blocks[0].type == CONTENT_TEXT) {
                        first_msg = conv.msgs[fi].blocks[0].text;
                        break;
                    }
                }
                if (first_msg)
                    session_title_auto(session_id, first_msg);
            }

            if (!quiet_mode && (st.input_tokens || st.output_tokens) &&
                !(st.stop_reason == STOP_TOOL_USE && st.num_tc > 0))
                print_turn_info(&st, g_total_input_tokens, g_total_output_tokens);

            if (st.stop_reason == STOP_MAX_TOKENS) {
                const char *fr = FINISH_REASON_LENGTH;
                bool has_pending = (st.num_tc > 0);
                if (auto_continue_should_retry(fr, st.text.data, has_pending)) {
                    if (auto_continue_needs_escalation() &&
                        cfg->max_tokens < auto_continue_escalated_tokens()) {
                        int prev = cfg->max_tokens;
                        ((config_t *)cfg)->max_tokens = auto_continue_escalated_tokens();
                        auto_continue_mark_escalated();
                        term_color(T_DIM, "[output truncated, escalating max_tokens %d -> %d]\n",
                                   prev, auto_continue_escalated_tokens());
                    }
                    term_color(T_DIM, "[auto-continuing %d/%d...]\n", auto_continue_retry_count(),
                               MAX_CONTINUATION_RETRIES);
                    conversation_add_user_text(&conv, auto_continue_prompt(has_pending));
                    stream_state_free(&st);
                    continue;
                }
                term_color(T_YELLOW,
                           "[warning: response truncated (max_tokens=%d, "
                           "recovery exhausted). Use -t to increase.]\n",
                           cfg->max_tokens);
            }

            if (st.stop_reason == STOP_TOOL_USE && st.num_tc > 0) {
                auto_continue_reset();
                prev_turn_had_tools = true;
                for (int ti = 0; ti < st.num_tc; ti++)
                    session_memory_record_tool_call();
                term_title_set(NC_TERM_TITLE_PREFIX " — executing %d tool%s", st.num_tc,
                               st.num_tc > 1 ? "s" : "");
                g_in_api_call = 1;
                execute_pending_tools(&conv, &st);
                g_in_api_call = 0;
                if (!quiet_mode && (st.input_tokens || st.output_tokens))
                    print_turn_info(&st, g_total_input_tokens, g_total_output_tokens);

                /* Record tool results in rollout tracker */
                for (int ri = conv.len - st.num_tc; ri < conv.len; ri++) {
                    if (ri < 0)
                        continue;
                    const message_t *rm = &conv.msgs[ri];
                    if (rm->role != ROLE_TOOL)
                        continue;
                    for (int bj = 0; bj < rm->num_blocks; bj++) {
                        const content_block_t *b = &rm->blocks[bj];
                        if (b->type == CONTENT_TOOL_RESULT)
                            rollout_record_tool_result(b->tool_use_id, b->text, b->is_error);
                    }
                }
                rollout_save();

                /* Tool verification: scan results for silent failures */
                {
                    int scan_from = conv.len - st.num_tc;
                    if (scan_from < 0)
                        scan_from = 0;
                    for (int vi = scan_from; vi < conv.len; vi++) {
                        const message_t *rm = &conv.msgs[vi];
                        if (rm->role != ROLE_TOOL)
                            continue;
                        for (int bj = 0; bj < rm->num_blocks; bj++) {
                            const content_block_t *b = &rm->blocks[bj];
                            if (b->type == CONTENT_TOOL_RESULT) {
                                const char *vname = b->tool_name;
                                if (!vname) {
                                    for (int ti = 0; ti < st.num_tc; ti++) {
                                        if (st.pending_tc[ti].id[0] && b->tool_use_id &&
                                            strcmp(st.pending_tc[ti].id, b->tool_use_id) == 0) {
                                            vname = st.pending_tc[ti].name[0]
                                                        ? st.pending_tc[ti].name
                                                        : NULL;
                                            break;
                                        }
                                    }
                                }
                                tv_verdict_t vv = tool_verify_check(
                                    vname, b->text, b->text ? (int)strlen(b->text) : 0,
                                    b->is_error ? 1 : 0, b->is_error);
                                if (tool_verify_needs_intervention(vv) && !quiet_mode)
                                    term_color(T_YELLOW, "[verify] %s: %s\n", vname ? vname : "?",
                                               tv_verdict_name(vv));
                            }
                        }
                    }
                }

                /* Post-tool context zone check */
                int post_est =
                    estimate_conversation_tokens(conv.msgs, conv.len, conv.system_prompt);
                int post_limit = context_limit_for_model(config_model(cfg));
                context_zone_t post_zone = get_context_zone(post_est, post_limit);
                int ctx_pct = post_limit > 0 ? post_est * 100 / post_limit : 0;

                /* Harness: track context and fatigue metrics */
                context_fatigue_record(st.turn_num, post_est);
                {
                    char stall_sig[NC_MSG_MAX] = "";
                    int soff = 0;
                    for (int si = 0; si < st.num_tc && soff < (int)sizeof(stall_sig) - 32; si++) {
                        if (si > 0)
                            stall_sig[soff++] = '+';
                        int w = snprintf(stall_sig + soff, sizeof(stall_sig) - soff, "%s",
                                         st.pending_tc[si].name);
                        if (w > 0)
                            soff += w;
                    }
                    stall_detector_record_turn(soff > 0 ? stall_sig : NULL, (int)st.text.len,
                                               file_history_count() > 0);
                }
                harness_post_turn(harness_global(), ctx_pct, st.num_tc, false);

                /* Ghost snapshot: create a rollback point after tool execution */
                if (ghost_snapshot_available())
                    ghost_snapshot_create(st.turn_num);

                /* Auto-test: suggest running tests after file edits */
                if (auto_test_is_enabled() && file_history_count() > 0 &&
                    auto_test_should_run(st.turn_num)) {
                    const char *fpaths[1] = {NULL};
                    int nf = file_history_files_at_turn(file_history_current_turn(), fpaths, 1);
                    if (nf > 0 && fpaths[0]) {
                        char *atest_hint = auto_test_suggest_prompt(fpaths[0]);
                        if (atest_hint) {
                            if (!quiet_mode)
                                term_color(T_DIM, "[auto-test] %s\n", atest_hint);
                            free(atest_hint);
                        }
                    }
                }

                /* Checkpoint discipline: periodic git commits during long sessions */
                if (checkpoint_discipline_should_fire(st.turn_num, file_history_count() > 0)) {
                    checkpoint_discipline_create(st.turn_num);
                    rollout_record_checkpoint(NULL);
                    if (!quiet_mode)
                        term_color(T_DIM, "[checkpoint] turn %d saved\n", st.turn_num);
                }
                if (harness_needs_handoff(harness_global())) {
                    if (!quiet_mode)
                        term_color(T_DIM,
                                   "[harness] context at %d%%, performing structured handoff...\n",
                                   ctx_pct);
                    rollout_record_handoff("context exhaustion");
                    char *resume = harness_do_handoff(harness_global(), &conv, session_id);
                    if (auto_compact(&conv))
                        rollout_meta_record_compact(&rollout_meta);
                    if (resume) {
                        conversation_add_user_text(&conv, resume);
                        free(resume);
                    }
                    g_warned_context = false;
                } else if (post_zone >= CTX_ZONE_COMPACT || context_fatigue_needs_compact()) {
                    if (!quiet_mode && context_fatigue_needs_compact() &&
                        post_zone < CTX_ZONE_COMPACT) {
                        int eta = context_fatigue_turns_remaining();
                        term_color(T_DIM,
                                   "[fatigue] ~%d turns until exhaustion, "
                                   "pre-emptive compact...\n",
                                   eta);
                    }
                    if (auto_compact(&conv))
                        rollout_meta_record_compact(&rollout_meta);
                    g_warned_context = false;
                } else if (post_zone == CTX_ZONE_WARNING && !g_warned_context) {
                    term_color(T_YELLOW, "⚠ Context at %d%%. Use /compact to free space.\n",
                               ctx_pct);
                    g_warned_context = true;
                }

                /* Stall detection: break out of stuck loops.
                 * Uses total_stalls (never reset within a task) because small
                 * models vary tool calls just enough to temporarily clear the
                 * stall detector, then re-enter the same loop. */
                {
                    stall_verdict_t sv = stall_detector_check();
                    if (sv != STALL_OK) {
                        total_stalls++;
                        if (!quiet_mode) {
                            char *stall_hint = stall_detector_suggest(sv);
                            if (stall_hint) {
                                term_color(T_YELLOW, "%s\n", stall_hint);
                                free(stall_hint);
                            }
                        }
                        if (sv == STALL_TIMEOUT)
                            harness_post_turn(harness_global(), 100, 0, true);

                        if (total_stalls >= STALL_MAX_CONSECUTIVE) {
                            if (!quiet_mode)
                                term_color(T_RED,
                                           "[stall] %d stalls detected — forcing stop. "
                                           "The model is stuck in a loop.\n",
                                           total_stalls);
                            prev_turn_had_tools = false;
                            stream_state_free(&st);
                            printf("\n");
                            break;
                        }

                        /* Only open hypothesis debug for true loops with
                         * write tools.  Read-only loops (Batch of Read/Grep/
                         * ListDir) are normal during project analysis and
                         * should not inject debug prompts. */
                        if (sv == STALL_LOOP_DETECTED) {
                            bool has_write = false;
                            for (int si = 0; si < st.num_tc && !has_write; si++) {
                                const tool_def_t *td = tools_find(st.pending_tc[si].name);
                                if (td && !td->read_only &&
                                    strcmp(st.pending_tc[si].name, STALL_EXEMPT_TOOL_BATCH) != 0 &&
                                    strcmp(st.pending_tc[si].name, STALL_EXEMPT_TOOL_TODO) != 0)
                                    has_write = true;
                            }
                            if (has_write) {
                                int ds = hypothesis_debug_begin(
                                    "Agent stuck in repeating tool-call loop");
                                if (ds >= 0) {
                                    hypothesis_debug_auto_populate(ds, (int)sv);
                                    char *dp = hypothesis_debug_build_prompt(ds);
                                    if (dp && dp[0])
                                        conversation_add_user_text(&conv, dp);
                                    free(dp);
                                }
                            }
                        }
                    }
                }

                stream_state_free(&st);
                printf("\n");
                continue;
            }

            /* Premature stop recovery: if the previous turn executed tools
             * but this turn the model stopped with minimal output and no
             * new tool calls, nudge it to continue.  Common with small
             * models that "give up" early.
             * Skip if we just broke out of a stall loop — the model already
             * tried and failed; nudging would re-enter the loop. */
            if (st.stop_reason == STOP_END_TURN && st.num_tc == 0 && prev_turn_had_tools &&
                total_stalls == 0 && (int)st.text.len < PREMATURE_STOP_CHAR_THRESHOLD &&
                premature_stop_nudges < MAX_PREMATURE_STOP_NUDGES) {
                premature_stop_nudges++;
                if (!quiet_mode)
                    term_color(T_DIM,
                               "[auto-continue] model stopped prematurely after tool use "
                               "(%d/%d)\n",
                               premature_stop_nudges, MAX_PREMATURE_STOP_NUDGES);
                conversation_add_user_text(&conv, PREMATURE_STOP_NUDGE);
                stream_state_free(&st);
                continue;
            }
            prev_turn_had_tools = false;

            /* Stop hook check */
            if (st.stop_reason == STOP_END_TURN && g_hooks.num_hooks > 0) {
                char *stop_ctx = NULL;
                const char *last_text = st.text.data;
                hook_decision_t sd = hooks_run(&g_hooks, HOOK_STOP, NULL, last_text, &stop_ctx);
                if (sd == HOOK_DECISION_BLOCK && stop_ctx) {
                    term_color(T_DIM, "[stop hook triggered, continuing...]\n");
                    strbuf_t inj;
                    strbuf_init(&inj);
                    strbuf_append_str(&inj, HOOK_STOP_PREFIX);
                    strbuf_append_str(&inj, stop_ctx);
                    strbuf_append_str(&inj, HOOK_STOP_SUFFIX);
                    conversation_add_user_text(&conv, inj.data);
                    strbuf_free(&inj);
                    free(stop_ctx);
                    stream_state_free(&st);
                    continue;
                }
                free(stop_ctx);
            }

            /* Output evaluation: score the response quality (skip in one-shot mode) */
            if (!one_shot && st.stop_reason == STOP_END_TURN && st.text.len > 0) {
                const char *user_goal = NULL;
                for (int gi = conv.len - 1; gi >= 0; gi--) {
                    if (conv.msgs[gi].role == ROLE_USER) {
                        for (int gj = 0; gj < conv.msgs[gi].num_blocks; gj++) {
                            if (conv.msgs[gi].blocks[gj].type == CONTENT_TEXT &&
                                conv.msgs[gi].blocks[gj].text) {
                                user_goal = conv.msgs[gi].blocks[gj].text;
                                break;
                            }
                        }
                        break;
                    }
                }
                oeval_result_t ev = output_eval_check_ex(st.text.data, user_goal, st.num_tc);
                if (output_eval_needs_retry(ev.verdict) &&
                    output_eval_consecutive_weak() < OEVAL_DEGRADE_THRESHOLD) {
                    char *rp = output_eval_retry_prompt(ev.verdict, user_goal);
                    if (rp) {
                        if (!quiet_mode)
                            term_color(T_DIM, "[eval] %s (confidence=%.2f)\n",
                                       output_eval_verdict_name(ev.verdict), ev.confidence);
                        conversation_add_user_text(&conv, rp);
                        free(rp);
                        stream_state_free(&st);
                        continue;
                    }
                }
                if (output_eval_degrading() && !quiet_mode)
                    term_color(T_YELLOW, "[eval] output quality degrading — consider /compact or "
                                         "rephrasing the goal\n");
            }

            /* Auto-memory extraction (heuristic) */
            if (g_memory.len >= 0 && conv.len >= 4) {
                int prev_count = g_memory.len;
                auto_extract_memories(&conv, &g_memory);
                if (g_memory.len > prev_count) {
                    int merged = memory_consolidate(&g_memory);
                    if (merged > 0 && !quiet_mode)
                        term_color(T_DIM, "[memory] consolidated %d entries\n", merged);
                    char mem_path[NC_PATH_MAX];
                    config_find_path(mem_path, sizeof(mem_path), NC_CFG_MEMORY);
                    memory_save(&g_memory, mem_path);
                }
            }

            /* Session memory: extract notes periodically */
            if (session_memory_enabled()) {
                int cur_turn = file_history_current_turn();
                if (session_memory_should_extract(cur_turn)) {
                    char *sm =
                        session_memory_extract(&conv, config_base_url(cfg), config_api_key(cfg),
                                               cfg->model, cfg->provider);
                    if (sm && !quiet_mode)
                        term_color(T_DIM, "[session-memory] updated (%d extractions)\n",
                                   session_memory_extraction_count());
                    free(sm);
                    session_memory_mark_extracted(cur_turn);
                }
            }

            stream_state_free(&st);
            break;
        }

        /* Post-turn: generate next prompt suggestion */
        if (prompt_suggest_enabled() && !one_shot && !quiet_mode) {
            prompt_suggest_clear();
            char *sugg = prompt_suggest_generate(&conv, config_base_url(cfg), config_api_key(cfg),
                                                 cfg->model, cfg->provider);
            free(sugg);
        }

        printf("\n");
        if (batch_mode) {
            const char *last_asst = NULL;
            for (int i = conv.len - 1; i >= 0; i--) {
                if (conv.msgs[i].role == ROLE_ASSISTANT) {
                    for (int j = 0; j < conv.msgs[i].num_blocks; j++) {
                        if (conv.msgs[i].blocks[j].type == CONTENT_TEXT &&
                            conv.msgs[i].blocks[j].text) {
                            last_asst = conv.msgs[i].blocks[j].text;
                            break;
                        }
                    }
                    if (last_asst)
                        break;
                }
            }
            batch_record_result(batch_idx, last_asst ? last_asst : "", last_asst != NULL);
            batch_save_checkpoint(batch_idx + 1);
            conversation_clear_messages(&conv);
            doom_loop_reset();
            batch_idx++;
            if (batch_idx >= batch_total) {
                char *bsum = batch_format_summary();
                if (bsum) {
                    if (!cfg->json_output)
                        term_color(T_DIM, "%s", bsum);
                    free(bsum);
                }
                break;
            }
            continue;
        }
        if (one_shot)
            break;
    }

    /* Collect final assistant text for --json / --output */
    char *final_text = NULL;
    for (int i = conv.len - 1; i >= 0; i--) {
        if (conv.msgs[i].role == ROLE_ASSISTANT) {
            for (int j = 0; j < conv.msgs[i].num_blocks; j++) {
                if (conv.msgs[i].blocks[j].type == CONTENT_TEXT && conv.msgs[i].blocks[j].text) {
                    final_text = conv.msgs[i].blocks[j].text;
                    break;
                }
            }
            if (final_text)
                break;
        }
    }

    /* --json: structured JSON output */
    if (cfg->json_output && final_text) {
        char *extracted = structured_output_extract_json(final_text);
        cJSON *out = cJSON_CreateObject();
        if (extracted) {
            cJSON *parsed = cJSON_Parse(extracted);
            if (parsed) {
                cJSON_AddItemToObject(out, "result", parsed);
            } else {
                cJSON_AddStringToObject(out, "result", final_text);
            }
            free(extracted);
        } else {
            cJSON_AddStringToObject(out, "result", final_text);
        }
        cJSON_AddStringToObject(out, "model", config_model(cfg));
        cJSON_AddStringToObject(out, "session_id", session_id);
        cJSON_AddNumberToObject(out, "input_tokens", g_total_input_tokens);
        cJSON_AddNumberToObject(out, "output_tokens", g_total_output_tokens);
        char *json = nc_cjson_detach_pretty(out);
        fprintf(stdout, "%s\n", json);
        free(json);
    }

    /* --output: write result to file */
    if (cfg->output_file && final_text) {
        FILE *of = fopen(cfg->output_file, "w");
        if (of) {
            bool ok = fputs(final_text, of) != EOF && !fflush(of);
            fclose(of);
            if (!ok)
                term_color(T_RED, "\nFailed to flush %s\n", cfg->output_file);
            else if (!cfg->json_output)
                term_color(T_DIM, "\nResult written to %s\n", cfg->output_file);
        } else {
            term_color(T_RED, "\nFailed to write to %s\n", cfg->output_file);
        }
    }

    if (!quiet_mode && (g_total_input_tokens || g_total_output_tokens)) {
        char *cost_summary = cost_tracker_format();
        if (cost_summary) {
            term_color(T_DIM, "\n%s", cost_summary);
            free(cost_summary);
        }
    }

    /* Session index: record final stats */
    session_index_update_activity(session_id, conv.len / 2, cost_tracker_total_cost());
    if (g_memory.len > 0)
        session_index_set_memories(session_id, true);

    /* Memory phases: extract + consolidate raw memories accumulated this session */
    if (mem_phase_raw_count() > 0) {
        mem_phase1_run_all();
        mem_phase2_consolidate();
    }

    /* Save progress tracker state */
    if (progress_tracker_count() > 0)
        progress_tracker_save();

    /* Save session memory (auto-extracted notes) */
    session_memory_save();

    /* Save key-value memories before exit */
    if (g_memory.len > 0) {
        char mem_path[NC_PATH_MAX];
        config_find_path(mem_path, sizeof(mem_path), NC_CFG_MEMORY);
        memory_save(&g_memory, mem_path);
    }

    /* Finalize and persist rollout data */
    rollout_meta_finalize(&rollout_meta, NULL);
    rollout_meta_save(&rollout_meta, session_id);
    rollout_save();
    if (rollout_truncate_check_needed(session_id))
        rollout_truncate_file(session_id, ROLLOUT_TRUNCATE_KEEP_TURNS);
    if (conv.len > 0)
        history_save(&conv, session_id);

    /* Record session statistics */
    {
        long session_duration = 0;
        if (g_last_assistant_time > 0)
            session_duration = nc_wall_sec() - (long)g_last_assistant_time;
        if (session_duration < 0)
            session_duration = 0;
        session_stats_record_session(config_model(cfg), (long)g_total_input_tokens,
                                     (long)g_total_output_tokens, cost_tracker_total_cost(),
                                     session_duration, (long)turn_timing_count(),
                                     turn_diff_total_added() + turn_diff_total_removed(),
                                     turn_diff_total_added(), turn_diff_total_removed());
        session_stats_save();
    }

    {
        dispatch_event_t ev = {.kind = EV_SESSION_END,
                               .timestamp = nc_wall_sec(),
                               .tokens_in = g_total_input_tokens,
                               .tokens_out = g_total_output_tokens,
                               .cost = cost_tracker_total_cost()};
        event_dispatch_emit(&ev);
    }
    event_dispatch_free();

    if (ghost_snapshot_count() > 0 && session_id)
        ghost_snapshot_cleanup(session_id);
    ghost_snapshot_free();
    context_fatigue_free();
    stall_detector_free();
    tool_verify_free();
    output_eval_free();
    auto_test_free();
    failure_router_free();
    checkpoint_discipline_free();
    hypothesis_debug_free();
    episode_memory_save();
    episode_memory_free();
    harness_free(harness_global());
    tool_router_free();
    exec_policy_free();
    session_scratch_cleanup();
    idle_guard_stop();
    process_state_free();
    delegate_free();
    sleep_inhibit_release();
    repl_cleanup_all();
    file_watcher_free(file_watcher_global());
    procgroup_graceful_shutdown(procgroup_global(), 3);
    agent_pool_free(agent_pool_global());
    free(session_id);
    perm_free(&g_perms);
    hooks_free(&g_hooks);
    memory_free(&g_memory);
    file_history_free();
    git_undo_free();
    read_file_tracker_free(&g_read_tracker);
    custom_skills_free();
    approval_cache_free();
    session_memory_free();
    conv_branch_free();
    model_router_free();
    multi_provider_free();
    session_stats_free();
    conv_recovery_clear();
    model_presets_free();
    rollout_tracker_free();
    conversation_free(&conv);
    warmup_free();
    plugin_inject_free();
    session_index_free();
    mem_phase_free();
    interrupt_ctx_free();
    autofix_free();
    event_bus_free();
    pm_free();
    proto_free();
    transcript_free();
    abort_handler_free();
    scratchpad_free();
    compact_tpl_free();
    auto_compact_free();
    auto_continue_free();
    env_ctx_free();
    feature_list_free();
    session_preflight_free();
    thread_state_free();
    agent_health_free();
    agent_summary_free();
    keybindings_free();
    turn_meta_free();
    turn_diff_free();
    tool_call_rt_free();
    state_db_free();
    ff_free();
    provider_auth_free();
    realtime_ctx_free();
    vim_mode_free();
    output_style_free();
    palette_free();
    sbar_free();
    conv_rewind_free();
    prompt_suggest_free();
    network_policy_free(network_policy_global());
    exec_rules_free(exec_rules_global());
    for (int i = 0; i < g_num_mcp_servers; i++)
        mcp_stop(&g_mcp_servers[i]);
    if (cfg->batch_file)
        batch_free();
    return 0;
}

int main(int argc, char **argv) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    term_init();
    term_history_load();

    nc_sigint_arm();
    signal(SIGPIPE, SIG_IGN);

    config_t cfg;
    config_init(&cfg);
    if (config_parse(&cfg, argc, argv) != 0) {
        config_free(&cfg);
        return 1;
    }
    if (cfg.no_markdown || !term_is_tty())
        term_set_no_markdown(true);

    if (cfg.cwd) {
        if (chdir(cfg.cwd) != 0) {
            fprintf(stderr, "Error: cannot change to directory: %s\n", cfg.cwd);
            config_free(&cfg);
            return 1;
        }
    }

    if (cfg.batch_file) {
        int bn = batch_load_prompts(cfg.batch_file);
        if (bn < 0) {
            fprintf(stderr, "batch: cannot load %s\n", cfg.batch_file);
            config_free(&cfg);
            return 1;
        }
        if (bn == 0) {
            fprintf(stderr, "batch: no prompts in %s\n", cfg.batch_file);
            batch_free();
            config_free(&cfg);
            return 1;
        }
    }

    if (!isatty(STDIN_FILENO)) {
        /* Use select() to check if stdin has data, avoiding blocking
         * forever on pipes that never close (e.g. IDE terminals) */
        fd_set rfds;
        struct timeval tv = {.tv_sec = 0, .tv_usec = PROC_POLL_INTERVAL_US};
        FD_ZERO(&rfds);
        if (STDIN_FILENO < FD_SETSIZE)
            FD_SET(STDIN_FILENO, &rfds);
        if (STDIN_FILENO < FD_SETSIZE && select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0) {
            strbuf_t piped;
            strbuf_init(&piped);
            char buf[NC_IO_BUF];
            ssize_t n;
            while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0)
                strbuf_append(&piped, buf, n);
            if (piped.len > 0) {
                if (cfg.initial_prompt) {
                    strbuf_t combined;
                    strbuf_init(&combined);
                    strbuf_append_str(&combined, "<stdin>\n");
                    strbuf_append_str(&combined, piped.data);
                    strbuf_append_str(&combined, "\n</stdin>\n\n");
                    strbuf_append_str(&combined, cfg.initial_prompt);
                    free(cfg.initial_prompt);
                    cfg.initial_prompt = strbuf_detach(&combined);
                    strbuf_free(&piped);
                } else {
                    cfg.initial_prompt = strbuf_detach(&piped);
                }
            } else {
                strbuf_free(&piped);
            }
        }
    }

    pinfo_init();
    const char *base = config_base_url(&cfg);
    if (base)
        pinfo_auto_register_from_url(base);
    request_compress_auto_configure(base);

    int rc = run_repl(&cfg);

    term_history_save();
    term_title_clear();
    term_cleanup();
    config_free(&cfg);
    curl_global_cleanup();
    return rc;
}
