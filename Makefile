CC      ?= cc
OBJDIR  := build
CFLAGS  := -Wall -Wextra -Wformat=2 -Wno-format-nonliteral -Wno-unused-parameter -O2 \
           -Iinclude -Ithird_party/cJSON
LDFLAGS := -lcurl -lpthread -lz -lm

# macOS readline (libedit compatible)
UNAME := $(shell uname)
ifeq ($(UNAME), Linux)
  # glibc exposes GNU extensions (O_PATH, FNM_CASEFOLD, ...) only under _GNU_SOURCE
  CFLAGS  += -D_GNU_SOURCE
endif
ifeq ($(UNAME), Darwin)
  CFLAGS  += -DUSE_READLINE
  LDFLAGS += -lreadline -liconv -framework IOKit -framework CoreFoundation
else ifneq ($(wildcard /usr/include/readline/readline.h),)
  CFLAGS  += -DUSE_READLINE
  LDFLAGS += -lreadline
endif

# ── Source files organized by module ──

CORE_SRCS := src/core/main.c \
             src/core/config.c \
             src/core/conversation.c \
             src/core/strbuf.c \
             src/core/prompt.c \
             src/core/repl_globals.c \
             src/core/structured_output.c \
             src/core/session_stats.c \
             src/core/effort_level.c \
             src/core/cost_tracker.c \
             src/core/state_machine.c \
             src/core/event_bus.c \
             src/core/config_loader.c \
             src/core/query_mode.c \
             src/core/session_prewarm.c \
             src/core/event_mapping.c \
             src/core/session_mode.c \
             src/core/prompt_builder.c \
             src/core/protocol.c \
             src/core/event_dispatch.c \
             src/core/interrupted_turn.c \
             src/core/startup_warmup.c \
             src/core/config_schema.c \
             src/core/onboard.c \
             src/core/session_resume.c \
             src/core/personality.c \
             src/core/coordinator_prompt.c \
             src/core/auto_continue.c \
             src/core/session_status.c \
             src/core/batch_runner.c \
             src/core/session_scratch.c \
             src/core/idle_guard.c \
             src/core/prompt_section.c \
             src/core/output_anchor.c \
             src/core/startup_profiler.c

API_SRCS := src/api/api_provider.c \
            src/api/api_messages.c \
            src/api/api_compat.c \
            src/api/api_retry.c \
            src/api/stream_handler.c \
            src/api/multi_provider.c \
            src/api/cache_hint.c \
            src/api/cache_strategy.c \
            src/api/server_mode.c \
            src/api/stream_parser.c \
            src/api/thinking.c \
            src/api/realtime_conv.c \
            src/api/json_streamer.c \
            src/api/api_error.c \
            src/api/provider_auth.c \
            src/api/provider_info.c \
            src/api/request_compress.c \
            src/api/resp_debug.c \
            src/api/responses_api.c \
            src/api/stream_ctrl.c \
            src/api/stream_event.c \
            src/api/tc_parser.c \
            src/api/key_pool.c \
            src/api/api_preconnect.c

TOOL_SRCS := src/tools/tool_registry.c \
             src/tools/bash.c \
             src/tools/file_read.c \
             src/tools/file_write.c \
             src/tools/file_edit.c \
             src/tools/glob.c \
             src/tools/grep.c \
             src/tools/agent.c \
             src/tools/web_fetch.c \
             src/tools/web_search.c \
             src/tools/todo.c \
             src/tools/mcp.c \
             src/tools/listdir.c \
             src/tools/notebook_edit.c \
             src/tools/worktree.c \
             src/tools/tool_search.c \
             src/tools/ask_user.c \
             src/tools/apply_patch.c \
             src/tools/config_tool.c \
             src/tools/sleep_tool.c \
             src/tools/tool_suggest.c \
             src/tools/request_perms.c \
             src/tools/repl.c \
             src/tools/task.c \
             src/tools/mcp_resources.c \
             src/tools/git_diff.c \
             src/tools/plan_mode.c \
             src/tools/skill_tool.c \
             src/tools/brief_tool.c \
             src/tools/cron_tool.c \
             src/tools/code_mode.c \
             src/tools/file_index.c \
             src/tools/lsp_bridge.c \
             src/tools/magic_docs.c \
             src/tools/dynamic_tool.c \
             src/tools/tool_router.c \
             src/tools/file_search.c \
             src/tools/send_msg.c \
             src/tools/image_view.c \
             src/tools/ctx_viz.c \
             src/tools/autofix.c \
             src/tools/tool_error.c \
             src/tools/tool_result_store.c \
             src/tools/tool_schema_cache.c \
             src/tools/team_tool.c \
             src/tools/structured_out.c \
             src/tools/mcp_auth.c \
             src/tools/scaffold.c \
             src/tools/multi_edit.c \
             src/tools/batch_tool.c \
             src/tools/consensus.c \
             src/tools/workflow.c \
             src/tools/snip_tool.c \
             src/tools/proc_monitor.c \
             src/tools/git_ops.c

CONTEXT_SRCS := src/context/context_manager.c \
                src/context/context_compact.c \
                src/context/context_inject.c \
                src/context/context_suggest.c \
                src/context/compact_enhance.c \
                src/context/thread_truncation.c \
                src/context/token_estimate.c \
                src/context/token_budget.c \
                src/context/turn_diff.c \
                src/context/away_summary.c \
                src/context/workspace_context.c \
                src/context/ctx_token_stats.c \
                src/context/budget_parse.c \
                src/context/remote_compact.c \
                src/context/realtime_ctx.c \
                src/context/post_compact.c \
                src/context/env_context.c \
                src/context/compact_template.c \
                src/context/auto_compact.c \
                src/context/contextual_msg.c \
                src/context/conv_summary.c \
                src/context/round_grouping.c \
                src/context/ctx_history.c \
                src/context/ctx_normalize.c \
                src/context/repo_map.c \
                src/context/compact_warn.c \
                src/context/ctx_ref_tracker.c \
                src/context/pinned_msg.c

AGENT_SRCS := src/agents/multi_agent.c \
              src/agents/custom_agents.c \
              src/agents/agents_md.c \
              src/agents/project_doc_paths.c \
              src/agents/skills.c \
              src/agents/agent_utils.c \
              src/agents/coordinator.c \
              src/agents/mailbox.c \
              src/agents/roles.c \
              src/agents/agent_summary.c \
              src/agents/skills_watcher.c \
              src/agents/skill_usage.c \
              src/agents/arg_subst.c \
              src/agents/swarm.c \
              src/agents/team_mgr.c \
              src/agents/scratchpad.c \
              src/agents/delegate.c \
              src/agents/agent_status.c \
              src/agents/agent_control.c \
              src/agents/agent_health.c \
              src/agents/coordinator_mode.c \
              src/agents/collab_v2.c \
              src/agents/agent_v2.c \
              src/agents/swarm_backend.c \
              src/agents/agent_color.c

MEMORY_SRCS := src/memory/memory.c \
               src/memory/history.c \
               src/memory/file_history.c \
               src/memory/conv_export.c \
               src/memory/conv_branch.c \
               src/memory/conv_recovery.c \
               src/memory/session_memory.c \
               src/memory/mem_citation.c \
               src/memory/thread_mgr.c \
               src/memory/auto_dream.c \
               src/memory/rollout_replay.c \
               src/memory/mem_pipeline.c \
               src/memory/session_transcript.c \
               src/memory/mem_trace.c \
               src/memory/mem_phase.c \
               src/memory/team_memory_sync.c \
               src/memory/transcript_search.c \
               src/memory/memdir.c \
               src/memory/session_title.c \
               src/memory/title_llm.c \
               src/memory/agent_mem_scope.c \
               src/memory/trajectory_compress.c

EXEC_SRCS := src/exec/tool_executor.c \
             src/exec/tool_result.c \
             src/exec/streaming_exec.c \
             src/exec/shell_session.c \
             src/exec/procgroup.c \
             src/exec/exec_rules.c \
             src/exec/turn_timing.c \
             src/exec/shell_snapshot.c \
             src/exec/process_lifecycle.c \
             src/exec/turn_meta.c \
             src/exec/tool_summary.c \
             src/exec/async_watcher.c \
             src/exec/parallel_exec.c \
             src/exec/hardened_exec.c \
             src/exec/task_types.c \
             src/exec/tool_orchestrate.c \
             src/exec/fork_backend.c \
             src/exec/exec_env.c \
             src/exec/shell_escalation.c \
             src/exec/tool_pool.c \
             src/exec/process_mgr.c \
             src/exec/exec_policy.c \
             src/exec/process_state.c \
             src/exec/stream_tool_exec.c \
             src/exec/user_shell_cmd.c \
             src/exec/abort_handler.c \
             src/exec/tool_call_rt.c \
             src/exec/unified_exec.c \
             src/exec/proc_output_buf.c

UI_SRCS := src/ui/terminal.c \
           src/ui/slash_commands.c \
           src/ui/prompt_suggest.c \
           src/ui/tips.c \
           src/ui/mention_syntax.c \
           src/ui/input_preprocess.c \
           src/ui/term_detect.c \
           src/ui/diff_render.c \
           src/ui/md_render.c \
           src/ui/vim_mode.c \
           src/ui/output_style.c \
           src/ui/keybindings.c \
           src/ui/notify.c \
           src/ui/frame_limiter.c \
           src/ui/chunk_policy.c \
           src/ui/spinner.c \
           src/ui/color_palette.c \
           src/ui/status_bar.c \
           src/ui/clipboard.c \
           src/ui/key_hint.c \
           src/ui/line_wrap.c \
           src/ui/input_history.c \
           src/ui/side_query.c \
           src/ui/msg_collapse.c \
           src/ui/cmd_fuzzy.c \
           src/ui/activity_heatmap.c

UTILS_SRCS := src/utils/edit_match.c \
              src/utils/fuzzy_match.c \
              src/utils/tool_args_parse.c \
              src/utils/text_encoding.c \
              src/utils/middle_trunc.c \
              src/utils/head_tail_buffer.c \
              src/utils/image.c \
              src/utils/cmd_canonical.c \
              src/utils/shell_detect.c \
              src/utils/commit_attr.c \
              src/utils/ansi_strip.c \
              src/utils/output_trunc.c \
              src/utils/seek_match.c \
              src/utils/git_fs.c \
              src/utils/input_guard.c \
              src/utils/alloc_track.c \
              src/utils/shell_completion.c \
              src/utils/file_read_cache.c \
              src/utils/generated_detect.c \
              src/utils/url_guard.c

REVIEW_SRCS := src/review/review_format.c \
               src/review/review_integration.c \
               src/review/diagnostic.c \
               src/review/risk_engine.c \
               src/review/guardian.c \
               src/review/action_risk.c \
               src/review/review_prompt.c

HARNESS_SRCS := src/harness/failure_router.c \
               src/harness/goal_tracker.c \
               src/harness/handoff.c \
               src/harness/daemon.c \
               src/harness/ghost_snapshot.c \
               src/harness/stall_detector.c \
               src/harness/thread_state.c \
               src/harness/context_fatigue.c \
               src/harness/progress_tracker.c \
               src/harness/tool_verify.c \
               src/harness/checkpoint_discipline.c \
               src/harness/hypothesis_debug.c \
               src/harness/episode_memory.c \
               src/harness/feature_list.c \
               src/harness/session_preflight.c \
               src/harness/output_eval.c \
               src/harness/auto_test.c \
               src/harness/init_agent.c \
               src/harness/session_watchdog.c \
               src/harness/doom_loop.c \
               src/harness/verify_trigger.c \
               src/harness/sprint_contract.c \
               src/harness/pipeline.c \
               src/harness/fanout.c \
               src/harness/success_criteria.c \
               src/harness/cancel_scope.c \
               src/harness/shared_pad.c \
               src/harness/event_ledger.c \
               src/harness/preflight_report.c \
               src/harness/periodic_probe.c \
               src/harness/retry_cycle.c \
               src/harness/pipe_orchestrator.c \
               src/harness/scope_aggregate.c \
               src/harness/prompt_ctx.c \
               src/harness/node_artifact.c \
               src/harness/ledger_watcher.c \
               src/harness/run_persist.c

STATE_SRCS := src/state/state_db.c \
              src/state/session_snapshot.c \
              src/state/rollout_tracker.c \
              src/state/rollout_policy.c \
              src/state/rollout_index.c \
              src/state/rollout_metadata.c \
              src/state/rollout_truncate.c \
              src/state/conv_rewind.c \
              src/state/session_registry.c

SYSTEM_SRCS := src/system/permission.c \
               src/system/sandbox.c \
               src/system/hooks.c \
               src/system/network_policy.c \
               src/system/sleep_inhibit.c \
               src/system/file_watcher.c \
               src/system/hot_reload.c \
               src/system/git_undo.c \
               src/system/model_router.c \
               src/system/project_root.c \
               src/system/practical.c \
               src/system/approval_preset.c \
               src/system/model_registry.c \
               src/system/model_caps.c \
               src/system/plugin.c \
               src/system/model_discovery.c \
               src/system/connector.c \
               src/system/secrets.c \
               src/system/plugin_manifest.c \
               src/system/resource_monitor.c \
               src/system/feature_flags.c \
               src/system/git_ops.c \
               src/system/remote_bridge.c \
               src/system/model_presets.c \
               src/system/hook_runtime.c \
               src/system/proc_harden.c \
               src/system/models_mgr.c \
               src/system/diagnostics.c \
               src/system/plugin_discover.c \
               src/system/plugin_inject.c \
               src/system/plugin_toggle.c \
               src/system/config_migrate.c \
               src/system/ext_agent_config.c \
               src/system/network_proxy.c \
               src/system/os_sandbox.c \
               src/system/plugin_market.c \
               src/system/plugin_store.c \
               src/system/knowledge_cutoff.c \
               src/system/model_deprecation.c

VENDOR_SRCS := third_party/cJSON/cJSON.c

SRCS := $(CORE_SRCS) $(API_SRCS) $(TOOL_SRCS) $(CONTEXT_SRCS) \
        $(AGENT_SRCS) $(MEMORY_SRCS) $(EXEC_SRCS) $(UI_SRCS) \
        $(UTILS_SRCS) $(REVIEW_SRCS) $(SYSTEM_SRCS) $(HARNESS_SRCS) \
        $(STATE_SRCS) $(VENDOR_SRCS)

OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))
BIN  := nevercode

# Rebuild objects when shared headers change (e.g. NEVERCODE_VERSION in defaults.h)
NC_INCLUDE_HDRS := $(wildcard include/*.h)

# ── Test sources (all library sources except main.c) ──
TEST_LIB_SRCS := $(filter-out src/core/main.c,$(SRCS))
TEST_LIB_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(TEST_LIB_SRCS))

TEST_UNIT_SRCS := $(sort $(filter-out tests/test_integration.c,$(wildcard tests/test_*.c)))
TEST_UNIT_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(TEST_UNIT_SRCS))

TEST_INT_SRCS := tests/test_integration.c
TEST_INT_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(TEST_INT_SRCS))

TEST_BIN      := test_unit
TEST_INT_BIN  := test_integration


PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin

.PHONY: all clean test test-unit test-integration install uninstall

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: %.c $(NC_INCLUDE_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/tests/%.o: tests/%.c $(NC_INCLUDE_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DUNIFIED_TEST -c -o $@ $<

# Note: test binaries declare multi-MB orchestrator structs as function-local
# `static` (BSS) rather than on the stack, so no stack-size games are needed.
ifeq ($(UNAME), Darwin)
  TEST_LDFLAGS := -Wl,-stack_size,0x8000000
else
  TEST_LDFLAGS :=
endif

# Unit tests
$(TEST_BIN): $(TEST_UNIT_OBJS) $(TEST_LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(TEST_LDFLAGS)

# Integration tests
$(TEST_INT_BIN): $(TEST_INT_OBJS) $(TEST_LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(TEST_LDFLAGS)

test: test-unit

test-unit: $(TEST_BIN)
	./$(TEST_BIN)

test-integration: $(TEST_INT_BIN)
	./$(TEST_INT_BIN)

test-all: test-unit test-integration

test-harness:
	./scripts/test-harness.sh

test-harness-json:
	./scripts/test-harness.sh --json

release: $(BIN)
	strip -o $(BIN)-stripped $(BIN)
	@ls -lh $(BIN)-stripped | awk '{print "stripped binary: " $$5 " " $$9}'

install: $(BIN)
	@mkdir -p $(DESTDIR)$(BINDIR)
	install -m 755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -rf $(OBJDIR) $(BIN) $(BIN)-stripped $(TEST_BIN) $(TEST_INT_BIN) || true
