/*
 * context_manager.c — Context management, auto-compaction, and API error
 * classification.
 *
 * Handles four-level progressive compaction (L2/L3/L3.5/L4), tool-pair
 * integrity checks after compaction, and iterative summary preservation
 * across multiple compaction rounds.
 */

#include "repl.h"
#include "api.h"
#include "terminal.h"

static char *g_prev_compact_summary = NULL;

int context_limit_for_model(const char *model) {
    if (g_cfg_ptr && g_cfg_ptr->context_window > 0)
        return g_cfg_ptr->context_window;
    int active_idx = model_preset_active_idx();
    int preset_cw = model_preset_context_window(active_idx);
    int registry_cw = model_context_window(model);
    return (preset_cw > 0 && preset_cw != DEFAULT_CONTEXT_FALLBACK) ? preset_cw : registry_cw;
}

context_zone_t get_context_zone(int est, int limit) {
    if (limit <= 0)
        return CTX_ZONE_SAFE;
    int pct = (int)((long long)est * 100 / limit);
    if (pct >= CTX_ZONE_BLOCKING_PCT)
        return CTX_ZONE_BLOCKING;
    if (pct >= CTX_ZONE_COMPACT_PCT)
        return CTX_ZONE_COMPACT;
    if (pct >= CTX_ZONE_WARNING_PCT)
        return CTX_ZONE_WARNING;
    return CTX_ZONE_SAFE;
}

static const char *COMPACT_SYSTEM_PROMPT =
    "CRITICAL: Respond with TEXT ONLY. Do NOT call any tools.\n"
    "- Do NOT use Read, Bash, Grep, Glob, Edit, Write, or ANY other tool.\n"
    "- You already have all the context you need in the conversation above.\n"
    "- Tool calls will be REJECTED and will waste your only turn.\n"
    "- Your entire response must be plain text: an <analysis> block followed "
    "by a <summary> block.\n\n"
    "Your task is to create a detailed summary of the conversation so far, "
    "paying close attention to the user's explicit requests and your previous "
    "actions. This summary should be thorough in capturing technical details, "
    "code patterns, and architectural decisions that would be essential for "
    "continuing development work without losing context.\n\n"
    "Before providing your final summary, wrap your analysis in <analysis> "
    "tags to organize your thoughts and ensure you've covered all necessary "
    "points. In your analysis process:\n\n"
    "1. Chronologically analyze each message and section of the conversation. "
    "For each section thoroughly identify:\n"
    "   - The user's explicit requests and intents\n"
    "   - Your approach to addressing the user's requests\n"
    "   - Key decisions, technical concepts and code patterns\n"
    "   - Specific details like file names, full code snippets, function "
    "signatures, file edits\n"
    "   - Errors that you ran into and how you fixed them\n"
    "   - Pay special attention to specific user feedback, especially if the "
    "user told you to do something differently.\n"
    "2. Double-check for technical accuracy and completeness.\n\n"
    "Your summary should include the following sections:\n\n"
    "1. Primary Request and Intent: Capture ALL of the user's explicit "
    "requests and intents in detail\n"
    "2. Key Technical Concepts: List all important technical concepts, "
    "technologies, and frameworks discussed.\n"
    "3. Files and Code Sections: Enumerate specific files and code sections "
    "examined, modified, or created. Include full code snippets where "
    "applicable and include a summary of why this file read or edit is "
    "important.\n"
    "4. Errors and Fixes: List all errors encountered, how they were fixed, "
    "and any user feedback received.\n"
    "5. Problem Solving: Document problems solved and ongoing troubleshooting.\n"
    "6. All User Messages: List ALL user messages that are not tool results. "
    "These are critical for understanding feedback and changing intent.\n"
    "7. Pending Tasks: Outline any pending tasks explicitly asked to work on.\n"
    "8. Current Work: Describe in detail precisely what was being worked on "
    "immediately before this summary request, paying special attention to "
    "the most recent messages. Include file names and code snippets.\n"
    "9. Optional Next Step: List the next step related to the most recent "
    "work. IMPORTANT: ensure this step is DIRECTLY in line with the user's "
    "most recent explicit requests. Include direct quotes from the most "
    "recent conversation showing exactly what task you were working on and "
    "where you left off.\n\n"
    "REMINDER: Do NOT call any tools. Respond with plain text only — an "
    "<analysis> block followed by a <summary> block. Tool calls will be "
    "rejected and you will fail the task.";

static void compact_stream_cb(const stream_event_t *ev, void *ud) {
    strbuf_t *sb = ud;
    if (ev->type == STREAM_EVENT_TEXT_DELTA && ev->text)
        strbuf_append_str(sb, ev->text);
}

static char *generate_llm_summary(const message_t *msgs, int n) {
    if (!g_cfg_ptr)
        return NULL;

    strbuf_t conv_text;
    strbuf_init(&conv_text);

    if (g_compact_instructions && g_compact_instructions[0]) {
        strbuf_append_str(&conv_text, "## User Compaction Instructions\n\n");
        strbuf_append_str(&conv_text, g_compact_instructions);
        strbuf_append_str(&conv_text, "\n\n---\n\n");
    }

    /* Iterative summary: inject the previous compaction's summary so the
     * model can UPDATE rather than recreate from scratch.  This preserves
     * decisions and file changes accumulated across multiple compactions. */
    if (g_prev_compact_summary && g_prev_compact_summary[0]) {
        strbuf_append_str(&conv_text,
                          "## Previous Compaction Summary (update — do NOT discard)\n\n");
        strbuf_append_str(&conv_text, g_prev_compact_summary);
        strbuf_append_str(&conv_text,
                          "\n\n---\n\n"
                          "Incorporate the NEW conversation turns below into the "
                          "summary above. PRESERVE existing information that is "
                          "still relevant. ADD new progress, move completed items.\n\n");
    }

    strbuf_append_str(&conv_text, "Conversation to summarize:\n\n");

    for (int i = 0; i < n; i++) {
        const char *role_name = "user";
        if (msgs[i].role == ROLE_ASSISTANT)
            role_name = "assistant";
        else if (msgs[i].role == ROLE_TOOL)
            role_name = "tool";
        else if (msgs[i].role == ROLE_SYSTEM)
            role_name = "system";

        for (int j = 0; j < msgs[i].num_blocks; j++) {
            const content_block_t *b = &msgs[i].blocks[j];
            if (b->type == CONTENT_TEXT && b->text) {
                strbuf_append_str(&conv_text, "[");
                strbuf_append_str(&conv_text, role_name);
                strbuf_append_str(&conv_text, "] ");
                size_t tlen = strlen(b->text);
                if (tlen > CTX_COMPACT_TEXT_TRUNC) {
                    strbuf_append(&conv_text, b->text, CTX_COMPACT_HEAD_TAIL);
                    strbuf_append_str(&conv_text, "\n...(");
                    strbuf_appendf(&conv_text, "%zu chars omitted", tlen - CTX_COMPACT_TEXT_TRUNC);
                    strbuf_append_str(&conv_text, ")...\n");
                    strbuf_append(&conv_text, b->text + tlen - CTX_COMPACT_HEAD_TAIL,
                                  CTX_COMPACT_HEAD_TAIL);
                } else {
                    strbuf_append_str(&conv_text, b->text);
                }
                strbuf_append_str(&conv_text, "\n\n");
            } else if (b->type == CONTENT_TOOL_USE && b->tool_name) {
                strbuf_append_str(&conv_text, "[tool_use: ");
                strbuf_append_str(&conv_text, b->tool_name);
                strbuf_append_str(&conv_text, "]\n");
            } else if (b->type == CONTENT_TOOL_RESULT && b->text) {
                strbuf_append_str(&conv_text, "[tool_result] ");
                size_t tlen = strlen(b->text);
                if (tlen > CTX_TOOL_RESULT_TRUNC) {
                    size_t half = CTX_TOOL_RESULT_TRUNC / 2;
                    strbuf_append(&conv_text, b->text, half);
                    strbuf_append_str(&conv_text, "\n...(truncated)...\n");
                    strbuf_append(&conv_text, b->text + tlen - half, half);
                } else {
                    strbuf_append_str(&conv_text, b->text);
                }
                strbuf_append_str(&conv_text, "\n\n");
            }
        }
    }

    message_t compact_msg;
    message_init(&compact_msg, ROLE_USER);
    message_add_text(&compact_msg, conv_text.data ? conv_text.data : "");
    strbuf_free(&conv_text);

    strbuf_t result;
    strbuf_init(&result);

    int rc = api_stream_request(&compact_msg, 1, COMPACT_SYSTEM_PROMPT, g_cfg_ptr, NULL, 0,
                                compact_stream_cb, &result);

    message_free(&compact_msg);

    if (rc != 0 || result.len == 0) {
        strbuf_free(&result);
        return NULL;
    }

    /* Extract <summary> block, discard <analysis> (dual-phase pattern) */
    char *raw = strbuf_detach(&result);
    char *sum_start = strstr(raw, "<summary>");
    char *sum_end = strstr(raw, "</summary>");
    if (sum_start && sum_end && sum_end > sum_start) {
        sum_start += 9; /* strlen("<summary>") */
        size_t slen = (size_t)(sum_end - sum_start);
        char *extracted = malloc(slen + 1);
        if (extracted) {
            memcpy(extracted, sum_start, slen);
            extracted[slen] = '\0';
            free(raw);
            return extracted;
        }
    }
    return raw;
}

/*
 * Fix orphaned tool_call / tool_result pairs after compaction.
 *
 * When messages are dropped, an assistant's tool_calls may lose their
 * matching tool results (or vice versa).  The API rejects mismatched
 * IDs, so we:
 *   1. Remove tool results whose call_id has no matching tool_call
 *   2. Insert stub results for tool_calls whose results were dropped
 */
static int sanitize_tool_pairs(conversation_t *conv) {
    if (!conv || conv->len < 2)
        return 0;
    int fixes = 0;

    /* Collect surviving tool_call IDs from assistant messages */
    const char *call_ids[NEVERCODE_MAX_TOOL_CALLS * 8];
    int n_calls = 0;
    for (int i = 0; i < conv->len && n_calls < NC_ARRAY_LEN(call_ids); i++) {
        if (conv->msgs[i].role != ROLE_ASSISTANT)
            continue;
        for (int b = 0; b < conv->msgs[i].num_blocks; b++) {
            const content_block_t *blk = &conv->msgs[i].blocks[b];
            if (blk->type == CONTENT_TOOL_USE && blk->tool_use_id && blk->tool_use_id[0])
                call_ids[n_calls++] = blk->tool_use_id;
        }
    }

    /* Collect surviving tool result IDs */
    const char *result_ids[NEVERCODE_MAX_TOOL_CALLS * 8];
    int n_results = 0;
    for (int i = 0; i < conv->len && n_results < NC_ARRAY_LEN(result_ids); i++) {
        if (conv->msgs[i].role != ROLE_TOOL)
            continue;
        for (int b = 0; b < conv->msgs[i].num_blocks; b++) {
            const content_block_t *blk = &conv->msgs[i].blocks[b];
            if (blk->type == CONTENT_TOOL_RESULT && blk->tool_use_id && blk->tool_use_id[0])
                result_ids[n_results++] = blk->tool_use_id;
        }
    }

    /* 1. Remove orphaned tool results (result exists but no matching call) */
    for (int i = conv->len - 1; i >= 0; i--) {
        if (conv->msgs[i].role != ROLE_TOOL)
            continue;
        for (int b = 0; b < conv->msgs[i].num_blocks; b++) {
            const content_block_t *blk = &conv->msgs[i].blocks[b];
            if (blk->type != CONTENT_TOOL_RESULT || !blk->tool_use_id)
                continue;
            bool has_call = false;
            for (int c = 0; c < n_calls; c++) {
                if (strcmp(call_ids[c], blk->tool_use_id) == 0) {
                    has_call = true;
                    break;
                }
            }
            if (!has_call) {
                conversation_remove_at(conv, i);
                fixes++;
                break;
            }
        }
    }

    /* 2. Insert stub results for tool_calls whose results were dropped */
    for (int c = 0; c < n_calls; c++) {
        bool has_result = false;
        for (int r = 0; r < n_results; r++) {
            if (strcmp(result_ids[r], call_ids[c]) == 0) {
                has_result = true;
                break;
            }
        }
        if (has_result)
            continue;

        /* Find the assistant message with this call and insert a stub after it */
        for (int i = 0; i < conv->len; i++) {
            if (conv->msgs[i].role != ROLE_ASSISTANT)
                continue;
            bool found = false;
            for (int b = 0; b < conv->msgs[i].num_blocks; b++) {
                if (conv->msgs[i].blocks[b].type == CONTENT_TOOL_USE &&
                    conv->msgs[i].blocks[b].tool_use_id &&
                    strcmp(conv->msgs[i].blocks[b].tool_use_id, call_ids[c]) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found)
                continue;

            /* Insert stub tool result after this assistant message */
            int ins = i + 1;
            while (ins < conv->len && conv->msgs[ins].role == ROLE_TOOL)
                ins++;
            message_t *stub = conversation_insert_at(conv, ins, ROLE_TOOL);
            if (!stub)
                break;
            message_add_tool_result(stub, call_ids[c], "[Result from compacted context]", 0);
            fixes++;
            break;
        }
    }

    return fixes;
}

int auto_compact(conversation_t *conv) {
    if (g_compact_failures >= CIRCUIT_BREAKER_THRESHOLD) {
        term_color(T_DIM, "(auto-compact circuit-breaker open: %d consecutive failures)\n",
                   g_compact_failures);
        return 0;
    }

    int est = estimate_conversation_tokens(conv->msgs, conv->len, conv->system_prompt);
    int limit = context_limit_for_model(config_model(g_cfg_ptr));
    context_zone_t zone = get_context_zone(est, limit);

    /* Level 2: MicroCompact — cache-aware bulk clearance */
    if (est > (int)(limit * 0.5) && conv->len > 6) {
        int freed = context_micro_compact(conv, g_last_assistant_time, NC_MICRO_COMPACT_FLOOR);
        if (freed > 0) {
            compact_metrics_record(2, freed);
            int saved_tokens = context_estimate_freed_tokens(freed);
            term_color(T_DIM, "(L2 micro-compact: cleared %d chars, ~%d tokens)\n", freed,
                       saved_tokens);
            est -= saved_tokens;
            zone = get_context_zone(est, limit);
        }
    }

    /* Level 3: Collapse — selective message group folding */
    if (context_collapse_needed(est, limit) && conv->len > 8) {
        int freed = context_collapse(conv, COLLAPSE_KEEP_RECENT);
        if (freed > 0) {
            compact_metrics_record(3, freed);
            int saved_tokens = context_estimate_freed_tokens(freed);
            term_color(T_DIM, "(L3 collapse: folded %d chars, ~%d tokens)\n", freed, saved_tokens);
            est -= saved_tokens;
            zone = get_context_zone(est, limit);
        }
    }

    if (zone < CTX_ZONE_COMPACT && conv->len <= 8)
        return 0;
    if (conv->len <= 4)
        return 0;

    /* Level 3.5: Session-memory-based zero-cost compaction.
     * If session memory has meaningful content, use it directly as the
     * summary instead of calling the LLM.  Saves an API call entirely.
     *
     * Calculates which messages to keep using a token+text-block budget
     * (SM_COMPACT_MIN_TOKENS / SM_COMPACT_MIN_TEXT_MSGS / SM_COMPACT_MAX_TOKENS)
     * and then adjusts the cut boundary to preserve API invariants:
     *   - tool_use / tool_result pairs are never split
     *   - assistant message groups (shared msg_id) stay together */
    char *sm_only = session_memory_for_compact();
    if (sm_only && strlen(sm_only) > CTX_SM_INLINE_MIN) {
        /* Walk backwards from the end, accumulating tokens and counting
         * messages that carry user-visible text, until both minimums are
         * satisfied or the hard cap is reached. */
        int tail_tok = 0;
        int text_msgs = 0;
        int start_idx = conv->len;

        for (int i = conv->len - 1; i >= 0; i--) {
            int mt = estimate_message_tokens(&conv->msgs[i]);
            tail_tok += mt;
            if (conv->msgs[i].role == ROLE_USER || conv->msgs[i].role == ROLE_ASSISTANT) {
                bool has_text = false;
                for (int b = 0; b < conv->msgs[i].num_blocks; b++)
                    if (conv->msgs[i].blocks[b].type == CONTENT_TEXT &&
                        conv->msgs[i].blocks[b].text && conv->msgs[i].blocks[b].text[0])
                        has_text = true;
                if (has_text)
                    text_msgs++;
            }
            start_idx = i;
            if (tail_tok >= SM_COMPACT_MAX_TOKENS)
                break;
            if (tail_tok >= SM_COMPACT_MIN_TOKENS && text_msgs >= SM_COMPACT_MIN_TEXT_MSGS)
                break;
        }
        if (start_idx >= conv->len)
            start_idx = conv->len > 2 ? conv->len - 2 : 0;
        if (start_idx < 1)
            start_idx = 1;

        /* Adjust boundary: never split tool_use / tool_result pairs */
        while (start_idx < conv->len && conv->msgs[start_idx].role == ROLE_TOOL)
            start_idx++;

        /* Pull back further to include any assistant messages whose
         * tool_use blocks are referenced by tool_result blocks we keep. */
        for (int r = start_idx; r < conv->len; r++) {
            if (conv->msgs[r].role != ROLE_TOOL)
                continue;
            for (int b = 0; b < conv->msgs[r].num_blocks; b++) {
                const content_block_t *rb = &conv->msgs[r].blocks[b];
                if (rb->type != CONTENT_TOOL_RESULT || !rb->tool_use_id)
                    continue;
                bool found = false;
                for (int k = start_idx; k < r && !found; k++) {
                    if (conv->msgs[k].role != ROLE_ASSISTANT)
                        continue;
                    for (int m = 0; m < conv->msgs[k].num_blocks; m++) {
                        if (conv->msgs[k].blocks[m].type == CONTENT_TOOL_USE &&
                            conv->msgs[k].blocks[m].tool_use_id &&
                            strcmp(conv->msgs[k].blocks[m].tool_use_id, rb->tool_use_id) == 0) {
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    for (int k = start_idx - 1; k >= 0; k--) {
                        if (conv->msgs[k].role != ROLE_ASSISTANT)
                            continue;
                        bool match = false;
                        for (int m = 0; m < conv->msgs[k].num_blocks; m++) {
                            if (conv->msgs[k].blocks[m].type == CONTENT_TOOL_USE &&
                                conv->msgs[k].blocks[m].tool_use_id &&
                                strcmp(conv->msgs[k].blocks[m].tool_use_id, rb->tool_use_id) == 0)
                                match = true;
                        }
                        if (match) {
                            start_idx = k;
                            break;
                        }
                    }
                }
            }
        }

        int drop_sm = start_idx;
        int keep_sm = conv->len - drop_sm;

        if (drop_sm > 0 && keep_sm >= 2) {
            strbuf_t sm_sum;
            strbuf_init(&sm_sum);
            strbuf_append_str(
                &sm_sum,
                "This session is being continued from a previous conversation that "
                "ran out of context. The session memory below summarizes earlier work.\n\n");
            strbuf_append_str(&sm_sum, sm_only);
            strbuf_append_str(&sm_sum, "\n\nContinue from where you left off. Do not recap.");

            int old_len_sm = conv->len;
            conversation_replace_head(conv, drop_sm, sm_sum.data);
            strbuf_free(&sm_sum);
            free(sm_only);

            int new_est = estimate_conversation_tokens(conv->msgs, conv->len, conv->system_prompt);
            term_color(T_DIM, "(L3.5 session-memory compact: %d -> %d msgs, ~%dk -> ~%dk tokens)\n",
                       old_len_sm, conv->len, est / 1000, new_est / 1000);
            rollout_record_compaction(est, new_est);
            g_compact_failures = 0;
            auto_compact_reset_circuit();
            return 1;
        }
        free(sm_only);
    } else {
        free(sm_only);
    }

    int target = (int)(limit * 0.4);
    int keep = conv->len;
    int tail_tokens = 0;
    for (int i = conv->len - 1; i >= 0 && keep > 1; i--) {
        tail_tokens += estimate_message_tokens(&conv->msgs[i]);
        if (tail_tokens > target) {
            keep = conv->len - i;
            break;
        }
    }
    if (keep < 4)
        keep = 4;
    if (keep >= conv->len)
        keep = conv->len - 2;
    if (keep < 2)
        keep = 2;
    int drop = conv->len - keep;

    /* Adjust drop boundary: never split a tool_use/tool_result pair */
    while (drop > 0 && drop < conv->len && conv->msgs[drop].role == ROLE_TOOL) {
        drop++;
        keep--;
    }
    if (drop <= 0 || keep < 2)
        return 0;

    int imgs_stripped = compact_strip_images(conv->msgs, drop);
    if (imgs_stripped > 0)
        term_color(T_DIM, "(stripped %d image block%s for compaction)\n", imgs_stripped,
                   imgs_stripped > 1 ? "s" : "");

    term_color(T_DIM, "(compacting %d messages via LLM...)\n", drop);

    char *llm_summary = NULL;
    int ptl_retries = 0;
    int effective_drop = drop;

    for (int attempt = 0; attempt <= MAX_CONTEXT_RETRIES; attempt++) {
        (void)attempt;
        llm_summary = generate_llm_summary(conv->msgs + (drop - effective_drop), effective_drop);
        if (llm_summary)
            break;

        ptl_retries++;
        if (ptl_retries > MAX_CONTEXT_RETRIES)
            break;

        msg_group_t groups[CTX_MAX_MSG_GROUPS];
        int ng = compact_group_by_api_round(conv->msgs, effective_drop, groups, CTX_MAX_MSG_GROUPS);
        if (ng <= 1)
            break;

        int groups_to_drop = ng > 5 ? ng / 5 : 1;
        if (groups_to_drop >= ng)
            break;

        int new_start = groups[groups_to_drop].start;
        int old_eff = effective_drop;
        effective_drop -= new_start;
        term_color(T_DIM, "(compact PTL retry %d: dropped %d msgs, %d remain)\n", ptl_retries,
                   old_eff - effective_drop, effective_drop);
    }

    strbuf_t summary;
    strbuf_init(&summary);

    bool compact_got_llm = false;
    if (llm_summary) {
        compact_got_llm = true;
        /* Store for iterative updates on subsequent compactions */
        free(g_prev_compact_summary);
        g_prev_compact_summary = nc_xstrdup(llm_summary);

        strbuf_append_str(&summary, compact_tpl_summary_prefix());
        strbuf_append_str(&summary, "\n\n");

        const char *sm_raw = session_memory_content();
        const char *sm_for_anchor = (sm_raw && sm_raw[0]) ? sm_raw : NULL;
        char *anchor = compact_build_memory_anchor(sm_for_anchor, llm_summary);
        free(llm_summary);
        llm_summary = NULL;
        if (anchor) {
            strbuf_append_str(&summary, anchor);
            free(anchor);
        }

        strbuf_append_str(&summary,
                          "\n\nContinue the conversation from where it left off without "
                          "asking the user any further questions. Resume directly — do not "
                          "acknowledge the summary, do not recap what was happening, do not "
                          "preface with \"I'll continue\" or similar. Pick up the last task "
                          "as if the break never happened.");
    } else {
        g_compact_failures++;
        auto_compact_record_failure();
        strbuf_append_str(&summary, "[Earlier conversation compacted. Key context: ");
        for (int i = 0; i < drop; i++) {
            if (conv->msgs[i].role == ROLE_ASSISTANT) {
                for (int j = 0; j < conv->msgs[i].num_blocks; j++) {
                    if (conv->msgs[i].blocks[j].type == CONTENT_TEXT &&
                        conv->msgs[i].blocks[j].text) {
                        size_t tlen = strlen(conv->msgs[i].blocks[j].text);
                        if (tlen > CONTEXT_SUMMARY_TRUNC_LEN)
                            tlen = CONTEXT_SUMMARY_TRUNC_LEN;
                        strbuf_append(&summary, conv->msgs[i].blocks[j].text, tlen);
                        strbuf_append_str(&summary, "... ");
                    }
                }
            }
        }
        strbuf_append_str(&summary, "]");
    }

    int old_len = conv->len;
    conversation_replace_head(conv, drop, summary.data);
    strbuf_free(&summary);

    /* Fix orphaned tool_call / tool_result pairs left by message removal */
    int pair_fixes = sanitize_tool_pairs(conv);
    if (pair_fixes > 0)
        term_color(T_DIM, "(fixed %d orphaned tool pair%s)\n", pair_fixes,
                   pair_fixes > 1 ? "s" : "");

    char *file_restore = compact_restore_files(&g_read_tracker);
    if (file_restore) {
        message_t *restore_msg = conversation_add(conv, ROLE_USER);
        message_add_text(restore_msg, file_restore);
        free(file_restore);
    }

    int new_est = estimate_conversation_tokens(conv->msgs, conv->len, conv->system_prompt);
    int l4_freed = (est - new_est) * APPROX_BYTES_PER_TOKEN;
    if (l4_freed > 0)
        compact_metrics_record(4, l4_freed);
    term_color(T_DIM, "(auto-compacted: %d -> %d messages, ~%dk -> ~%dk tokens)\n", old_len,
               conv->len, est / 1000, new_est / 1000);
    rollout_record_compaction(est, new_est);
    if (compact_got_llm || g_compact_failures == 0) {
        g_compact_failures = 0;
        auto_compact_reset_circuit();
    }

    if (auto_compact_should_warn_degradation())
        term_color(T_YELLOW, "Note: Multiple compactions may reduce accuracy. "
                             "Consider starting a new session for best results.\n");

    return 1;
}

api_error_class_t classify_api_error(const char *msg) {
    if (!msg)
        return API_ERR_UNKNOWN;
    if (strstr(msg, HTTP_ERR_CONTEXT_LENGTH) || strstr(msg, "context length") ||
        strstr(msg, AERR_BODY_TOO_LONG) || strstr(msg, "too many tokens") ||
        strstr(msg, "max_tokens_exceeded") || (strstr(msg, "maximum") && strstr(msg, "token")) ||
        strstr(msg, "prompt is too long") || strstr(msg, "reduce the length") ||
        strstr(msg, "context window"))
        return API_ERR_CONTEXT_OVERFLOW;
    if (strstr(msg, HTTP_ERR_RATE_LIMIT) || strstr(msg, HTTP_ERR_RATE_LIMIT_SP) ||
        strstr(msg, HTTP_STR_429) || strstr(msg, HTTP_ERR_TOO_MANY) || strstr(msg, "quota") ||
        strstr(msg, "throttl"))
        return API_ERR_RATE_LIMIT;
    if (strstr(msg, HTTP_ERR_OVERLOADED) || strstr(msg, HTTP_STR_503) ||
        strstr(msg, HTTP_STR_529) || strstr(msg, "capacity") ||
        strstr(msg, "temporarily unavailable") || strstr(msg, "busy"))
        return API_ERR_OVERLOADED;
    if (strstr(msg, HTTP_STR_401) || strstr(msg, HTTP_STR_403) || strstr(msg, "Unauthorized") ||
        strstr(msg, "Forbidden") || strstr(msg, "authentication") ||
        strstr(msg, "invalid_api_key") || strstr(msg, "invalid api key") ||
        strstr(msg, AERR_BODY_INVALID_KEY) || strstr(msg, AERR_BODY_KEY_INVALID))
        return API_ERR_AUTH;
    if (strstr(msg, AERR_BODY_TURN_DEADLINE) || strstr(msg, AERR_BODY_WC_DEADLINE) ||
        strstr(msg, AERR_BODY_CONSEC_TURNS) || strstr(msg, AERR_BODY_CTX_DEGRADED))
        return API_ERR_WATCHDOG;
    if (strstr(msg, NET_ERR_CURLE_PREFIX) || strstr(msg, NET_ERR_DNS_FAIL) ||
        strstr(msg, NET_ERR_CONN_REFUSED) || strstr(msg, NET_ERR_TIMED_OUT) ||
        strstr(msg, NET_ERR_SSL) || strstr(msg, NET_ERR_NETWORK))
        return API_ERR_NETWORK;
    if (strstr(msg, AERR_BODY_MODEL_MISSING) || strstr(msg, AERR_BODY_NOT_EXIST) ||
        strstr(msg, AERR_BODY_UNKNOWN_MODEL) || strstr(msg, AERR_BODY_NOT_AVAILABLE))
        return API_ERR_MODEL_NOT_FOUND;
    if (strstr(msg, HTTP_ERR_INVALID) || strstr(msg, HTTP_STR_400) ||
        strstr(msg, HTTP_ERR_BAD_REQUEST) || strstr(msg, AERR_BODY_MALFORMED))
        return API_ERR_INVALID_REQUEST;
    return API_ERR_UNKNOWN;
}

const char *api_error_hint(api_error_class_t cls) {
    switch (cls) {
    case API_ERR_CONTEXT_OVERFLOW:
        return "Context too large. Use /compact to reduce.";
    case API_ERR_RATE_LIMIT:
        return "Rate limited. Waiting before retry...";
    case API_ERR_OVERLOADED:
        return "Server overloaded. Retrying shortly...";
    case API_ERR_AUTH:
        return "Authentication failed. Check your API key.";
    case API_ERR_NETWORK:
        return "Network error. Check connection and base URL.";
    case API_ERR_WATCHDOG:
        return "Turn deadline exceeded. Auto-retrying with a simpler approach...";
    case API_ERR_MODEL_NOT_FOUND:
        return "Model not found. Use /model to change.";
    case API_ERR_INVALID_REQUEST:
        return "Invalid request. This may be a bug.";
    default:
        return NULL;
    }
}
