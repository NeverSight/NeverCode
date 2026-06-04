/*
 * send_msg.c — Send a message to a running sub-agent.
 *
 * Attempts direct pool delivery first; falls back to the
 * mailbox registry for asynchronous queuing.
 */

#include "nevercode.h"
#include "tools.h"

int send_msg_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    if (!input || !output)
        return -1;

    const cJSON *tg = cJSON_GetObjectItem(input, "agent_id");
    const cJSON *bd = cJSON_GetObjectItem(input, "message");

    if (!tg || !cJSON_IsString(tg) || !tg->valuestring[0]) {
        *output = nc_xstrdup("error: missing 'agent_id' or 'message'");
        return 1;
    }
    if (!bd || !cJSON_IsString(bd) || !bd->valuestring[0]) {
        *output = nc_xstrdup("error: missing 'agent_id' or 'message'");
        return 1;
    }

    agent_pool_t *pool = agent_pool_global();
    int rc = agent_pool_send_message(pool, tg->valuestring, bd->valuestring);

    strbuf_t sb;
    strbuf_init(&sb);

    if (rc == 0) {
        strbuf_appendf(&sb, "Message delivered to agent %s (%zu bytes)", tg->valuestring,
                       strlen(bd->valuestring));
    } else {
        agent_mailbox_t *mb = agent_mailbox_registry_find(tg->valuestring);
        if (!mb) {
            strbuf_appendf(&sb, "Agent '%s' not found", tg->valuestring);
            *output = strbuf_detach(&sb);
            return 1;
        }
        const char *sender = state_session_id();
        if (!sender)
            sender = "coordinator";
        agent_mailbox_send(mb, sender, bd->valuestring);
        strbuf_appendf(&sb, "Message queued to agent %s via mailbox", tg->valuestring);
    }

    *output = strbuf_detach(&sb);
    return 0;
}
