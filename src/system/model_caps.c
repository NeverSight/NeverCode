/*
 * model_caps.c — Model capability queries
 *
 * Thin wrapper over model_registry: every public function does a
 * pattern-match lookup and returns the relevant field.
 */

#include "nevercode.h"
#include "model_registry.h"

bool model_supports_thinking(const char *model) {
    const model_info_t *m = model_registry_match(model);
    return m ? m->supports_thinking : false;
}

bool model_supports_adaptive_thinking(const char *model) {
    const model_info_t *m = model_registry_match(model);
    return m ? m->thinking.adaptive : false;
}

const char *model_provider(const char *model) {
    const model_info_t *m = model_registry_match(model);
    return m ? m->owned_by : NULL;
}

bool model_supports_vision(const char *model) {
    const model_info_t *m = model_registry_match(model);
    return m ? m->supports_vision : false;
}

bool model_supports_tools(const char *model) {
    const model_info_t *m = model_registry_match(model);
    return m ? m->supports_tools : true;
}

bool model_supports_json_mode(const char *model) {
    const model_info_t *m = model_registry_match(model);
    return m ? m->supports_json_mode : false;
}

bool model_uses_developer_role(const char *model) {
    const model_info_t *m = model_registry_match(model);
    return m ? m->uses_developer_role : false;
}

bool model_uses_max_completion_tokens(const char *model) {
    const model_info_t *m = model_registry_match(model);
    return m ? m->uses_max_completion_tokens_param : false;
}

int model_context_window(const char *model) {
    const model_info_t *m = model_registry_match(model);
    return m ? m->context_length : DEFAULT_CONTEXT_FALLBACK;
}

int model_max_output(const char *model) {
    const model_info_t *m = model_registry_match(model);
    return m ? m->max_output_tokens : DEFAULT_MAX_TOKENS;
}

int model_input_limit(const char *model) {
    const model_info_t *m = model_registry_match(model);
    if (!m)
        return DEFAULT_CONTEXT_FALLBACK - DEFAULT_MAX_TOKENS;
    if (m->max_input_tokens > 0)
        return m->max_input_tokens;
    return m->context_length - m->max_output_tokens;
}

char *model_caps_describe(const char *model) {
    const model_info_t *m = model_registry_match(model);
    if (!m) {
        strbuf_t sb;
        strbuf_init(&sb);
        strbuf_appendf(&sb, "Model '%s': unknown (defaults: %dK context, %dK output)", model,
                       DEFAULT_CONTEXT_FALLBACK / 1024, DEFAULT_MAX_TOKENS / 1024);
        return strbuf_detach(&sb);
    }

    strbuf_t sb;
    strbuf_init(&sb);
    int ilim =
        m->max_input_tokens > 0 ? m->max_input_tokens : m->context_length - m->max_output_tokens;
    strbuf_appendf(&sb,
                   "Model '%s': context=%dK input=%dK output=%dK thinking=%s vision=%s "
                   "tools=%s json=%s dev_role=%s",
                   model, m->context_length / 1024, ilim / 1024, m->max_output_tokens / 1024,
                   m->supports_thinking ? "yes" : "no", m->supports_vision ? "yes" : "no",
                   m->supports_tools ? "yes" : "no", m->supports_json_mode ? "yes" : "no",
                   m->uses_developer_role ? "yes" : "no");
    return strbuf_detach(&sb);
}
