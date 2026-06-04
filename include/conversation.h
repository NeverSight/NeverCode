#ifndef CONVERSATION_H
#define CONVERSATION_H

#include "nevercode.h"

struct conversation_s {
    message_t *msgs;
    int len;
    int cap;
    char *system_prompt;
};

void conversation_init(conversation_t *c);
void conversation_free(conversation_t *c);
/* Drop all messages; keep system_prompt (e.g. between batch prompts). */
void conversation_clear_messages(conversation_t *c);

message_t *conversation_add(conversation_t *c, message_role_t role);
void conversation_add_user_text(conversation_t *c, const char *text);

/* Remove message at index, shifting subsequent messages down. */
void conversation_remove_at(conversation_t *c, int idx);

/* Insert a new message at index, shifting subsequent messages up. */
message_t *conversation_insert_at(conversation_t *c, int idx, message_role_t role);

/* Drop a contiguous range [start, start+count) and shift the tail down.
 * Frees the removed messages.  Returns number of messages actually removed. */
int conversation_drop_range(conversation_t *c, int start, int count);

/* Drop messages [0..drop_count), replace with a single summary at index 0.
 * Common pattern during context compaction.  Returns new length. */
int conversation_replace_head(conversation_t *c, int drop_count, const char *summary_text);

void message_init(message_t *m, message_role_t role);
void message_add_text(message_t *m, const char *text);
void message_add_image(message_t *m, const char *base64_data, const char *media_type);
void message_add_tool_use(message_t *m, const char *id, const char *name, cJSON *input);
void message_add_tool_result(message_t *m, const char *id, const char *content, int is_error);
void message_free(message_t *m);

/* Add a user message with mixed text + image content blocks */
message_t *conversation_add_user_multimodal(conversation_t *c);

/* Context compression (needs conversation_t) */
int context_snip_range(conversation_t *conv, int from, int to);
int context_micro_compact(conversation_t *conv, time_t last_assistant_time, int cache_timeout_sec);

#endif /* CONVERSATION_H */
