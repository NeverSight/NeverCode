#include "nevercode.h"
#include "conversation.h"

/* ---- Read file tracker ---- */

void read_file_tracker_init(read_file_tracker_t *t) {
    memset(t, 0, sizeof(*t));
}

void read_file_tracker_free(read_file_tracker_t *t) {
    for (int i = 0; i < t->count; i++)
        free(t->paths[i]);
    t->count = 0;
}

void read_file_tracker_add(read_file_tracker_t *t, const char *path) {
    if (!t || !path || !path[0])
        return;

    for (int i = 0; i < t->count; i++) {
        if (strcmp(t->paths[i], path) == 0) {
            /* Move to end (most recent) */
            char *tmp = t->paths[i];
            memmove(&t->paths[i], &t->paths[i + 1], (t->count - i - 1) * sizeof(char *));
            t->paths[t->count - 1] = tmp;
            return;
        }
    }

    if (t->count >= READ_FILE_TRACKER_MAX) {
        free(t->paths[0]);
        memmove(&t->paths[0], &t->paths[1], (READ_FILE_TRACKER_MAX - 1) * sizeof(char *));
        t->count = READ_FILE_TRACKER_MAX - 1;
    }
    t->paths[t->count++] = nc_xstrdup(path);
}

int read_file_tracker_get_recent(const read_file_tracker_t *t, const char **out, int max) {
    if (!t || !out || max <= 0)
        return 0;
    int n = 0;
    for (int i = t->count - 1; i >= 0 && n < max; i--)
        out[n++] = t->paths[i];
    return n;
}

/* ---- Post-compact file restoration ---- */

static char *read_file_head(const char *path, int max_chars) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return NULL;

    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;

    int to_read = (int)st.st_size;
    if (to_read > max_chars)
        to_read = max_chars;

    char *buf = malloc(to_read + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, to_read, f);
    fclose(f);
    buf[n] = '\0';

    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c == 0 || (c < 32 && c != '\n' && c != '\r' && c != '\t')) {
            free(buf);
            return NULL;
        }
    }
    return buf;
}

char *compact_restore_files(const read_file_tracker_t *tracker) {
    if (!tracker || tracker->count == 0)
        return NULL;

    const char *recent[POST_COMPACT_MAX_FILES];
    int n = read_file_tracker_get_recent(tracker, recent, POST_COMPACT_MAX_FILES);
    if (n == 0)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    int restored = 0;

    strbuf_append_str(&sb, "[Post-compaction context: key files re-read for continuity]\n\n");

    for (int i = 0; i < n; i++) {
        char *content = read_file_head(recent[i], POST_COMPACT_MAX_PER_FILE);
        if (!content)
            continue;

        size_t clen = strlen(content);
        if (clen < 10) {
            free(content);
            continue;
        }

        strbuf_append_str(&sb, "<file path=\"");
        strbuf_append_str(&sb, recent[i]);
        strbuf_append_str(&sb, "\">\n");
        strbuf_append_str(&sb, content);
        if ((int)clen >= POST_COMPACT_MAX_PER_FILE)
            strbuf_append_str(&sb, "\n... (truncated)");
        strbuf_append_str(&sb, "\n</file>\n\n");
        free(content);
        restored++;
    }

    if (restored == 0) {
        strbuf_free(&sb);
        return NULL;
    }
    return strbuf_detach(&sb);
}

/* ---- Message grouping by API round ---- */

int compact_group_by_api_round(const message_t *msgs, int n, msg_group_t *groups, int max_groups) {
    if (!msgs || n == 0 || !groups || max_groups == 0)
        return 0;

    int ng = 0;
    int current_start = 0;
    bool in_assistant = false;

    for (int i = 0; i < n; i++) {
        if (msgs[i].role == ROLE_ASSISTANT && in_assistant == false && i > 0) {
            if (ng < max_groups) {
                groups[ng].start = current_start;
                groups[ng].end = i;
                ng++;
            }
            current_start = i;
        }
        in_assistant = (msgs[i].role == ROLE_ASSISTANT);
    }

    if (current_start < n && ng < max_groups) {
        groups[ng].start = current_start;
        groups[ng].end = n;
        ng++;
    }
    return ng;
}

/* ---- Strip images from compact input ---- */

int compact_strip_images(message_t *msgs, int n) {
    int stripped = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < msgs[i].num_blocks; j++) {
            content_block_t *b = &msgs[i].blocks[j];
            if (b->type == CONTENT_IMAGE) {
                free(b->image_data);
                b->image_data = NULL;
                free(b->image_media_type);
                b->image_media_type = NULL;
                b->type = CONTENT_TEXT;
                b->text = nc_xstrdup("[image]");
                stripped++;
            }
        }
    }
    return stripped;
}

/* ---- Strip <thinking> blocks from old assistant messages ---- */

int compact_prune_thinking(message_t *msgs, int n, int keep_recent) {
    if (!msgs || n <= 0)
        return 0;

    int cutoff = n - keep_recent;
    if (cutoff <= 0)
        return 0;

    int pruned = 0;
    for (int i = 0; i < cutoff; i++) {
        if (msgs[i].role != ROLE_ASSISTANT)
            continue;
        for (int j = 0; j < msgs[i].num_blocks; j++) {
            content_block_t *b = &msgs[i].blocks[j];
            if (b->type != CONTENT_TEXT || !b->text)
                continue;

            if (!strstr(b->text, THINKING_TAG_OPEN))
                continue;

            strbuf_t sb;
            strbuf_init(&sb);
            char *cur = b->text;
            while (cur && *cur) {
                char *tag_open = strstr(cur, THINKING_TAG_OPEN);
                if (!tag_open) {
                    strbuf_append_str(&sb, cur);
                    break;
                }
                if (tag_open > cur)
                    strbuf_append(&sb, cur, (size_t)(tag_open - cur));

                char *tag_close = strstr(tag_open, THINKING_TAG_CLOSE);
                if (!tag_close) {
                    strbuf_append_str(&sb, tag_open);
                    break;
                }
                cur = tag_close + THINKING_TAG_CLOSE_LEN;
                pruned++;
            }

            free(b->text);
            b->text = strbuf_detach(&sb);
        }
    }
    return pruned;
}

/* ---- Session memory injection at compaction boundary ---- */

char *compact_build_memory_anchor(const char *session_memories, const char *summary) {
    if (!session_memories && !summary)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_str(&sb, COMPACT_PREV_SUMMARY_MARKER "\n\n");

    if (session_memories && session_memories[0]) {
        strbuf_append_str(&sb, "<session_context>\n");
        strbuf_append_str(&sb, session_memories);
        strbuf_append_str(&sb, "\n</session_context>\n\n");
    }

    if (summary && summary[0]) {
        strbuf_append_str(&sb, "<prior_work_summary>\n");
        strbuf_append_str(&sb, summary);
        strbuf_append_str(&sb, "\n</prior_work_summary>\n");
    }

    return strbuf_detach(&sb);
}

/* ---- Resume summary ---- */

char *generate_resume_summary(const message_t *msgs, int n, const char *session_name) {
    if (!msgs || n == 0)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);

    if (session_name && session_name[0]) {
        strbuf_append_str(&sb, "Session: ");
        strbuf_append_str(&sb, session_name);
        strbuf_append_str(&sb, "\n");
    }

    /* Find last user message and last assistant response */
    const char *last_user_text = NULL;
    const char *last_asst_text = NULL;
    bool found_asst = false;

    for (int i = n - 1; i >= 0; i--) {
        if (msgs[i].role == ROLE_ASSISTANT && !found_asst) {
            for (int j = 0; j < msgs[i].num_blocks; j++) {
                if (msgs[i].blocks[j].type == CONTENT_TEXT && msgs[i].blocks[j].text) {
                    last_asst_text = msgs[i].blocks[j].text;
                    found_asst = true;
                    break;
                }
            }
        }
        if (msgs[i].role == ROLE_USER && !last_user_text) {
            for (int j = 0; j < msgs[i].num_blocks; j++) {
                if (msgs[i].blocks[j].type == CONTENT_TEXT && msgs[i].blocks[j].text) {
                    last_user_text = msgs[i].blocks[j].text;
                    break;
                }
            }
        }
        if (last_user_text && found_asst)
            break;
    }

    /* Count total turns and tool calls */
    int user_turns = 0, asst_turns = 0, total_tool_calls = 0;
    for (int i = 0; i < n; i++) {
        if (msgs[i].role == ROLE_USER)
            user_turns++;
        else if (msgs[i].role == ROLE_ASSISTANT) {
            asst_turns++;
            for (int j = 0; j < msgs[i].num_blocks; j++)
                if (msgs[i].blocks[j].type == CONTENT_TOOL_USE)
                    total_tool_calls++;
        }
    }

    strbuf_append_str(&sb, "While you were away:\n");

    if (last_user_text) {
        strbuf_append_str(&sb, "  Last request: ");
        size_t tlen = strlen(last_user_text);
        if (tlen > NC_PREVIEW_SHORT) {
            strbuf_append(&sb, last_user_text, NC_PREVIEW_SHORT);
            strbuf_append_str(&sb, "...");
        } else {
            strbuf_append_str(&sb, last_user_text);
        }
        strbuf_append_str(&sb, "\n");
    }

    if (last_asst_text) {
        strbuf_append_str(&sb, "  Last response: ");
        size_t tlen = strlen(last_asst_text);
        if (tlen > COMPACT_LAST_REPLY_PREVIEW) {
            strbuf_append(&sb, last_asst_text, COMPACT_LAST_REPLY_PREVIEW);
            strbuf_append_str(&sb, "...");
        } else {
            strbuf_append_str(&sb, last_asst_text);
        }
        strbuf_append_str(&sb, "\n");
    }

    char stats[NC_LABEL_MAX];
    snprintf(stats, sizeof(stats), "  Progress: %d exchanges, %d tool calls\n",
             NC_MIN(user_turns, asst_turns), total_tool_calls);
    strbuf_append_str(&sb, stats);

    return strbuf_detach(&sb);
}
