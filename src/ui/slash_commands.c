#include "repl.h"
#include "api.h"
#include "git_ops.h"
#include "terminal.h"
#include "tools.h"
#include "harness.h"

#include <sys/utsname.h>

static bool restore_guard_rewind_blocked(int turn) {
    char cwd[NC_PATH_MAX];
    if (turn <= 0 || !file_history_has_turn(turn))
        return false;
    if (!getcwd(cwd, sizeof(cwd)) || !git_is_repo(cwd))
        return false;
    if (!git_has_uncommitted(cwd))
        return false;
    term_color(T_YELLOW, "(rewind skipped: current working tree has uncommitted changes)\n");
    term_color(T_DIM, "Commit, stash, or clean the repo before restoring an older turn.\n");
    return true;
}

slash_result_t handle_slash_command(const char *input, conversation_t *conv, config_t *cfg,
                                    char **session_id, bool quiet_mode) {
    if (strcmp(input, "/exit") == 0 || strcmp(input, "/quit") == 0)
        return CMD_EXIT;

    if (strcmp(input, "/clear") == 0) {
        for (int i = 0; i < conv->len; i++)
            message_free(&conv->msgs[i]);
        conv->len = 0;
        psec_invalidate_all();
        vtrig_reset();
        term_color(T_DIM, "(conversation cleared)\n");
        return CMD_CONTINUE;
    }

    if (nc_starts_with(input, "/compact")) {
        const char *instruct = input + 8;
        while (*instruct == ' ')
            instruct++;
        if (*instruct) {
            term_color(T_DIM, "(compacting with: %s)\n", instruct);
            g_compact_instructions = nc_xstrdup(instruct);
        }
        if (!auto_compact(conv))
            term_color(T_DIM, "(conversation too short to compact)\n");
        else
            psec_invalidate_all();
        free(g_compact_instructions);
        g_compact_instructions = NULL;
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/history") == 0 || nc_starts_with(input, "/history ")) {
        const char *arg = strlen(input) > 9 ? input + 9 : NULL;
        if (arg && nc_starts_with(arg, "search ")) {
            const char *query = arg + 7;
            while (*query == ' ')
                query++;
            if (*query) {
                char *listing = NULL;
                int n = history_search_fuzzy(query, &listing);
                if (n > 0) {
                    term_color(T_BOLD, "Search results for \"%s\":\n", query);
                    printf("%s", listing);
                    term_color(T_DIM, "Resume with: /resume <session-id>\n");
                } else {
                    term_color(T_DIM, "%s\n", listing);
                }
                free(listing);
            } else {
                term_color(T_DIM, "Usage: /history search <query>\n");
            }
        } else {
            char *listing = NULL;
            int n = nc_session_history_list(&listing);
            if (n > 0) {
                term_color(T_BOLD, "Recent sessions:\n");
                printf("%s", listing);
                term_color(T_DIM, "Resume with: /resume <session-id>\n");
            } else {
                term_color(T_DIM, "%s\n", listing);
            }
            free(listing);
        }
        return CMD_CONTINUE;
    }

    if (nc_starts_with(input, "/resume ")) {
        const char *sid = input + 8;
        while (*sid == ' ')
            sid++;
        if (*sid) {
            for (int i = 0; i < conv->len; i++)
                message_free(&conv->msgs[i]);
            conv->len = 0;
            if (history_load(conv, sid) == 0) {
                free(*session_id);
                *session_id = nc_xstrdup(sid);
                term_color(T_DIM, "Resumed session: %s (%d messages)\n", sid, conv->len);
                history_file_snapshot_status_t hs = history_restore_file_snapshot(sid);
                char *hs_msg = history_file_snapshot_status_message(sid, hs);
                if (hs_msg) {
                    term_color(hs == HISTORY_FILE_SNAPSHOT_RESTORED ? T_GREEN : T_YELLOW, "%s",
                               hs_msg);
                    free(hs_msg);
                }
            } else {
                term_color(T_RED, "Session not found: %s\n", sid);
            }
        }
        return CMD_CONTINUE;
    }

    if (nc_starts_with(input, "/model ")) {
        const char *new_model = input + 7;
        while (*new_model == ' ')
            new_model++;
        if (*new_model) {
            free(cfg->model);
            cfg->model = nc_xstrdup(new_model);
            rollout_record_settings("model", cfg->model);
            term_color(T_DIM, "(model changed to: %s)\n", cfg->model);
        } else {
            term_color(T_DIM, "Current model: %s\n", config_model(cfg));
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/model") == 0) {
        term_color(T_DIM,
                   "Current model: %s\n"
                   "Change with: /model <name>\n",
                   config_model(cfg));
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/models") == 0) {
        term_color(T_DIM, "Fetching models from %s ...\n", config_base_url(cfg));
        int n = model_discovery_fetch(config_base_url(cfg), config_api_key(cfg));
        if (n <= 0) {
            term_color(T_YELLOW, "No models returned (check base URL and API key).\n");
        } else {
            char *listing = model_discovery_format();
            if (listing) {
                printf("%s", listing);
                free(listing);
            }
            term_color(T_DIM, "(%d model%s available)\n", n, n > 1 ? "s" : "");
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/mpreset") == 0 || strcmp(input, "/mpreset list") == 0) {
        char *s = model_preset_format_all();
        if (s) {
            printf("%s", s);
            free(s);
        }
        return CMD_CONTINUE;
    }
    if (nc_starts_with(input, "/mpreset ")) {
        const char *pname = input + 9;
        while (*pname == ' ')
            pname++;
        if (!*pname) {
            char *s = model_preset_format_all();
            if (s) {
                printf("%s", s);
                free(s);
            }
            return CMD_CONTINUE;
        }
        if (model_preset_activate(pname) != 0) {
            term_color(T_RED, "Unknown model preset: %s\n", pname);
            return CMD_CONTINUE;
        }
        int idx = model_preset_find(pname);
        if (idx < 0)
            return CMD_CONTINUE;
        free(cfg->model);
        cfg->model = nc_xstrdup(model_preset_model(idx));
        cfg->max_tokens = model_preset_max_tokens(idx);
        cfg->effort = model_preset_effort(idx);
        effort_level_set(cfg->effort);
        auto_compact_set_window(model_preset_context_window(idx), cfg->max_tokens);
        term_color(T_GREEN, "Active preset: %s → model %s (max_tokens=%d ctx=%dk effort=%s)\n",
                   pname, cfg->model, cfg->max_tokens, model_preset_context_window(idx) / 1000,
                   effort_level_name(cfg->effort));
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/tokens") == 0) {
        if (g_total_input_tokens || g_total_output_tokens)
            term_color(T_DIM,
                       "Session tokens: %d in, %d out (total %d)\n"
                       "Messages: %d\n",
                       g_total_input_tokens, g_total_output_tokens,
                       g_total_input_tokens + g_total_output_tokens, conv->len);
        else
            term_color(T_DIM, "(no API calls yet)\n");
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/cost") == 0) {
        if (g_total_input_tokens || g_total_output_tokens) {
            char *cost_str = cost_tracker_format();
            if (cost_str) {
                term_color(T_DIM, "%s", cost_str);
                free(cost_str);
            }
        } else {
            term_color(T_DIM, "(no API calls yet)\n");
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/undo") == 0) {
        if (conv->len == 0) {
            term_color(T_DIM, "(nothing to undo)\n");
        } else {
            int cur_turn = file_history_current_turn();
            int removed = 0;
            while (conv->len > 0) {
                message_role_t role = conv->msgs[conv->len - 1].role;
                message_free(&conv->msgs[conv->len - 1]);
                conv->len--;
                removed++;
                if (role == ROLE_USER)
                    break;
            }
            term_color(T_DIM, "(undid %d message%s)\n", removed, removed > 1 ? "s" : "");
            if (cur_turn > 0) {
                int restored = 0, errors = 0;
                file_history_restore_turn(cur_turn, &restored, &errors);
                if (restored > 0)
                    term_color(T_GREEN, "(restored %d file%s from turn %d)\n", restored,
                               restored > 1 ? "s" : "", cur_turn);
                if (git_undo_has_turn(cur_turn)) {
                    if (git_undo_restore(cur_turn) == 0)
                        term_color(T_GREEN, "(git changes reverted for turn %d)\n", cur_turn);
                    else
                        term_color(
                            T_YELLOW,
                            "(git restore was unavailable for turn %d; file checkpoints only)\n",
                            cur_turn);
                }
            }
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/rules") == 0) {
        char *out = exec_rules_format(exec_rules_global());
        term_color(T_DIM, "%s", out);
        free(out);
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/config") == 0) {
        term_color(T_BOLD, "Current configuration:\n");
        term_color(T_DIM, "  Provider:     %s\n", nc_provider_slug(cfg->provider));
        term_color(T_DIM, "  Model:        %s\n", config_model(cfg));
        term_color(T_DIM, "  Base URL:     %s\n", config_base_url(cfg));
        term_color(T_DIM, "  Max Tokens:   %d\n", cfg->max_tokens);
        term_color(T_DIM, "  Effort:       %s\n", effort_level_name(cfg->effort));
        if (cfg->temperature >= 0)
            term_color(T_DIM, "  Temperature:  %.2f\n", cfg->temperature);
        if (cfg->top_p >= 0)
            term_color(T_DIM, "  Top-p:        %.2f\n", cfg->top_p);
        if (cfg->seed >= 0)
            term_color(T_DIM, "  Seed:         %d\n", cfg->seed);
        if (cfg->context_window > 0)
            term_color(T_DIM, "  Ctx Window:   %d\n", cfg->context_window);
        int mt = cfg->max_turns > 0 ? cfg->max_turns : DEFAULT_MAX_TURNS;
        term_color(T_DIM, "  Max Turns:    %d\n", mt);
        term_color(T_DIM, "  Markdown:     %s\n", cfg->no_markdown ? "off" : "on");
        term_color(T_DIM, "\nConfig file: %s/config.json\n", config_home_dir());
        return CMD_CONTINUE;
    }

    if (nc_starts_with(input, "/export")) {
        const char *arg = input + 7;
        while (*arg == ' ')
            arg++;
        char export_path[NC_PATH_MAX];
        conv_export_fmt_t fmt;
        if (*arg) {
            nc_strlcpy(export_path, arg, sizeof(export_path));
            fmt = conv_export_detect_format(export_path);
        } else {
            snprintf(export_path, sizeof(export_path), NC_EXPORT_FILE_PREFIX "%s.md", *session_id);
            fmt = EXPORT_MARKDOWN;
        }
        int rc = conv_export_to_file(conv, export_path, fmt, NULL);
        if (rc == 0) {
            const char *fmt_name = fmt == EXPORT_HTML   ? "HTML"
                                   : fmt == EXPORT_JSON ? "JSON"
                                                        : "Markdown";
            term_color(T_DIM, "Exported (%s) to %s\n", fmt_name, export_path);
        } else {
            term_color(T_RED, "Failed to write export file\n");
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/plan") == 0) {
        g_plan_mode = true;
        term_color(T_BOLD, "(plan mode ON) ");
        term_color(T_DIM, "Only read-only tools available. Use /endplan to exit.\n");
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/endplan") == 0) {
        g_plan_mode = false;
        term_color(T_DIM, "(plan mode OFF) All tools available.\n");
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/memory") == 0) {
        if (g_memory.len == 0) {
            term_color(T_DIM, "(no memories stored)\n");
        } else {
            term_color(T_BOLD, "Stored memories:\n");
            for (int mi = 0; mi < g_memory.len; mi++)
                term_color(T_DIM, "  %s: %s\n", g_memory.entries[mi].key,
                           g_memory.entries[mi].value);
        }
        return CMD_CONTINUE;
    }

    if (nc_starts_with(input, "/remember ")) {
        const char *kv = input + 10;
        while (*kv == ' ')
            kv++;
        const char *eq = strchr(kv, '=');
        if (eq && eq > kv) {
            char key[NC_LABEL_MAX] = {0};
            size_t klen = (size_t)(eq - kv);
            if (klen > 255)
                klen = 255;
            memcpy(key, kv, klen);
            while (klen > 0 && key[klen - 1] == ' ')
                key[--klen] = '\0';
            const char *val = eq + 1;
            while (*val == ' ')
                val++;
            memory_set(&g_memory, key, val);
            char mp[NC_PATH_MAX];
            config_find_path(mp, sizeof(mp), NC_CFG_MEMORY);
            memory_save(&g_memory, mp);
            term_color(T_DIM, "(remembered: %s = %s)\n", key, val);
        } else {
            term_color(T_RED, "Usage: /remember key = value\n");
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/doctor") == 0) {
        term_color(T_BOLD, "Running full diagnostics...\n\n");
        diagnostics_run();
        char *report = diagnostics_format();
        printf("%s", report);
        free(report);

        term_color(T_DIM, "\n  Provider:   %s\n", nc_provider_label(cfg->provider));
        term_color(T_DIM, "  Model:      %s\n", config_model(cfg));
        term_color(T_DIM, "  Base URL:   %s\n", config_base_url(cfg));
        term_color(T_DIM, "  API Key:    %s\n", config_api_key(cfg) ? "set" : "NOT SET");
        term_color(T_DIM, "  Tools:      %d registered\n", tools_count());
        term_color(T_DIM, "  Hooks:      %d loaded\n", g_hooks.num_hooks);
        term_color(T_DIM, "  Memories:   %d stored\n", g_memory.len);
        term_color(T_DIM, "  MCP:        %d server%s\n", g_num_mcp_servers,
                   g_num_mcp_servers != 1 ? "s" : "");
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/brief") == 0) {
        g_brief_mode = !g_brief_mode;
        term_color(T_DIM, "(brief mode %s)\n", g_brief_mode ? "ON — concise responses" : "OFF");
        return CMD_CONTINUE;
    }

    if (nc_starts_with(input, "/personality")) {
        const char *arg = input + 12;
        while (*arg == ' ')
            arg++;
        if (*arg == '\0') {
            char *list = personality_list();
            term_color(T_DIM, "Personalities:\n%s", list);
            free(list);
        } else {
            int id = personality_parse(arg);
            if (id < 0) {
                term_color(T_YELLOW, "Unknown personality '%s'. Use /personality to list.\n", arg);
            } else {
                personality_set(id);
                term_color(T_DIM, "(personality: %s)\n", personality_name(id));
            }
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/snip") == 0) {
        int n_snip = conv->len > 4 ? conv->len - 4 : 0;
        if (n_snip > 0) {
            int freed = context_snip_range(conv, 0, n_snip);
            int saved = context_estimate_freed_tokens(freed);
            term_color(T_DIM, "(snipped %d messages, freed ~%d tokens)\n", n_snip, saved);
        } else {
            term_color(T_DIM, "(nothing to snip)\n");
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/skills") == 0) {
        char *listing = NULL;
        skill_list_all(&listing);
        printf("%s", listing);
        free(listing);
        return CMD_CONTINUE;
    }

    if (skill_exists(input + 1)) {
        const char *prompt = skill_find_prompt(input + 1);
        if (prompt) {
            term_color(T_CYAN, "[skill: %s]\n", input + 1);
            conversation_add_user_text(conv, prompt);
            return CMD_API_CALL;
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/init") == 0) {
        const char *init_prompt =
            "Please analyze this codebase and create an AGENTS.md file.\n\n"
            "Steps:\n"
            "1. Read key files: package.json, Cargo.toml, pyproject.toml, "
            "go.mod, Makefile, README.md, existing AGENTS.md, .cursor/rules/\n"
            "2. Identify: build/test/lint commands, languages, frameworks, "
            "project structure, code style, gotchas\n"
            "3. Write a concise AGENTS.md at the project root with:\n"
            "   - Build/test/lint commands that differ from defaults\n"
            "   - Code style rules that differ from language defaults\n"
            "   - Testing instructions and quirks\n"
            "   - Required env vars or setup steps\n"
            "   - Non-obvious gotchas\n\n"
            "Do NOT include:\n"
            "- File-by-file structure lists\n"
            "- Standard conventions the AI already knows\n"
            "- Generic advice\n\n"
            "Prefix with:\n```\n# AGENTS.md\n\n"
            "This file provides guidance to AI coding agents when working "
            "with code in this repository.\n```";
        term_color(T_CYAN, "[/init] analyzing codebase...\n");
        conversation_add_user_text(conv, init_prompt);
        return CMD_API_CALL;
    }

    if (strcmp(input, "/scaffold") == 0 || nc_starts_with(input, "/scaffold ")) {
        const char *sc_arg = input + 9;
        while (*sc_arg == ' ')
            sc_arg++;
        const tool_def_t *sc_tool = tools_find("Scaffold");
        if (sc_tool) {
            cJSON *sc_in = cJSON_CreateObject();
            if (*sc_arg)
                cJSON_AddStringToObject(sc_in, "action", sc_arg);
            char *sc_out = NULL;
            sc_tool->execute(sc_in, &sc_out, NULL);
            if (sc_out) {
                term_color(T_CYAN, "%s\n", sc_out);
                free(sc_out);
            }
            cJSON_Delete(sc_in);
        } else {
            term_color(T_YELLOW, "Scaffold tool not registered.\n");
        }
        return CMD_CONTINUE;
    }

    if (nc_starts_with(input, "/review")) {
        const char *arg = input + 7;
        while (*arg == ' ')
            arg++;
        char *review_prompt;
        if (*arg) {
            strbuf_t rp;
            strbuf_init(&rp);
            if (arg[0] >= '0' && arg[0] <= '9') {
                strbuf_append_str(&rp, "First run `gh pr diff ");
                strbuf_append_str(&rp, arg);
                strbuf_append_str(&rp, "` to get the diff, then review.\n\n");
            } else {
                strbuf_append_str(&rp, arg);
                strbuf_append_str(&rp, "\n\n");
            }
            review_prompt = build_review_prompt(rp.data, NULL);
            strbuf_free(&rp);
        } else {
            review_prompt =
                build_review_prompt("Review the most recent changes (`git diff HEAD`).", NULL);
        }
        term_color(T_CYAN, "[/review] starting structured code review...\n");
        conversation_add_user_text(conv, review_prompt);
        free(review_prompt);
        return CMD_API_CALL;
    }

    if (nc_starts_with(input, "/security-review")) {
        const char *target = input + 16;
        while (*target == ' ')
            target++;
        char *sr_prompt = build_security_review_prompt(*target ? target : NULL);
        term_color(T_CYAN, "[/security-review] starting security audit...\n");
        conversation_add_user_text(conv, sr_prompt);
        free(sr_prompt);
        return CMD_API_CALL;
    }

    if (nc_starts_with(input, "/preset")) {
        const char *arg = input + 7;
        while (*arg == ' ')
            arg++;
        if (!*arg) {
            char *fmt = security_presets_format();
            term_color(T_BOLD, "%s", fmt);
            free(fmt);
        } else {
            const security_preset_t *p = security_preset_find(arg);
            if (p) {
                perm_set_approval(&g_perms, (approval_preset_t)p->approval_value);
                term_color(T_GREEN, "Security preset set: %s\n", p->label);
                term_color(T_DIM, "  %s\n", p->description);
            } else {
                term_color(T_RED, "Unknown preset: %s\n", arg);
                char *fmt = security_presets_format();
                term_color(T_BOLD, "%s", fmt);
                free(fmt);
            }
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/procs") == 0) {
        procgroup_reap(procgroup_global());
        procgroup_cleanup_exited(procgroup_global());
        int pc = procgroup_count(procgroup_global());
        if (pc == 0) {
            term_color(T_DIM, "(no tracked child processes)\n");
        } else {
            term_color(T_BOLD, "Tracked Processes (%d):\n", pc);
            char *fmt = procgroup_format(procgroup_global());
            printf("%s", fmt);
            free(fmt);
        }
        return CMD_CONTINUE;
    }

    if (nc_starts_with(input, "/diag")) {
        const char *arg = input + 5;
        while (*arg == ' ')
            arg++;
        if (!*arg) {
            term_color(T_DIM, "Usage: /diag <build command>\n"
                              "  Runs the command and parses compiler/linter output.\n"
                              "  Example: /diag make\n"
                              "           /diag gcc -c main.c\n"
                              "           /diag npm run lint\n");
        } else {
            term_color(T_DIM, "Running: %s\n", arg);
            FILE *fp = popen(arg, "r");
            if (!fp) {
                term_color(T_RED, "Failed to run: %s\n", arg);
            } else {
                strbuf_t out;
                strbuf_init(&out);
                char buf[NC_IO_BUF];
                while (fgets(buf, sizeof(buf), fp))
                    strbuf_append_str(&out, buf);
                int rc = pclose(fp);
                if (out.len == 0) {
                    term_color(T_GREEN, "Clean build (exit %d, no output)\n", rc);
                } else {
                    diagnostic_entry_t diags[MAX_DIAGNOSTICS];
                    int nd = diagnostic_parse_output(out.data, diags, MAX_DIAGNOSTICS);
                    if (nd > 0) {
                        diagnostic_annotate_suggestions(diags, nd);
                        char *summary = diagnostic_summary(diags, nd);
                        char *formatted = diagnostic_format(diags, nd);
                        term_color(T_BOLD, "Diagnostics: %s\n\n", summary);
                        printf("%s", formatted);
                        free(summary);
                        free(formatted);
                        diagnostics_free(diags, nd);
                    } else {
                        term_color(T_DIM, "No diagnostics parsed.\n");
                        if (out.len < SLASH_DIAG_OUTPUT_MAX)
                            printf("%s", out.data);
                    }
                }
                strbuf_free(&out);
            }
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/diff") == 0 || strcmp(input, "/diff session") == 0) {
        bool show_session = (strcmp(input, "/diff session") == 0);

        if (show_session) {
            term_color(T_BOLD, "Session file changes:\n\n");
            int nf = turn_diff_files_changed();
            int nb = turn_diff_baseline_count();
            term_color(T_DIM, "  %d file(s) tracked, %d changed\n\n", nb, nf);
            if (nf > 0) {
                char *udiff = turn_diff_unified_all();
                if (udiff && *udiff) {
                    const char *p = udiff;
                    while (*p) {
                        const char *nl = strchr(p, '\n');
                        size_t ll = nl ? (size_t)(nl - p + 1) : strlen(p);
                        if (*p == '+' && (ll < 2 || p[1] != '+'))
                            term_color(T_GREEN, "%.*s", (int)ll, p);
                        else if (*p == '-' && (ll < 2 || p[1] != '-'))
                            term_color(T_RED, "%.*s", (int)ll, p);
                        else if (ll >= 2 && p[0] == '@' && p[1] == '@')
                            term_color(T_CYAN, "%.*s", (int)ll, p);
                        else if (ll >= 4 && nc_starts_with(p, "--- "))
                            term_color(T_BOLD, "%.*s", (int)ll, p);
                        else if (ll >= 4 && nc_starts_with(p, "+++ "))
                            term_color(T_BOLD, "%.*s", (int)ll, p);
                        else
                            printf("%.*s", (int)ll, p);
                        p += ll;
                    }
                }
                free(udiff);
            }
            char *stats = turn_diff_format_all();
            if (stats) {
                term_color(T_DIM, "\n%s", stats);
                free(stats);
            }
        } else {
            term_color(T_BOLD, "Uncommitted changes:\n\n");
            char stat_buf[DIFF_OUTPUT_MAX_BYTES];
            int rc = git_diff_stat(".", stat_buf, sizeof(stat_buf));
            if (rc == 0 && stat_buf[0]) {
                char *lp = stat_buf;
                while (*lp) {
                    char *eol = strchr(lp, '\n');
                    if (eol)
                        *eol = '\0';
                    printf("  %s\n", lp);
                    if (!eol)
                        break;
                    lp = eol + 1;
                }
                printf("\n");
                char *diff_buf = malloc(DIFF_OUTPUT_MAX_BYTES);
                if (diff_buf) {
                    int drc = git_diff_output(".", diff_buf, DIFF_OUTPUT_MAX_BYTES);
                    if (drc == 0 && diff_buf[0]) {
                        char line[NC_LINE_MAX];
                        int lines_shown = 0;
                        const char *p = diff_buf;
                        while (*p && lines_shown < NC_DIFF_DISPLAY_MAX_LINES) {
                            const char *eol = strchr(p, '\n');
                            size_t len = eol ? (size_t)(eol - p) : strlen(p);
                            if (len >= sizeof(line))
                                len = sizeof(line) - 1;
                            memcpy(line, p, len);
                            line[len] = '\0';
                            if (line[0] == '+' && line[1] != '+')
                                term_color(T_GREEN, "%s\n", line);
                            else if (line[0] == '-' && line[1] != '-')
                                term_color(T_RED, "%s\n", line);
                            else if (nc_starts_with(line, "@@"))
                                term_color(T_CYAN, "%s\n", line);
                            else if (nc_starts_with(line, "diff "))
                                term_color(T_BOLD, "%s\n", line);
                            else
                                printf("%s\n", line);
                            lines_shown++;
                            p = eol ? eol + 1 : p + strlen(p);
                        }
                        if (lines_shown >= NC_DIFF_DISPLAY_MAX_LINES)
                            term_color(T_DIM, "\n... (truncated, first %d lines)\n",
                                       NC_DIFF_DISPLAY_MAX_LINES);
                    }
                    free(diff_buf);
                }
            } else if (rc == 0) {
                term_color(T_DIM, "  (no uncommitted changes)\n");
            } else {
                term_color(T_DIM, "  (not a git repository)\n");
            }
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/permissions") == 0) {
        term_color(T_BOLD, "Permission Rules:\n");
        if (g_perms.trust_all) {
            term_color(T_GREEN, "  Mode: trust all (--dangerously-skip-permissions)\n");
        } else if (g_perms.approval_preset == APPROVAL_READONLY) {
            term_color(T_YELLOW, "  Mode: read-only (--approval read-only)\n");
        } else {
            term_color(T_DIM, "  Mode: interactive confirmation\n");
        }
        if (g_perms.approval_preset != APPROVAL_DEFAULT)
            term_color(T_DIM, "  Approval preset: %s\n",
                       perm_approval_name(g_perms.approval_preset));
        if (g_perms.num_rules == 0) {
            term_color(T_DIM, "  (no custom rules)\n");
        } else {
            for (int ri = 0; ri < g_perms.num_rules; ri++) {
                const char *dec_str = g_perms.rules[ri].decision == PERM_ALLOW
                                          ? T_GREEN "allow" T_RESET
                                          : T_RED "deny" T_RESET;
                term_color(T_DIM, "  %s: %s", g_perms.rules[ri].tool_name, dec_str);
                if (g_perms.rules[ri].path_prefix)
                    term_color(T_DIM, " (path: %s*)", g_perms.rules[ri].path_prefix);
                printf("\n");
            }
        }
        term_color(T_DIM, "\n  Read-only tools (auto-allowed): ");
        int ro_count = 0;
        for (int ti = 0; ti < tools_count(); ti++) {
            const tool_def_t *all = tools_get_all();
            if (all[ti].read_only) {
                if (ro_count > 0)
                    printf(", ");
                printf("%s", all[ti].name);
                ro_count++;
            }
        }
        printf(" (%d tools)\n", ro_count);
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/status") == 0) {
        term_color(T_BOLD, "Session Status\n");
        term_color(T_DIM, "  Model:      %s\n", config_model(cfg));
        term_color(T_DIM, "  Provider:   %s\n", nc_provider_label(cfg->provider));
        term_color(T_DIM, "  Base URL:   %s\n", config_base_url(cfg));
        term_color(T_DIM, "  Messages:   %d\n", conv->len);
        int est = estimate_conversation_tokens(conv->msgs, conv->len, conv->system_prompt);
        int limit = context_limit_for_model(config_model(cfg));
        int pct = limit > 0 ? (est * 100 / limit) : 0;
        const char *bar_color = pct > 75 ? T_RED : pct > 50 ? T_YELLOW : T_GREEN;
        term_color(T_DIM, "  Context:    ~%dk / %dk tokens (%d%%)", est / 1000, limit / 1000, pct);
        term_color(bar_color, " [");
        int bar_w = SLASH_CTX_BAR_WIDTH, filled = pct * bar_w / 100;
        if (filled > bar_w)
            filled = bar_w;
        for (int bi = 0; bi < bar_w; bi++)
            fputc(bi < filled ? '#' : '.', stdout);
        term_color(bar_color, "]\n");
        term_color(T_DIM, "  Tokens in:  %d\n", g_total_input_tokens);
        term_color(T_DIM, "  Tokens out: %d\n", g_total_output_tokens);
        double cost = cost_tracker_total_cost();
        if (cost > 0)
            term_color(T_DIM, "  Cost:       $%.4f\n", cost);
        term_color(T_DIM, "  Tools:      %d registered\n", tools_count());
        term_color(T_DIM, "  Hooks:      %d loaded\n", g_hooks.num_hooks);
        term_color(T_DIM, "  Memories:   %d stored\n", g_memory.len);
        term_color(T_DIM, "  MCP:        %d server%s\n", g_num_mcp_servers,
                   g_num_mcp_servers != 1 ? "s" : "");
        term_color(T_DIM, "  Plan mode:  %s\n", g_plan_mode ? "ON" : "off");
        term_color(T_DIM, "  Brief mode: %s\n", g_brief_mode ? "ON" : "off");
        {
            int fatigue_pct = context_fatigue_pct();
            int fatigue_eta = context_fatigue_turns_remaining();
            if (fatigue_pct > 0)
                term_color(T_DIM, "  Fatigue:    %d%% (~%d turns left)\n", fatigue_pct,
                           fatigue_eta);
        }
        term_color(T_DIM, "  Watchdog:   %s (%d timeouts, longest %ldms)\n",
                   watchdog_timeout_count() > 0 ? "fired" : "ok", watchdog_timeout_count(),
                   watchdog_longest_turn_ms());
        {
            int stall_w = stall_detector_window_count();
            long stall_idle = stall_detector_idle_s();
            if (stall_w > 0)
                term_color(T_DIM, "  Stall:      %d turns tracked, idle %lds\n", stall_w,
                           stall_idle);
        }
        term_color(T_DIM, "  Session:    %s\n", *session_id);
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/stats") == 0) {
        char *stats = session_stats_format();
        if (stats) {
            printf("%s", stats);
            free(stats);
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/insights") == 0 || nc_starts_with(input, "/insights ")) {
        int days = INSIGHTS_DEFAULT_DAYS;
        if (strlen(input) > 10) {
            int d = nc_safe_atoi(input + 10, 0);
            if (d > 0 && d <= INSIGHTS_MAX_DAYS)
                days = d;
        }
        long cutoff = nc_wall_sec() - (long)days * 86400;

        char sess_dir[NC_PATH_MAX];
        config_home_path(sess_dir, sizeof(sess_dir), "sessions");
        DIR *dp = opendir(sess_dir);
        if (!dp) {
            term_color(T_DIM, "(no session data found)\n");
            return CMD_CONTINUE;
        }

        int total_sessions = 0, total_turns = 0, total_tools = 0;
        long total_in = 0, total_out = 0;
        double total_cost = 0.0;
        long total_dur = 0;

        typedef struct {
            char name[NC_TAG_MAX];
            int count;
            long in_tok;
            long out_tok;
            double cost;
        } model_bucket_t;
        model_bucket_t models[INSIGHTS_MAX_MODELS];
        int nmodels = 0;

        struct dirent *ent;
        while ((ent = readdir(dp)) != NULL) {
            if (ent->d_name[0] == '.')
                continue;
            char mpath[NC_PATH_MAX];
            snprintf(mpath, sizeof(mpath), "%.2038s/%.2038s/metadata.json", sess_dir, ent->d_name);

            size_t flen = 0;
            char *json_data = strbuf_read_file_max(mpath, &flen, NC_READ_CAP_JSON_SMALL);
            if (!json_data)
                continue;

            rollout_meta_t meta;
            if (rollout_meta_deserialize(&meta, json_data) != 0) {
                free(json_data);
                continue;
            }
            free(json_data);

            if (meta.started_at < cutoff)
                continue;

            total_sessions++;
            total_turns += meta.turn_count;
            total_tools += meta.tool_call_count;
            total_in += meta.input_tokens;
            total_out += meta.output_tokens;
            total_cost += meta.cost;
            total_dur += rollout_meta_duration_s(&meta);

            if (meta.model[0]) {
                int found = -1;
                for (int i = 0; i < nmodels; i++) {
                    if (strcmp(models[i].name, meta.model) == 0) {
                        found = i;
                        break;
                    }
                }
                if (found >= 0) {
                    models[found].count++;
                    models[found].in_tok += meta.input_tokens;
                    models[found].out_tok += meta.output_tokens;
                    models[found].cost += meta.cost;
                } else if (nmodels < INSIGHTS_MAX_MODELS) {
                    nc_strlcpy(models[nmodels].name, meta.model, NC_TAG_MAX);
                    models[nmodels].count = 1;
                    models[nmodels].in_tok = meta.input_tokens;
                    models[nmodels].out_tok = meta.output_tokens;
                    models[nmodels].cost = meta.cost;
                    nmodels++;
                }
            }
        }
        closedir(dp);

        if (total_sessions == 0) {
            term_color(T_DIM, "(no sessions in the last %d days)\n", days);
            return CMD_CONTINUE;
        }

        term_color(T_BOLD, "Usage Insights (%d day%s)\n", days, days > 1 ? "s" : "");
        term_color(T_DIM, "──────────────────────────────────────\n");
        printf("  Sessions:     %d\n", total_sessions);
        printf("  Turns:        %d (avg %.1f/session)\n", total_turns,
               total_sessions ? (double)total_turns / total_sessions : 0.0);
        printf("  Tool calls:   %d\n", total_tools);

        {
            char in_s[NC_FMT_MAX], out_s[NC_FMT_MAX];
            nc_fmt_tokens(in_s, sizeof(in_s), total_in);
            nc_fmt_tokens(out_s, sizeof(out_s), total_out);
            printf("  Input tokens:  %s\n", in_s);
            printf("  Output tokens: %s\n", out_s);
        }

        printf("  Cost:         $%.4f\n", total_cost);

        {
            char dur_s[NC_FMT_MAX];
            nc_fmt_elapsed_s(dur_s, sizeof(dur_s), (int)total_dur);
            printf("  Active time:  %s\n", dur_s);
        }

        if (nmodels > 0) {
            term_color(T_BOLD, "\nModel Breakdown\n");
            for (int i = 0; i < nmodels; i++) {
                printf("  %-28s %3d session%s  ", models[i].name, models[i].count,
                       models[i].count > 1 ? "s" : " ");
                char mi[NC_FMT_MAX], mo[NC_FMT_MAX];
                nc_fmt_tokens(mi, sizeof(mi), models[i].in_tok);
                nc_fmt_tokens(mo, sizeof(mo), models[i].out_tok);
                printf("%s in / %s out", mi, mo);
                if (models[i].cost > 0.0001)
                    printf("  $%.4f", models[i].cost);
                printf("\n");
            }
        }

        return CMD_CONTINUE;
    }

    if (strcmp(input, "/fallback") == 0) {
        char *fb = multi_provider_format();
        if (fb) {
            printf("%s", fb);
            free(fb);
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/suggest on") == 0) {
        prompt_suggest_set_enabled(true);
        term_color(T_GREEN, "Prompt suggestions enabled.\n");
        return CMD_CONTINUE;
    }
    if (strcmp(input, "/suggest off") == 0) {
        prompt_suggest_set_enabled(false);
        term_color(T_DIM, "Prompt suggestions disabled.\n");
        return CMD_CONTINUE;
    }
    if (strcmp(input, "/suggest") == 0) {
        if (!prompt_suggest_enabled()) {
            term_color(T_DIM, "Prompt suggestions: disabled\nEnable with: /suggest on\n");
        } else {
            const char *cur = prompt_suggest_current();
            if (cur)
                term_color(T_CYAN, "Suggestion: %s\n", cur);
            else
                term_color(T_DIM, "No suggestion available.\n");
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/away on") == 0) {
        away_summary_set_enabled(true);
        term_color(T_GREEN, "Away summary enabled.\n");
        return CMD_CONTINUE;
    }
    if (strcmp(input, "/away off") == 0) {
        away_summary_set_enabled(false);
        term_color(T_DIM, "Away summary disabled.\n");
        return CMD_CONTINUE;
    }
    if (strcmp(input, "/away") == 0) {
        term_color(T_DIM, "Away summary: %s\n", away_summary_enabled() ? "enabled" : "disabled");
        term_color(T_DIM, "Idle: %lds (trigger: 300s)\n", away_summary_idle_seconds());
        return CMD_CONTINUE;
    }

    if (nc_starts_with(input, "/effort ")) {
        effort_level_t lv = effort_level_parse(input + 8);
        effort_level_set(lv);
        cfg->effort = lv;
        term_color(T_GREEN, "Effort set to: %s\n", effort_level_name(lv));
        if (!effort_level_valid_for_model(lv, config_model(cfg)))
            term_color(T_YELLOW, "  (note: '%s' may not be supported by %s, will be clamped)\n",
                       effort_level_name(lv), config_model(cfg));
        return CMD_CONTINUE;
    }
    if (strcmp(input, "/effort") == 0) {
        char *desc = effort_level_describe();
        printf("%s\n", desc);
        free(desc);
        term_color(T_DIM, "  Levels: none|minimal|low|medium|high|xhigh|max|default\n");
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/context") == 0) {
        int ctx_limit = context_limit_for_model(config_model(cfg));
        int sp_tok = conv->system_prompt ? estimate_tokens(conv->system_prompt) : 0;

        ctx_viz_result_t viz;
        ctx_viz_analyze(&viz, conv->msgs, conv->len, ctx_limit, sp_tok);
        char *bar = ctx_viz_render(&viz);
        if (bar) {
            printf("%s", bar);
            free(bar);
        }

        int mem_tok = estimate_tokens(memory_format_for_prompt(&g_memory));
        ctx_analysis_t ca;
        ctx_analysis_from_conversation(&ca, conv, ctx_limit, mem_tok, true);
        ctx_suggestion_t suggestions[8];
        int ns = ctx_suggest_analyze(&ca, suggestions, 8);
        if (ns > 0) {
            char *fmt = ctx_suggest_format(suggestions, ns);
            if (fmt) {
                term_color(T_YELLOW, "\nSuggestions:\n%s", fmt);
                free(fmt);
            }
            ctx_suggestions_free(suggestions, ns);
        }

        /* Detailed token distribution breakdown */
        ctx_token_stats_t tstat;
        ctx_token_stats_analyze(&tstat, conv->msgs, conv->len);
        char *tfmt = ctx_token_stats_format(&tstat);
        if (tfmt) {
            term_color(T_BOLD, "\n");
            printf("%s", tfmt);
            free(tfmt);
        }

        return CMD_CONTINUE;
    }

    if (strcmp(input, "/session-memory") == 0 || strcmp(input, "/sm") == 0) {
        if (!session_memory_enabled()) {
            term_color(T_DIM, "Session memory: disabled\n");
        } else {
            const char *sm = session_memory_content();
            if (sm && sm[0]) {
                term_color(T_BOLD, "Session Memory (%d extraction%s):\n\n",
                           session_memory_extraction_count(),
                           session_memory_extraction_count() != 1 ? "s" : "");
                printf("%s\n", sm);
            } else {
                term_color(
                    T_DIM,
                    "(no session memory yet — extracted automatically after %d+ tool calls)\n", 2);
            }
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/sm on") == 0) {
        session_memory_set_enabled(true);
        term_color(T_GREEN, "Session memory enabled.\n");
        return CMD_CONTINUE;
    }
    if (strcmp(input, "/sm off") == 0) {
        session_memory_set_enabled(false);
        term_color(T_DIM, "Session memory disabled.\n");
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/version") == 0) {
        printf("%s\n", NC_VERSION_DISPLAY);
        printf("Build: pure C, libcurl + cJSON\n");
        printf("License: open source, no data collection\n");
        char *caps = model_caps_describe(config_model(cfg));
        if (caps) {
            printf("%s\n", caps);
            free(caps);
        }
        return CMD_CONTINUE;
    }

    if (nc_starts_with(input, "/doc")) {
        const char *arg = input + 4;
        while (*arg == ' ')
            arg++;
        strbuf_t dp;
        strbuf_init(&dp);
        strbuf_append_str(
            &dp, "Analyze this project and generate a comprehensive technical document.\n\n"
                 "Steps:\n"
                 "1. Scan the project structure using ListDir and Glob\n"
                 "2. Read key files: README.md, Makefile, package.json, Cargo.toml, etc.\n"
                 "3. Analyze the architecture, modules, and dependencies\n"
                 "4. Generate a document with:\n"
                 "   - Project overview and purpose\n"
                 "   - Architecture diagram (in ASCII/mermaid)\n"
                 "   - Module responsibilities\n"
                 "   - Build/test/deploy commands\n"
                 "   - Key data structures and interfaces\n"
                 "   - Development workflow\n\n");
        if (*arg) {
            strbuf_append_str(&dp, "Focus on: ");
            strbuf_append_str(&dp, arg);
            strbuf_append_str(&dp, "\n");
        }
        term_color(T_CYAN, "[/doc] generating project documentation...\n");
        conversation_add_user_text(conv, dp.data);
        strbuf_free(&dp);
        return CMD_API_CALL;
    }

    if (nc_starts_with(input, "/search ")) {
        const char *query = input + 8;
        while (*query == ' ')
            query++;
        if (*query) {
            char *listing = NULL;
            int n = history_search_fuzzy(query, &listing);
            if (n > 0) {
                term_color(T_BOLD, "Search results for \"%s\" (%d matches):\n", query, n);
                printf("%s", listing);
                free(listing);
                term_color(T_DIM, "Resume with: /resume <session-id>\n");
            } else {
                term_color(T_DIM, "No sessions matching \"%s\"\n", query);
            }
        } else {
            term_color(T_DIM, "Usage: /search <query>\n");
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/routing") == 0) {
        if (model_router_enabled()) {
            char *tiers = model_router_format_tiers();
            printf("%s", tiers);
            free(tiers);
        } else {
            term_color(T_DIM,
                       "Model routing: disabled (single model: %s)\n"
                       "Enable via settings.json model_routing.tiers\n",
                       config_model(cfg));
        }
        return CMD_CONTINUE;
    }

    if (nc_starts_with(input, "/share")) {
        char share_path[NC_PATH_MAX];
        const char *arg = input + 6;
        while (*arg == ' ')
            arg++;
        if (*arg) {
            nc_strlcpy(share_path, arg, sizeof(share_path));
        } else {
            time_t now = nc_time_now();
            struct tm *t = localtime(&now);
            snprintf(share_path, sizeof(share_path),
                     NC_SESSION_FILE_PREFIX "%04d%02d%02d-%02d%02d%02d.md", t->tm_year + 1900,
                     t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
        }
        strbuf_t md;
        strbuf_init(&md);
        strbuf_append_str(&md, "# Nevercode Session\n\n> Model: ");
        strbuf_append_str(&md, config_model(cfg));
        strbuf_append_str(&md, "  \n> Session: ");
        strbuf_append_str(&md, *session_id);
        strbuf_append_str(&md, "\n\n---\n\n");
        for (int i = 0; i < conv->len; i++) {
            const message_t *m = &conv->msgs[i];
            if (m->role == ROLE_USER)
                strbuf_append_str(&md, "## User\n\n");
            else if (m->role == ROLE_ASSISTANT)
                strbuf_append_str(&md, "## Assistant\n\n");
            else if (m->role == ROLE_TOOL)
                strbuf_append_str(&md, "## Tool\n\n");
            else
                continue;
            for (int j = 0; j < m->num_blocks; j++) {
                const content_block_t *b = &m->blocks[j];
                if (b->type == CONTENT_TEXT && b->text) {
                    strbuf_append_str(&md, b->text);
                    strbuf_append_str(&md, "\n\n");
                } else if (b->type == CONTENT_TOOL_USE && b->tool_name) {
                    strbuf_append_str(&md, "**Tool: ");
                    strbuf_append_str(&md, b->tool_name);
                    strbuf_append_str(&md, "**\n\n");
                } else if (b->type == CONTENT_TOOL_RESULT && b->text) {
                    strbuf_append_str(&md, "<details>\n<summary>Result</summary>\n\n```\n");
                    size_t tl = strlen(b->text);
                    if (tl > SLASH_SHARE_RESULT_MAX) {
                        strbuf_append(&md, b->text, SLASH_SHARE_RESULT_MAX);
                        strbuf_append_str(&md, "\n... (truncated)\n");
                    } else {
                        strbuf_append_str(&md, b->text);
                    }
                    strbuf_append_str(&md, "\n```\n</details>\n\n");
                }
            }
        }
        FILE *sf = fopen(share_path, "w");
        if (sf) {
            bool ok = (md.len == 0 || fwrite(md.data, 1, md.len, sf) == md.len) && !fflush(sf);
            fclose(sf);
            if (ok)
                term_color(T_GREEN, "Session exported to: %s\n", share_path);
            else
                term_color(T_RED, "Write error: %s\n", share_path);
        } else {
            term_color(T_RED, "Failed to write: %s\n", share_path);
        }
        strbuf_free(&md);
        return CMD_CONTINUE;
    }

    if (nc_starts_with(input, "/rewind")) {
        const char *arg = input + 7;
        while (*arg == ' ')
            arg++;
        if (*arg == '\0') {
            if (conv_rewind_count() > 0) {
                term_color(T_BOLD, "Conversation Checkpoints:\n");
                char *clist = conv_rewind_list();
                printf("%s", clist);
                free(clist);
            }
            char *checkpoints = file_history_format_checkpoints();
            term_color(T_BOLD, "File Checkpoints:\n");
            printf("%s", checkpoints);
            if (file_history_count() > 0 || conv_rewind_count() > 0)
                term_color(T_DIM, "Use /rewind <turn> to restore files, "
                                  "/rewind conv <n> to restore conversation\n");
            free(checkpoints);
        } else if (nc_starts_with(arg, "conv")) {
            const char *n = arg + 4;
            while (*n == ' ')
                n++;
            int idx = nc_safe_atoi(n, -1);
            char *snap = conv_rewind_apply(idx);
            if (snap) {
                term_color(T_GREEN, "Conversation rewound to checkpoint %d\n", idx);
                free(snap);
            } else {
                term_color(T_RED, "Invalid checkpoint: %s (have %d)\n", n, conv_rewind_count());
            }
        } else {
            int target = nc_safe_atoi(arg, 0);
            if (target <= 0) {
                term_color(T_RED, "Invalid turn number: %s\n", arg);
            } else {
                if (restore_guard_rewind_blocked(target))
                    return CMD_CONTINUE;
                int restored = 0, errors = 0;
                file_history_restore_turn(target, &restored, &errors);
                if (restored > 0)
                    term_color(T_GREEN, "Restored %d file%s to turn %d\n", restored,
                               restored > 1 ? "s" : "", target);
                if (errors > 0)
                    term_color(T_YELLOW, "(%d file%s could not be restored)\n", errors,
                               errors > 1 ? "s" : "");
                if (restored == 0 && errors == 0)
                    term_color(T_DIM, "No files found at turn %d\n", target);
            }
        }
        return CMD_CONTINUE;
    }

    if (nc_starts_with(input, "/rename")) {
        const char *name = input + 7;
        while (*name == ' ')
            name++;
        if (*name == '\0') {
            term_color(T_DIM, "Usage: /rename <session name>\n");
        } else {
            if (history_rename(*session_id, name) == 0) {
                term_color(T_GREEN, "Session renamed to: %s\n", name);
            } else {
                term_color(T_YELLOW, "Could not rename (save session first)\n");
                history_save(conv, *session_id);
                if (history_rename(*session_id, name) == 0)
                    term_color(T_GREEN, "Session renamed to: %s\n", name);
                else
                    term_color(T_RED, "Failed to rename session\n");
            }
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/files") == 0) {
        char *listing = files_in_context(&g_read_tracker);
        printf("%s", listing);
        free(listing);
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/pr") == 0) {
        char *prompt = build_pr_prompt();
        term_color(T_CYAN, "[/pr] preparing pull request...\n");
        conversation_add_user_text(conv, prompt);
        free(prompt);
        return CMD_API_CALL;
    }

    if (nc_starts_with(input, "/mcp")) {
        const char *sub = input + 4;
        while (*sub == ' ')
            sub++;
        char mcp_path[NC_PATH_MAX] = {0};
        config_find_path(mcp_path, sizeof(mcp_path), NC_CFG_MCP);
        if (*sub == '\0' || nc_starts_with(sub, "list")) {
            char *listing = mcp_format_server_list(mcp_path);
            printf("%s", listing);
            free(listing);
        } else if (nc_starts_with(sub, "add ")) {
            const char *args = sub + 4;
            while (*args == ' ')
                args++;
            const char *sep = strstr(args, " -- ");
            if (!sep) {
                term_color(T_RED, "Usage: /mcp add <name> -- <command>\n");
            } else {
                char name[NC_TAG_MAX] = {0};
                size_t nlen = (size_t)(sep - args);
                if (nlen >= sizeof(name))
                    nlen = sizeof(name) - 1;
                memcpy(name, args, nlen);
                while (nlen > 0 && name[nlen - 1] == ' ')
                    name[--nlen] = '\0';
                const char *cmd = sep + 4;
                while (*cmd == ' ')
                    cmd++;
                int rc = mcp_add_server_to_config(mcp_path, name, cmd);
                if (rc == 0)
                    term_color(T_GREEN, "Added MCP server: %s\nRestart nevercode to activate.\n",
                               name);
                else if (rc == -2)
                    term_color(T_YELLOW, "Server '%s' already exists.\n", name);
                else
                    term_color(T_RED, "Failed to add server.\n");
            }
        } else if (nc_starts_with(sub, "remove ")) {
            const char *name = sub + 7;
            while (*name == ' ')
                name++;
            if (mcp_remove_server_from_config(mcp_path, name) == 0)
                term_color(T_GREEN, "Removed MCP server: %s\n", name);
            else
                term_color(T_YELLOW, "Server '%s' not found.\n", name);
        } else {
            term_color(T_DIM, "Usage: /mcp [list|add <name> -- <cmd>|remove <name>]\n");
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/commit") == 0) {
        strbuf_t ctx;
        strbuf_init(&ctx);
        strbuf_append_str(&ctx, "## Context\n\n");
        char gbuf[GIT_OPS_OUTPUT_BUF * 4];
        if (git_status_short(NULL, gbuf, sizeof(gbuf)) == 0 && gbuf[0]) {
            strbuf_append_str(&ctx, "### git status\n```\n");
            strbuf_append_str(&ctx, gbuf);
            strbuf_append_str(&ctx, "\n```\n\n");
        }
        if (git_diff_output(NULL, gbuf, sizeof(gbuf)) == 0 && gbuf[0]) {
            strbuf_append_str(&ctx, "### git diff HEAD\n```diff\n");
            strbuf_append_str(&ctx, gbuf);
            strbuf_append_str(&ctx, "\n```\n\n");
        }
        char branch_buf[NC_LABEL_MAX];
        if (git_current_branch(NULL, branch_buf, sizeof(branch_buf)) == 0 && branch_buf[0]) {
            strbuf_append_str(&ctx, "Branch: ");
            strbuf_append_str(&ctx, branch_buf);
            strbuf_append_str(&ctx, "\n\n");
        }
        char log_buf[GIT_OPS_OUTPUT_BUF * 2];
        if (git_log_oneline(NULL, 10, log_buf, sizeof(log_buf)) == 0 && log_buf[0]) {
            strbuf_append_str(&ctx, "### Recent commits\n```\n");
            strbuf_append_str(&ctx, log_buf);
            strbuf_append_str(&ctx, "\n```\n\n");
        }
        strbuf_append_str(&ctx, "## Git Safety Protocol\n\n"
                                "- NEVER update the git config\n"
                                "- NEVER skip hooks (--no-verify, --no-gpg-sign)\n"
                                "- ALWAYS create NEW commits. NEVER use --amend\n"
                                "- Do not commit files with secrets (.env, credentials)\n"
                                "- Never use git -i flags (interactive mode)\n\n");
        char *attr_inst = commit_attr_instruction();
        if (attr_inst) {
            strbuf_append_str(&ctx, "## Commit Attribution\n\n");
            strbuf_append_str(&ctx, attr_inst);
            strbuf_append_str(&ctx, "\n\n");
            free(attr_inst);
        }
        strbuf_append_str(&ctx,
                          "## Task\n\nBased on the changes above, create a single git commit:\n"
                          "1. Look at recent commits to follow the repo's style\n"
                          "2. Draft a concise commit message (WHY > WHAT)\n"
                          "3. Stage relevant files with `git add`\n"
                          "4. Commit using: git commit -m \"message\"\n\n"
                          "Do not push. Do not use any other tools.");
        term_color(T_CYAN, "[/commit] preparing commit...\n");
        conversation_add_user_text(conv, ctx.data);
        strbuf_free(&ctx);
        return CMD_API_CALL;
    }

    if (strcmp(input, "/timing") == 0) {
        char *report = turn_timing_format();
        printf("%s", report);
        free(report);
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/env") == 0) {
        struct utsname un;
        term_color(T_BOLD, "Environment\n");
        if (uname(&un) == 0)
            term_color(T_DIM, "  OS:        %s %s %s\n", un.sysname, un.release, un.machine);
        term_color(T_DIM, "  Shell:     %s (%s)\n", shell_type_name(shell_detected()),
                   shell_detected_path());
        const char *usr = nc_user();
        if (usr)
            term_color(T_DIM, "  User:      %s\n", usr);
        const char *tm_var = nc_term_env();
        if (tm_var)
            term_color(T_DIM, "  Terminal:  %s\n", tm_var);
        time_t env_now = nc_time_now();
        struct tm *env_tm = localtime(&env_now);
        char env_timebuf[NC_FMT_MAX];
        strftime(env_timebuf, sizeof(env_timebuf), "%Y-%m-%d %H:%M %Z", env_tm);
        term_color(T_DIM, "  Date:      %s\n", env_timebuf);
        char env_cwd[NC_PATH_MAX];
        if (getcwd(env_cwd, sizeof(env_cwd)))
            term_color(T_DIM, "  CWD:       %s\n", env_cwd);
        const char *pr = project_root_detect(NULL);
        if (pr) {
            const char *mk = project_root_primary_marker(pr);
            term_color(T_DIM, "  Project:   %s%s%s%s\n", pr, mk ? " (" : "", mk ? mk : "",
                       mk ? ")" : "");
        }
        const char *sr = sandbox_root();
        if (sr) {
            const char *enforce = sandbox_enforcing() ? "enforcing" : "off";
            const char *os_type =
                sandbox_enforcing() && os_sandbox_available() ? os_sandbox_type_name() : NULL;
            if (os_type)
                term_color(T_DIM, "  Sandbox:   %s (%s, %s)\n", sr, enforce, os_type);
            else
                term_color(T_DIM, "  Sandbox:   %s (%s)\n", sr, enforce);
        }
        if (project_root_is_git()) {
            char gbuf[NC_LABEL_MAX] = {0};
            if (git_current_branch(NULL, gbuf, sizeof(gbuf)) == 0 && gbuf[0])
                term_color(T_DIM, "  Git:       %s\n", gbuf);
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/network") == 0) {
        network_policy_t *np = network_policy_global();
        if (np->num_rules == 0) {
            term_color(T_DIM, "(no network rules — all domains allowed)\n");
        } else {
            term_color(T_BOLD, "Network Policy\n");
            char *nf = network_policy_format(np);
            if (nf) {
                printf("%s", nf);
                free(nf);
            }
        }
        return CMD_CONTINUE;
    }

    if (nc_starts_with(input, "/branch")) {
        const char *sub = input + 7;
        while (*sub == ' ')
            sub++;
        if (strcmp(sub, "list") == 0 || strcmp(sub, "ls") == 0) {
            char *list = conv_branch_format_list();
            term_color(T_BOLD, "Branches:\n");
            printf("%s", list);
            free(list);
            return CMD_CONTINUE;
        }
        if (conv->len == 0) {
            term_color(T_DIM, "(nothing to branch)\n");
            return CMD_CONTINUE;
        }
        char *new_sid = history_gen_session_id();
        history_save(conv, new_sid);
        conv_branch_create(new_sid, *sub ? sub : NULL, conv->len);
        int branch_idx = conv_branch_count() - 1;
        const char *snapshot_ref = conv_branch_snapshot_ref(branch_idx);
        if (snapshot_ref && history_attach_file_snapshot(new_sid, snapshot_ref) != 0)
            term_color(T_YELLOW, "(warning: failed to persist file snapshot metadata)\n");
        if (*sub) {
            history_rename(new_sid, sub);
            bool snap = conv_branch_has_snapshot(branch_idx);
            term_color(T_GREEN,
                       "Conversation branched as '%s' (session: %s)%s\n"
                       "Resume with: nevercode --resume %s\n",
                       sub, new_sid, snap ? " [+file snapshot]" : "", new_sid);
        } else {
            bool snap = conv_branch_has_snapshot(branch_idx);
            term_color(T_GREEN,
                       "Conversation branched (session: %s)%s\n"
                       "Resume with: nevercode --resume %s\n",
                       new_sid, snap ? " [+file snapshot]" : "", new_sid);
        }
        free(new_sid);
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/agents") == 0) {
        char *listing = NULL;
        agent_pool_list(agent_pool_global(), &listing);
        printf("%s", listing);
        free(listing);
        return CMD_CONTINUE;
    }

    if (custom_skill_find(input + 1)) {
        const char *prompt = custom_skill_find(input + 1);
        term_color(T_CYAN, "[skill: %s]\n", input + 1);
        conversation_add_user_text(conv, prompt);
        return CMD_API_CALL;
    }

    if (nc_starts_with(input, "/copy")) {
        int age = 0;
        const char *copy_arg = input + 5;
        while (*copy_arg == ' ')
            copy_arg++;
        if (*copy_arg)
            age = nc_safe_atoi(copy_arg, 0) - 1;
        if (age < 0)
            age = 0;
        const char *asst_text = NULL;
        int found = 0;
        for (int i = conv->len - 1; i >= 0 && found <= age; i--) {
            if (conv->msgs[i].role == ROLE_ASSISTANT) {
                for (int j = 0; j < conv->msgs[i].num_blocks; j++) {
                    if (conv->msgs[i].blocks[j].type == CONTENT_TEXT &&
                        conv->msgs[i].blocks[j].text) {
                        if (found == age) {
                            asst_text = conv->msgs[i].blocks[j].text;
                            break;
                        }
                        found++;
                    }
                }
                if (asst_text)
                    break;
            }
        }
        if (!asst_text) {
            term_color(T_DIM, "(no assistant message to copy)\n");
        } else {
            size_t alen = strlen(asst_text);
            if (clipboard_write_text(asst_text, alen)) {
                int lines = 1;
                for (const char *p = asst_text; *p; p++)
                    if (*p == '\n')
                        lines++;
                term_color(T_DIM, "(copied %zu chars, %d lines to clipboard)\n", alen, lines);
            } else {
                char tmp_path[NC_LABEL_MAX];
                snprintf(tmp_path, sizeof(tmp_path), "/tmp/nevercode-copy.md");
                FILE *tf = fopen(tmp_path, "w");
                if (tf) {
                    bool wok = (alen == 0 || fwrite(asst_text, 1, alen, tf) == alen) && !fflush(tf);
                    fclose(tf);
                    if (!wok)
                        term_color(T_RED, "Write error: %s\n", tmp_path);
                    else
                        term_color(T_DIM, "(clipboard unavailable, written to %s)\n", tmp_path);
                }
            }
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/paste") == 0) {
        if (!clipboard_available()) {
            term_color(T_RED, "Clipboard not available on this system\n");
        } else {
            char *text = clipboard_read_text();
            if (text && *text) {
                int lines = 1;
                for (const char *p = text; *p; p++)
                    if (*p == '\n')
                        lines++;
                term_color(T_DIM, "(pasted %zu chars, %d lines from clipboard)\n", strlen(text),
                           lines);
                printf("%s\n", text);
            } else {
                term_color(T_DIM, "(clipboard is empty)\n");
            }
            free(text);
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/keys") == 0) {
        char *hints = key_hint_render_plain(KHINT_CTX_IDLE);
        term_color(T_BOLD, "Keyboard Shortcuts:\n");
        printf("  %s\n", hints);
        free(hints);
        hints = key_hint_render_plain(KHINT_CTX_STREAMING);
        term_color(T_BOLD, "During Streaming:\n");
        printf("  %s\n", hints);
        free(hints);
        hints = key_hint_render_plain(KHINT_CTX_VIM);
        term_color(T_BOLD, "Vim Mode:\n");
        printf("  %s\n", hints);
        free(hints);
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/summary") == 0) {
        if (conv->len == 0) {
            term_color(T_DIM, "(nothing to summarize)\n");
        } else {
            term_color(T_BOLD, "Session Summary\n");
            int user_msgs = 0, asst_msgs = 0, tool_calls = 0;
            for (int i = 0; i < conv->len; i++) {
                if (conv->msgs[i].role == ROLE_USER)
                    user_msgs++;
                else if (conv->msgs[i].role == ROLE_ASSISTANT)
                    asst_msgs++;
                else if (conv->msgs[i].role == ROLE_TOOL)
                    tool_calls++;
            }
            term_color(T_DIM, "  Turns:       %d user, %d assistant, %d tool\n", user_msgs,
                       asst_msgs, tool_calls);
            term_color(T_DIM, "  Tokens:      %d in, %d out\n", g_total_input_tokens,
                       g_total_output_tokens);
            for (int i = 0; i < conv->len; i++) {
                if (conv->msgs[i].role == ROLE_USER && conv->msgs[i].num_blocks > 0 &&
                    conv->msgs[i].blocks[0].type == CONTENT_TEXT && conv->msgs[i].blocks[0].text) {
                    char topic[NC_TAG_MAX];
                    snprintf(topic, sizeof(topic), "%.*s", SLASH_STATUS_TOPIC_MAX,
                             conv->msgs[i].blocks[0].text);
                    char *nl = strchr(topic, '\n');
                    if (nl)
                        *nl = '\0';
                    term_color(T_DIM, "  Topic:       %s%s\n", topic,
                               strlen(conv->msgs[i].blocks[0].text) > SLASH_STATUS_TOPIC_MAX ? "..."
                                                                                             : "");
                    break;
                }
            }
            for (int i = conv->len - 1; i >= 0; i--) {
                if (conv->msgs[i].role == ROLE_ASSISTANT) {
                    for (int j = 0; j < conv->msgs[i].num_blocks; j++) {
                        if (conv->msgs[i].blocks[j].type == CONTENT_TEXT &&
                            conv->msgs[i].blocks[j].text) {
                            char last[NC_TAG_MAX];
                            snprintf(last, sizeof(last), "%.*s", SLASH_STATUS_REPLY_MAX,
                                     conv->msgs[i].blocks[j].text);
                            char *nl = strchr(last, '\n');
                            if (nl)
                                *nl = '\0';
                            term_color(T_DIM, "  Last reply:  %s%s\n", last,
                                       strlen(conv->msgs[i].blocks[j].text) > SLASH_STATUS_REPLY_MAX
                                           ? "..."
                                           : "");
                            break;
                        }
                    }
                    break;
                }
            }
        }
        return CMD_CONTINUE;
    }

    if (nc_starts_with(input, "/fast")) {
        const char *fast_arg = input + 5;
        while (*fast_arg == ' ')
            fast_arg++;
        static bool fast_mode = false;
        if (*fast_arg == '\0')
            fast_mode = !fast_mode;
        else if (strcmp(fast_arg, "on") == 0)
            fast_mode = true;
        else if (strcmp(fast_arg, "off") == 0)
            fast_mode = false;
        if (fast_mode) {
            cfg->max_tokens = FAST_MODE_MAX_TOKENS;
            g_brief_mode = true;
            term_color(T_DIM, "(fast mode ON — max_tokens=%d, brief=on)\n", FAST_MODE_MAX_TOKENS);
        } else {
            cfg->max_tokens = DEFAULT_MAX_TOKENS;
            g_brief_mode = false;
            term_color(T_DIM, "(fast mode OFF — max_tokens=%d, brief=off)\n", DEFAULT_MAX_TOKENS);
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/tips") == 0) {
        term_color(T_BOLD, "Tips & Tricks\n\n");
        int total = tips_total();
        for (int i = 0; i < total; i++) {
            const char *tip = tips_get_by_index(i);
            if (tip) {
                term_color(T_DIM, "  %2d. ", i + 1);
                printf("%s\n", tip);
            }
        }
        printf("\n");
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/vim") == 0 || strcmp(input, "/vim toggle") == 0) {
        bool on = !vim_mode_enabled();
        vim_mode_set_enabled(on);
        term_color(on ? T_GREEN : T_DIM, "Vim mode %s\n", on ? "enabled" : "disabled");
        return CMD_CONTINUE;
    }
    if (strcmp(input, "/vim on") == 0) {
        vim_mode_init();
        vim_mode_set_enabled(true);
        term_color(T_GREEN, "Vim mode enabled\n");
        return CMD_CONTINUE;
    }
    if (strcmp(input, "/vim off") == 0) {
        vim_mode_set_enabled(false);
        term_color(T_DIM, "Vim mode disabled\n");
        return CMD_CONTINUE;
    }

    if (nc_starts_with(input, "/output-style ")) {
        const char *name = input + 14;
        while (*name == ' ')
            name++;
        output_style_init();
        term_color(T_CYAN, "Output style: %s\n", name);
        return CMD_CONTINUE;
    }
    if (strcmp(input, "/output-style") == 0) {
        output_style_init();
        char *s = output_style_format();
        if (s) {
            printf("%s\n", s);
            free(s);
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/keybindings") == 0 || strcmp(input, "/kb") == 0) {
        keybindings_init();
        char *s = keybindings_format();
        if (s) {
            printf("%s\n", s);
            free(s);
        }
        keybindings_free();
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/tasks") == 0) {
        char *s = task_types_format();
        if (s) {
            printf("%s\n", s);
            free(s);
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/hooks") == 0) {
        term_color(T_BOLD, "Hooks\n");
        printf("  (use config to manage hooks)\n\n");
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/session") == 0) {
        term_color(T_BOLD, "Session\n");
        const char *sid = state_session_id();
        printf("  ID:      %s\n", sid ? sid : "(none)");
        printf("  Phase:   %s\n", state_session_phase_name(state_session_phase()));
        printf("  Turn:    %d\n", state_session_turn_number());
        printf("  Uptime:  %ldms\n", state_session_uptime_ms());
        printf("  Idle:    %ldms\n", state_session_idle_ms());
        char *tm = turn_meta_format_summary();
        if (tm) {
            printf("  %s", tm);
            free(tm);
        }
        printf("\n");
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/bridge") == 0) {
        remote_bridge_init();
        char *s = remote_bridge_format_all();
        if (s) {
            printf("%s\n", s);
            free(s);
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/coordinator") == 0 || strcmp(input, "/coord") == 0) {
        char *s = coordinator_mode_format();
        if (s) {
            printf("%s\n", s);
            free(s);
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/harness") == 0) {
        harness_ctx_t *hg = harness_global();
        term_color(T_BOLD, "Long-running harness\n");
        char *ds = daemon_format_status(&hg->daemon);
        if (ds) {
            printf("%s", ds);
            free(ds);
        }
        char *cf = context_fatigue_format();
        if (cf) {
            printf("%s", cf);
            free(cf);
        }
        term_color(T_DIM, "  Stall window: %d turns, idle: %lds\n", stall_detector_window_count(),
                   stall_detector_idle_s());
        {
            char *ws = watchdog_format_status();
            if (ws) {
                term_color(T_BOLD, "Watchdog\n");
                printf("%s", ws);
                free(ws);
            }
        }
        term_color(T_DIM, "  Context: %d%%  Handoff needed: %s  Retries: %d\n", hg->context_pct,
                   hg->needs_handoff ? "yes" : "no", hg->handoff_retries);
        if (progress_tracker_count() > 0) {
            char *ps = progress_tracker_format();
            if (ps) {
                printf("%s", ps);
                free(ps);
            }
        }
        {
            char *tvs = tool_verify_format_stats();
            if (tvs) {
                term_color(T_DIM, "  %s\n", tvs);
                free(tvs);
            }
        }
        {
            char *cps = checkpoint_discipline_format();
            if (cps) {
                term_color(T_DIM, "  %s\n", cps);
                free(cps);
            }
        }
        if (hypothesis_debug_session_count() > 0) {
            char *hs = hypothesis_debug_format_all();
            if (hs) {
                printf("%s", hs);
                free(hs);
            }
        }
        if (episode_memory_count() > 0) {
            char *es = episode_memory_format();
            if (es) {
                printf("%s", es);
                free(es);
            }
        }
        if (feature_list_count() > 0) {
            char *fl = feature_list_format();
            if (fl) {
                printf("%s", fl);
                free(fl);
            }
        }
        {
            char *oes = output_eval_format_stats();
            if (oes) {
                term_color(T_DIM, "  %s\n", oes);
                free(oes);
            }
        }
        {
            char *ats = auto_test_format_stats();
            if (ats) {
                term_color(T_DIM, "  %s\n", ats);
                free(ats);
            }
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/eval") == 0) {
        char *s = output_eval_format_stats();
        if (s) {
            printf("%s\n", s);
            free(s);
        } else
            printf("No evaluations recorded yet.\n");
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/autotest") == 0) {
        char *s = auto_test_format_stats();
        if (s) {
            printf("%s\n", s);
            free(s);
        }
        printf("Enabled: %s\n", auto_test_is_enabled() ? "yes" : "no");
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/episodes") == 0) {
        char *s = episode_memory_format();
        if (s) {
            printf("%s\n", s);
            free(s);
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/features") == 0) {
        if (feature_list_count() == 0) {
            printf("No feature list loaded. Create %s in the project root.\n", FLIST_FILE);
        } else {
            char *s = feature_list_format();
            if (s) {
                printf("%s\n", s);
                free(s);
            }
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/hypothesis") == 0 || strcmp(input, "/hyp") == 0) {
        if (hypothesis_debug_session_count() == 0) {
            printf("No active debugging sessions.\n");
        } else {
            char *s = hypothesis_debug_format_all();
            if (s) {
                printf("%s\n", s);
                free(s);
            }
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/realtime") == 0) {
        char *s = realtime_conv_format();
        if (s) {
            printf("%s\n", s);
            free(s);
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/health") == 0) {
        int ctx_est = estimate_conversation_tokens(conv->msgs, conv->len, conv->system_prompt);
        int ctx_lim = context_limit_for_model(config_model(cfg));
        int ctx_pct = ctx_lim > 0 ? ctx_est * 100 / ctx_lim : 0;

        term_color(T_BOLD, "Session Health Dashboard\n");
        term_separator();

        /* Context usage bar */
        {
            int filled = ctx_pct * DASHBOARD_BAR_WIDTH / 100;
            if (filled > DASHBOARD_BAR_WIDTH)
                filled = DASHBOARD_BAR_WIDTH;
            const char *color = ctx_pct < CTX_ZONE_WARNING_PCT
                                    ? T_GREEN
                                    : (ctx_pct < CTX_ZONE_COMPACT_PCT ? T_YELLOW : T_RED);
            printf("  %-*s ", DASHBOARD_LABEL_WIDTH, "Context:");
            term_color(color, "[");
            for (int i = 0; i < DASHBOARD_BAR_WIDTH; i++)
                term_color(color, i < filled ? "█" : "░");
            term_color(color, "] %3d%%", ctx_pct);
            printf(" (%dk/%dk)\n", ctx_est / 1000, ctx_lim / 1000);
        }

        /* Fatigue velocity */
        {
            int fatigue_pct = context_fatigue_pct();
            int eta_turns = context_fatigue_turns_remaining();
            printf("  %-*s %d%%", DASHBOARD_LABEL_WIDTH, "Fatigue:", fatigue_pct);
            if (eta_turns > 0 && eta_turns < 9999)
                printf(" (~%d turns remaining)", eta_turns);
            printf("\n");
        }

        /* Stall detector */
        {
            stall_verdict_t sv = stall_detector_check();
            const char *sv_color = sv == STALL_OK ? T_GREEN : T_YELLOW;
            printf("  %-*s ", DASHBOARD_LABEL_WIDTH, "Stall:");
            term_color(sv_color, "%s", stall_verdict_name(sv));
            printf(" (window: %d/%d)\n", stall_detector_window_count(), STALL_WINDOW_SIZE);
        }

        /* Tool verification */
        {
            int tv_total = tool_verify_total_count();
            int tv_flagged = tool_verify_flagged_count();
            printf("  %-*s %d total, %d flagged\n", DASHBOARD_LABEL_WIDTH, "Tool verify:", tv_total,
                   tv_flagged);
        }

        /* Watchdog */
        {
            int wd_timeouts = watchdog_timeout_count();
            long wd_longest = watchdog_longest_turn_ms();
            printf("  %-*s %d timeout%s, longest turn %ldms\n", DASHBOARD_LABEL_WIDTH,
                   "Watchdog:", wd_timeouts, wd_timeouts != 1 ? "s" : "", wd_longest);
        }

        /* Output quality */
        {
            int eval_total = output_eval_total();
            int eval_pass = output_eval_pass_count();
            int eval_weak = output_eval_weak_count();
            if (eval_total > 0) {
                int pass_pct = eval_pass * 100 / eval_total;
                printf("  %-*s %d/%d pass (%d%%), %d weak\n", DASHBOARD_LABEL_WIDTH,
                       "Output eval:", eval_pass, eval_total, pass_pct, eval_weak);
            } else {
                printf("  %-*s (no evaluations yet)\n", DASHBOARD_LABEL_WIDTH, "Output eval:");
            }
        }

        /* Cost */
        {
            double total_cost = cost_tracker_total_cost();
            printf("  %-*s $%.4f  (%lld in / %lld out tokens)\n", DASHBOARD_LABEL_WIDTH,
                   "Cost:", total_cost, (long long)g_total_input_tokens,
                   (long long)g_total_output_tokens);
        }

        /* Harness */
        {
            harness_ctx_t *h = harness_global();
            long uptime = daemon_uptime_s(&h->daemon);
            char up_s[NC_FMT_MAX];
            nc_fmt_elapsed_s(up_s, sizeof(up_s), (int)uptime);
            printf("  %-*s %ld turns, %d handoffs, uptime %s\n", DASHBOARD_LABEL_WIDTH,
                   "Harness:", (long)h->daemon.total_turns, h->daemon.total_handoffs, up_s);
        }

        /* Checkpoints & snapshots */
        {
            int cp = checkpoint_discipline_count();
            int gs = ghost_snapshot_count();
            int ep = episode_memory_count();
            printf("  %-*s %d checkpoint%s, %d snapshot%s, %d episode%s\n", DASHBOARD_LABEL_WIDTH,
                   "Persistence:", cp, cp != 1 ? "s" : "", gs, gs != 1 ? "s" : "", ep,
                   ep != 1 ? "s" : "");
        }

        term_separator();
        return CMD_CONTINUE;
    }

    /* /btw <question> — side query */
    if (side_query_is_trigger(input)) {
        const char *q = side_query_extract(input);
        side_query_run(q, conv, cfg);
        return CMD_CONTINUE;
    }

    /* /completion [zsh|bash|fish] — generate & install shell completion */
    if (nc_starts_with(input, "/completion")) {
        const char *arg = input + 11;
        while (*arg == ' ')
            arg++;
        const char *shell = (*arg) ? arg : NULL;
        char *script = shell_completion_generate(shell);
        if (script) {
            printf("%s", script);
            free(script);
            int rc = shell_completion_install(shell);
            if (rc == 0)
                term_color(T_GREEN, "\n(completion script installed)\n");
        } else {
            term_color(T_RED, "Unknown shell. Try: /completion zsh|bash|fish\n");
        }
        return CMD_CONTINUE;
    }

    /* /git — git operation summary */
    if (strcmp(input, "/git") == 0) {
        char *s = git_ops_format_summary();
        if (s) {
            printf("%s\n", s);
            free(s);
        }
        return CMD_CONTINUE;
    }

    if (strcmp(input, "/help") == 0) {
#define H2(a, da, b, db) printf("  %-16s %-22s  %-16s %s\n", a, da, b, db)
#define H1(a, da) printf("  %-16s %s\n", a, da)
        term_color(T_BOLD, "Session\n");
        H2("/clear", "Clear conversation", "/compact", "Compress messages");
        H2("/copy [N]", "Copy to clipboard", "/paste", "Paste from clipboard");
        H2("/undo", "Undo last exchange", "/snip", "Clear old tool results");
        H2("/summary", "Session summary", "/keys", "Keyboard shortcuts");

        term_color(T_BOLD, "\nSession Management\n");
        H2("/history", "List past sessions", "/resume", "Resume a session");
        H2("/branch", "Fork conversation", "/rename", "Name this session");
        H2("/share", "Export as Markdown", "/export", "Export conversation");

        term_color(T_BOLD, "\nConfig & Info\n");
        H2("/config", "Show configuration", "/model", "Show or change model");
        H2("/models", "List available models", "/mpreset", "List or apply presets");
        H2("/cost", "Session cost report", "/insights", "Cross-session usage report");
        H2("/status", "Session status", "/env", "Environment info");
        H2("/timing", "Per-turn timing", "/doctor", "System diagnostics");
        H2("/version", "Version info", "/brief", "Toggle concise output");
        H2("/fast", "Toggle fast mode", "/effort", "Thinking effort");
        H2("/personality", "Set personality", "/permissions", "View permission rules");
        H2("/files", "Files in context", "/routing", "Model routing tiers");
        H2("/stats", "Cumulative stats", "/tokens", "Token usage");
        H2("/fallback", "Fallback providers", "/suggest", "Prompt suggestion");
        H2("/context", "Context + token stats", "/away", "Away summary config");
        H2("/network", "Network policy", "/sm", "Session memory");

        term_color(T_BOLD, "\nGit & Code\n");
        H2("/commit", "Create a git commit", "/diff", "Uncommitted / /diff session");
        H2("/pr", "Create a pull request", "/review", "Review a pull request");
        H2("/init", "Initialize AGENTS.md", "/rewind", "File checkpoints");
        H2("/scaffold", "Project scaffolding", "/rules", "Exec policy rules");
        H1("/git", "Git operation summary");

        term_color(T_BOLD, "\nTools & Agents\n");
        H2("/plan", "Plan mode (read-only)", "/endplan", "Exit plan mode");
        H2("/mcp", "Manage MCP servers", "/agents", "Background agents");
        H2("/memory", "Show memories", "/remember", "Store a memory");
        H2("/skills", "Available skills", "/preset", "Security presets");
        H2("/procs", "Tracked processes", "/diag", "Parse build errors");
        H2("/tasks", "Background task list", "/coordinator", "Multi-task status");
        H2("/harness", "Harness status", "/realtime", "Streaming status");
        H2("/episodes", "Prior experience log", "/hyp", "Debug hypotheses");
        H2("/eval", "Output evaluator stats", "/autotest", "Auto-test status");
        H2("/health", "Health dashboard", "/features", "Feature list status");

        term_color(T_BOLD, "\nUI & Input\n");
        H2("/vim", "Toggle vim mode", "/output-style", "Output rendering");
        H2("/keybindings", "Show key bindings", "/session", "Session details");
        H2("/hooks", "List configured hooks", "/bridge", "Remote connections");
        H2("/btw", "Quick side question", "/completion", "Shell completions");

        term_color(T_BOLD, "\nSkills\n");
        H2("/verify", "Adversarial testing", "/debug", "Diagnostic debugging");
        H2("/simplify", "Refactor & simplify", "/stuck", "Break out of loops");
        H2("/batch", "Multi-file transforms", "/perf", "Performance profiling");
        H2("/test", "Generate tests", "/security-review", "Security audit");
        H2("/recon", "CTF reconnaissance", "/exploit", "Exploit development");
        H1("/tips", "All tips & tricks");
        {
            char *names[32];
            int nc = custom_skill_list(names, 32);
            if (nc > 0) {
                term_color(T_BOLD, "\nCustom Skills\n");
                for (int i = 0; i + 1 < nc; i += 2)
                    H2(names[i], "", names[i + 1], "");
                if (nc % 2 == 1)
                    H1(names[nc - 1], "");
            }
        }
#undef H2
#undef H1
        printf("\n  TAB  command & @file completion   \\    multi-line input\n");
        return CMD_CONTINUE;
    }

    return CMD_NOT_HANDLED;
}
