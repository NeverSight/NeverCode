#include "nevercode.h"
#include "conversation.h"

/*
 * Thread Rollout Truncation — smart conversation truncation strategies
 * that preserve message integrity and tool_use/tool_result pairing.
 *
 * Removes oldest API rounds while ensuring tool calls always have
 * matching results.
 */

int thread_truncate_rounds(message_t *msgs, int n, int rounds_to_remove) {
    if (!msgs || n <= 0 || rounds_to_remove <= 0)
        return 0;

    msg_group_t groups[CTX_MAX_MSG_GROUPS];
    int ng = compact_group_by_api_round(msgs, n, groups, CTX_MAX_MSG_GROUPS);
    if (ng <= rounds_to_remove)
        return 0;

    int cut_at = groups[rounds_to_remove].start;
    if (cut_at <= 0 || cut_at >= n)
        return 0;

    return cut_at;
}

int thread_truncate_to_token_budget(message_t *msgs, int n, const char *sys_prompt, int budget) {
    if (!msgs || n <= 0 || budget <= 0)
        return 0;

    int total = estimate_conversation_tokens(msgs, n, sys_prompt);
    if (total <= budget)
        return 0;

    int excess = total - budget;

    msg_group_t groups[CTX_MAX_MSG_GROUPS];
    int ng = compact_group_by_api_round(msgs, n, groups, CTX_MAX_MSG_GROUPS);
    if (ng <= 1)
        return 0;

    int cut_at = 0;
    int tokens_freed = 0;

    for (int g = 0; g < ng - 1 && tokens_freed < excess; g++) {
        for (int i = groups[g].start; i < groups[g].end; i++) {
            tokens_freed += estimate_message_tokens(&msgs[i]);
        }
        cut_at = groups[g].end;
    }

    return cut_at;
}

static bool is_tool_result_orphaned(const message_t *msgs, int n, int start) {
    for (int i = start; i < n; i++) {
        if (msgs[i].role == ROLE_TOOL) {
            for (int j = 0; j < msgs[i].num_blocks; j++) {
                const content_block_t *b = &msgs[i].blocks[j];
                if (b->type == CONTENT_TOOL_RESULT && b->tool_use_id) {
                    bool found_use = false;
                    for (int k = start; k < i; k++) {
                        if (msgs[k].role != ROLE_ASSISTANT)
                            continue;
                        for (int m = 0; m < msgs[k].num_blocks; m++) {
                            if (msgs[k].blocks[m].type == CONTENT_TOOL_USE &&
                                msgs[k].blocks[m].tool_use_id &&
                                strcmp(msgs[k].blocks[m].tool_use_id, b->tool_use_id) == 0) {
                                found_use = true;
                                break;
                            }
                        }
                        if (found_use)
                            break;
                    }
                    if (!found_use)
                        return true;
                }
            }
        }
    }
    return false;
}

/*
 * Adjust the cut point backwards to keep consecutive assistant
 * messages together.  During extended-thinking sessions a single
 * API round may produce multiple assistant blocks (thinking chunk
 * followed by tool_use chunk).  Splitting them would lose context.
 */
static int adjust_for_consecutive_assistant(const message_t *msgs, int n, int cut) {
    if (cut <= 0 || cut >= n)
        return cut;

    /* If the first kept message is an assistant, pull back through
     * any preceding assistant messages (same logical response). */
    while (cut > 0 && msgs[cut].role == ROLE_ASSISTANT && msgs[cut - 1].role == ROLE_ASSISTANT)
        cut--;
    return cut;
}

int thread_find_safe_cut(const message_t *msgs, int n, int desired_cut) {
    if (!msgs || n <= 0 || desired_cut <= 0)
        return 0;
    if (desired_cut >= n)
        return n - 1;

    int cut = desired_cut;

    /* Phase 1: find nearest orphan-free boundary */
    while (cut < n - 1) {
        if (!is_tool_result_orphaned(msgs, n, cut))
            break;
        cut++;
    }
    if (cut >= n - 1) {
        cut = desired_cut;
        while (cut > 0) {
            if (!is_tool_result_orphaned(msgs, n, cut))
                break;
            cut--;
        }
    }

    /* Phase 2: keep consecutive assistant messages together */
    cut = adjust_for_consecutive_assistant(msgs, n, cut);

    return cut;
}

int thread_truncate_safe(message_t *msgs, int n, int rounds_to_remove) {
    int raw_cut = thread_truncate_rounds(msgs, n, rounds_to_remove);
    if (raw_cut <= 0)
        return 0;
    return thread_find_safe_cut(msgs, n, raw_cut);
}

char *thread_truncation_describe(const message_t *msgs, int n) {
    if (!msgs || n <= 0)
        return nc_xstrdup("(empty conversation)");

    msg_group_t groups[CTX_MAX_MSG_GROUPS];
    int ng = compact_group_by_api_round(msgs, n, groups, CTX_MAX_MSG_GROUPS);

    strbuf_t sb;
    strbuf_init(&sb);

    char line[NC_LABEL_MAX];
    snprintf(line, sizeof(line), "Conversation: %d messages, %d API rounds\n", n, ng);
    strbuf_append_str(&sb, line);

    int total_tokens = estimate_conversation_tokens(msgs, n, NULL);
    snprintf(line, sizeof(line), "Estimated tokens: %d\n", total_tokens);
    strbuf_append_str(&sb, line);

    for (int g = 0; g < ng && g < 10; g++) {
        int group_tokens = 0;
        int tool_calls = 0;
        for (int i = groups[g].start; i < groups[g].end; i++) {
            group_tokens += estimate_message_tokens(&msgs[i]);
            if (msgs[i].role == ROLE_ASSISTANT) {
                for (int j = 0; j < msgs[i].num_blocks; j++) {
                    if (msgs[i].blocks[j].type == CONTENT_TOOL_USE)
                        tool_calls++;
                }
            }
        }
        snprintf(line, sizeof(line), "  Round %d: msgs %d-%d (~%d tokens, %d tool calls)\n", g + 1,
                 groups[g].start, groups[g].end - 1, group_tokens, tool_calls);
        strbuf_append_str(&sb, line);
    }
    if (ng > 10) {
        snprintf(line, sizeof(line), "  ... and %d more rounds\n", ng - 10);
        strbuf_append_str(&sb, line);
    }

    return strbuf_detach(&sb);
}
