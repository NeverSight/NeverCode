#include "tools.h"
#include "nevercode.h"
#include "terminal.h"

static char *trim_ws(char *s) {
    if (!s)
        return s;
    while (*s && isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1)))
        *(--end) = '\0';
    return s;
}

static int ask_user_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    cJSON *questions = cJSON_GetObjectItem((cJSON *)input, "questions");
    if (!questions || !cJSON_IsArray(questions) || cJSON_GetArraySize(questions) == 0) {
        *output = nc_xstrdup("Error: 'questions' array is required and must not be empty");
        return 1;
    }

    int nq = cJSON_GetArraySize(questions);
    if (nq > NC_ASK_MAX_QUESTIONS)
        nq = NC_ASK_MAX_QUESTIONS;

    cJSON *answers_json = cJSON_CreateObject();
    int valid_questions = 0;

    for (int qi = 0; qi < nq; qi++) {
        cJSON *q = cJSON_GetArrayItem(questions, qi);
        const char *question = nc_cjson_str(q, "question", NULL);
        cJSON *options = cJSON_GetObjectItem(q, "options");
        cJSON *multi_sel = cJSON_GetObjectItem(q, "multiSelect");
        bool multi = multi_sel && cJSON_IsTrue(multi_sel);

        if (!question || !options || !cJSON_IsArray(options))
            continue;

        int nopts = cJSON_GetArraySize(options);
        if (nopts < 2 || nopts > NC_ASK_MAX_OPTIONS)
            continue;
        valid_questions++;

        printf("\n");
        term_color(T_BOLD, "  ? %s\n", question);

        for (int oi = 0; oi < nopts; oi++) {
            cJSON *opt = cJSON_GetArrayItem(options, oi);
            const char *label = nc_cjson_str(opt, "label", NULL);
            const char *desc = nc_cjson_str(opt, "description", NULL);
            if (!label)
                continue;
            term_color(T_CYAN, "    %d) %s", oi + 1, label);
            if (desc)
                term_color(T_DIM, " — %s", desc);
            printf("\n");
        }
        term_color(T_DIM, "    0) Other (type your own answer)\n");

        if (multi)
            term_color(T_DIM, "  Select one or more (comma-separated, e.g. 1,3): ");
        else
            term_color(T_DIM, "  Select [1-%d, 0=other]: ", nopts);

        char line[NC_MSG_MAX] = {0};
        if (!fgets(line, sizeof(line), stdin)) {
            cJSON_AddStringToObject(answers_json, question, "(no response)");
            continue;
        }
        size_t ll = strlen(line);
        while (ll > 0 && (line[ll - 1] == '\n' || line[ll - 1] == '\r'))
            line[--ll] = '\0';

        if (multi) {
            strbuf_t sel;
            strbuf_init(&sel);
            bool picked[NC_ASK_MAX_OPTIONS] = {0};
            char *tok = strtok(line, ",");
            int first = 1;
            while (tok) {
                tok = trim_ws(tok);
                int choice = nc_safe_atoi(tok, -1);
                if (choice >= 1 && choice <= nopts && !picked[choice - 1]) {
                    picked[choice - 1] = true;
                    cJSON *opt = cJSON_GetArrayItem(options, choice - 1);
                    const char *label = nc_cjson_str(opt, "label", NULL);
                    if (label) {
                        if (!first)
                            strbuf_append_str(&sel, ", ");
                        strbuf_append_str(&sel, label);
                        first = 0;
                    }
                }
                tok = strtok(NULL, ",");
            }
            if (sel.len == 0) {
                term_color(T_DIM, "  Enter your answer: ");
                char custom[NC_MSG_MAX] = {0};
                if (fgets(custom, sizeof(custom), stdin)) {
                    size_t cl = strlen(custom);
                    while (cl > 0 && (custom[cl - 1] == '\n' || custom[cl - 1] == '\r'))
                        custom[--cl] = '\0';
                    strbuf_append_str(&sel, custom);
                }
            }
            cJSON_AddStringToObject(answers_json, question, sel.data ? sel.data : "(none)");
            strbuf_free(&sel);
        } else {
            char *line_trim = trim_ws(line);
            int choice = nc_safe_atoi(line_trim, -1);
            if (choice >= 1 && choice <= nopts) {
                cJSON *opt = cJSON_GetArrayItem(options, choice - 1);
                const char *label = nc_cjson_str(opt, "label", NULL);
                cJSON_AddStringToObject(answers_json, question, label ? label : "(selected)");
            } else {
                if (choice == 0 || !isdigit((unsigned char)line_trim[0])) {
                    term_color(T_DIM, "  Enter your answer: ");
                    char custom[NC_MSG_MAX] = {0};
                    if (fgets(custom, sizeof(custom), stdin)) {
                        size_t cl = strlen(custom);
                        while (cl > 0 && (custom[cl - 1] == '\n' || custom[cl - 1] == '\r'))
                            custom[--cl] = '\0';
                        cJSON_AddStringToObject(answers_json, question, custom);
                    } else {
                        cJSON_AddStringToObject(answers_json, question, line_trim);
                    }
                } else {
                    cJSON_AddStringToObject(answers_json, question, line_trim);
                }
            }
        }
    }

    if (valid_questions == 0) {
        cJSON_Delete(answers_json);
        *output = nc_xstrdup("Error: no valid questions found (each question needs 2-4 options)");
        return 1;
    }

    strbuf_t out;
    strbuf_init(&out);
    strbuf_append_str(&out, "User has answered your questions: ");

    cJSON *item = NULL;
    int first = 1;
    cJSON_ArrayForEach(item, answers_json) {
        if (!first)
            strbuf_append_str(&out, ", ");
        strbuf_append_str(&out, "\"");
        strbuf_append_str(&out, item->string);
        strbuf_append_str(&out, "\"=\"");
        strbuf_append_str(&out, item->valuestring ? item->valuestring : "");
        strbuf_append_str(&out, "\"");
        first = 0;
    }
    strbuf_append_str(&out, ". You can now continue with the user's answers in mind.");

    *output = strbuf_detach(&out);
    cJSON_Delete(answers_json);
    return 0;
}

void tool_ask_user_register(void) {
    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");

    cJSON *props = cJSON_CreateObject();

    /* questions array */
    cJSON *q_arr = cJSON_CreateObject();
    cJSON_AddStringToObject(q_arr, "type", "array");
    cJSON *q_items = cJSON_CreateObject();
    cJSON_AddStringToObject(q_items, "type", "object");
    cJSON *q_props = cJSON_CreateObject();

    cJSON *q_question = cJSON_CreateObject();
    cJSON_AddStringToObject(q_question, "type", "string");
    cJSON_AddStringToObject(q_question, "description",
                            "The question to ask. Should be clear and end with ?");
    cJSON_AddItemToObject(q_props, "question", q_question);

    cJSON *q_header = cJSON_CreateObject();
    cJSON_AddStringToObject(q_header, "type", "string");
    cJSON_AddStringToObject(q_header, "description", "Short label (max 20 chars)");
    cJSON_AddItemToObject(q_props, "header", q_header);

    cJSON *q_options = cJSON_CreateObject();
    cJSON_AddStringToObject(q_options, "type", "array");
    cJSON *opt_items = cJSON_CreateObject();
    cJSON_AddStringToObject(opt_items, "type", "object");
    cJSON *opt_props = cJSON_CreateObject();
    cJSON *ol = cJSON_CreateObject();
    cJSON_AddStringToObject(ol, "type", "string");
    cJSON_AddItemToObject(opt_props, "label", ol);
    cJSON *od = cJSON_CreateObject();
    cJSON_AddStringToObject(od, "type", "string");
    cJSON_AddItemToObject(opt_props, "description", od);
    cJSON_AddItemToObject(opt_items, "properties", opt_props);
    cJSON *opt_req = cJSON_CreateArray();
    cJSON_AddItemToArray(opt_req, cJSON_CreateString("label"));
    cJSON_AddItemToArray(opt_req, cJSON_CreateString("description"));
    cJSON_AddItemToObject(opt_items, "required", opt_req);
    cJSON_AddItemToObject(q_options, "items", opt_items);
    cJSON_AddItemToObject(q_props, "options", q_options);

    cJSON *q_multi = cJSON_CreateObject();
    cJSON_AddStringToObject(q_multi, "type", "boolean");
    cJSON_AddItemToObject(q_props, "multiSelect", q_multi);

    cJSON_AddItemToObject(q_items, "properties", q_props);
    cJSON *q_req = cJSON_CreateArray();
    cJSON_AddItemToArray(q_req, cJSON_CreateString("question"));
    cJSON_AddItemToArray(q_req, cJSON_CreateString("options"));
    cJSON_AddItemToObject(q_items, "required", q_req);
    cJSON_AddItemToObject(q_arr, "items", q_items);
    cJSON_AddItemToObject(props, "questions", q_arr);

    cJSON_AddItemToObject(schema, "properties", props);
    cJSON *req = cJSON_CreateArray();
    cJSON_AddItemToArray(req, cJSON_CreateString("questions"));
    cJSON_AddItemToObject(schema, "required", req);

    tool_def_t t = {.name = "AskUserQuestion",
                    .description = "Ask the user one or more multiple-choice questions. "
                                   "Use this when you need user input to make a decision. "
                                   "Each question has 2-4 options with labels and descriptions. "
                                   "An 'Other' option is always added automatically.",
                    .input_schema = schema,
                    .execute = ask_user_exec,
                    .check_permission = NULL,
                    .read_only = true,
                    .concurrency_safe = true,
                    .destructive = false};
    tools_register(&t);
}
