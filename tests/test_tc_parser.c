#include "test.h"
#include "nevercode.h"

static int test_init(void) {
    ASSERT_TRUE(tc_parser_count() > 0);
    return 0;
}

static int test_list(void) {
    const char *list = tc_parser_list();
    ASSERT_NOT_NULL(list);
    ASSERT_TRUE(strstr(list, NC_TC_PARSER_XML_JSON_TOOL) != NULL);
    ASSERT_TRUE(strstr(list, "deepseek") != NULL);
    ASSERT_TRUE(strstr(list, "mistral") != NULL);
    return 0;
}

static int test_xml_tool_json_parse(void) {
    const char *input = "I'll read the file now.\n"
                        "<tool_call>\n"
                        "{\"name\": \"read_file\", \"arguments\": {\"path\": \"/tmp/x.c\"}}\n"
                        "</tool_call>";

    tc_parse_result_t r;
    ASSERT_INT_EQ(tc_parser_parse(NC_TC_PARSER_XML_JSON_TOOL, input, &r), 0);
    ASSERT_INT_EQ(r.n_calls, 1);
    ASSERT_STR_EQ(r.calls[0].name, "read_file");
    ASSERT_TRUE(strstr(r.calls[0].arguments, "/tmp/x.c") != NULL);
    ASSERT_TRUE(strstr(r.content, "read the file") != NULL);
    return 0;
}

static int test_xml_tool_json_multiple(void) {
    const char *input =
        "Let me do two things.\n"
        "<tool_call>\n"
        "{\"name\": \"read_file\", \"arguments\": {\"path\": \"a.c\"}}\n"
        "</tool_call>\n"
        "<tool_call>\n"
        "{\"name\": \"write_file\", \"arguments\": {\"path\": \"b.c\", \"content\": \"hi\"}}\n"
        "</tool_call>";

    tc_parse_result_t r;
    ASSERT_INT_EQ(tc_parser_parse(NC_TC_PARSER_XML_JSON_TOOL, input, &r), 0);
    ASSERT_INT_EQ(r.n_calls, 2);
    ASSERT_STR_EQ(r.calls[0].name, "read_file");
    ASSERT_STR_EQ(r.calls[1].name, "write_file");
    return 0;
}

static int test_xml_tool_json_no_match(void) {
    tc_parse_result_t r;
    ASSERT_TRUE(tc_parser_parse(NC_TC_PARSER_XML_JSON_TOOL, "just plain text", &r) != 0);
    return 0;
}

static int test_qwen_coder(void) {
    const char *input = "Sure, let me search.\n"
                        "<tool_call>\n"
                        "<function=grep>\n"
                        "<parameter=pattern>TODO</parameter>\n"
                        "<parameter=path>/src</parameter>\n"
                        "</function>\n"
                        "</tool_call>";

    tc_parse_result_t r;
    ASSERT_INT_EQ(tc_parser_parse("qwen3_coder", input, &r), 0);
    ASSERT_INT_EQ(r.n_calls, 1);
    ASSERT_STR_EQ(r.calls[0].name, "grep");
    ASSERT_TRUE(strstr(r.calls[0].arguments, "TODO") != NULL);
    ASSERT_TRUE(strstr(r.calls[0].arguments, "/src") != NULL);
    return 0;
}

static int test_llama_json(void) {
    const char *input = "I'll check the file.\n"
                        "<|python_tag|>\n"
                        "{\"name\": \"bash\", \"arguments\": {\"command\": \"ls -la\"}}";

    tc_parse_result_t r;
    ASSERT_INT_EQ(tc_parser_parse("llama_json", input, &r), 0);
    ASSERT_INT_EQ(r.n_calls, 1);
    ASSERT_STR_EQ(r.calls[0].name, "bash");
    ASSERT_TRUE(strstr(r.calls[0].arguments, "ls -la") != NULL);
    return 0;
}

static int test_llama_json_params_key(void) {
    const char *input = "{\"name\": \"read\", \"parameters\": {\"file\": \"x.c\"}}";
    tc_parse_result_t r;
    ASSERT_INT_EQ(tc_parser_parse("llama_json", input, &r), 0);
    ASSERT_INT_EQ(r.n_calls, 1);
    ASSERT_STR_EQ(r.calls[0].name, "read");
    return 0;
}

static int test_kimi_k2(void) {
    const char *input = "OK let me help.\n"
                        "<|tool_calls_section_begin|>\n"
                        "<|tool_call_begin|>functions.get_weather:0<|tool_call_argument_begin|>"
                        "{\"city\": \"Beijing\"}<|tool_call_end|>\n"
                        "<|tool_calls_section_end|>";

    tc_parse_result_t r;
    ASSERT_INT_EQ(tc_parser_parse("kimi_k2", input, &r), 0);
    ASSERT_INT_EQ(r.n_calls, 1);
    ASSERT_STR_EQ(r.calls[0].name, "get_weather");
    ASSERT_TRUE(strstr(r.calls[0].arguments, "Beijing") != NULL);
    return 0;
}

static int test_mistral_pre_v11(void) {
    const char *input = "Here is the result.\n"
                        "[TOOL_CALLS] [{\"name\": \"search\", \"arguments\": {\"q\": \"test\"}}]";

    tc_parse_result_t r;
    ASSERT_INT_EQ(tc_parser_parse("mistral", input, &r), 0);
    ASSERT_INT_EQ(r.n_calls, 1);
    ASSERT_STR_EQ(r.calls[0].name, "search");
    return 0;
}

static int test_mistral_v11(void) {
    const char *input = "Running now.\n"
                        "[TOOL_CALLS]bash{\"command\": \"pwd\"}";

    tc_parse_result_t r;
    ASSERT_INT_EQ(tc_parser_parse("mistral", input, &r), 0);
    ASSERT_INT_EQ(r.n_calls, 1);
    ASSERT_STR_EQ(r.calls[0].name, "bash");
    return 0;
}

static int test_auto_detect_xml_tool_json(void) {
    const char *input = "<tool_call>\n"
                        "{\"name\": \"glob\", \"arguments\": {\"pattern\": \"*.c\"}}\n"
                        "</tool_call>";

    tc_parse_result_t r;
    ASSERT_INT_EQ(tc_parser_auto(input, &r), 0);
    ASSERT_INT_EQ(r.n_calls, 1);
    ASSERT_STR_EQ(r.calls[0].name, "glob");
    return 0;
}

static int test_auto_detect_mistral(void) {
    const char *input = "[TOOL_CALLS] [{\"name\": \"ls\", \"arguments\": {}}]";
    tc_parse_result_t r;
    ASSERT_INT_EQ(tc_parser_auto(input, &r), 0);
    ASSERT_INT_EQ(r.n_calls, 1);
    return 0;
}

static int test_unknown_parser(void) {
    tc_parse_result_t r;
    ASSERT_TRUE(tc_parser_parse("nonexistent", "text", &r) != 0);
    return 0;
}

static int test_null_safety(void) {
    ASSERT_TRUE(tc_parser_parse(NULL, "text", NULL) != 0);
    ASSERT_TRUE(tc_parser_auto(NULL, NULL) != 0);
    return 0;
}

static int test_content_preserved(void) {
    const char *input = "Here is my analysis of the code.\n\n"
                        "<tool_call>\n"
                        "{\"name\": \"edit\", \"arguments\": {\"file\": \"x.c\"}}\n"
                        "</tool_call>";

    tc_parse_result_t r;
    ASSERT_INT_EQ(tc_parser_parse(NC_TC_PARSER_XML_JSON_TOOL, input, &r), 0);
    ASSERT_TRUE(strstr(r.content, "analysis") != NULL);
    ASSERT_INT_EQ(r.n_calls, 1);
    return 0;
}

static int test_glm45(void) {
    const char *input = "<tool_call>read_file\n"
                        "<arg_key>path</arg_key><arg_value>\"src/main.c\"</arg_value>\n"
                        "<arg_key>offset</arg_key><arg_value>10</arg_value>\n"
                        "</tool_call>";
    tc_parse_result_t r;
    ASSERT_INT_EQ(tc_parser_parse("glm45", input, &r), 0);
    ASSERT_INT_EQ(r.n_calls, 1);
    ASSERT_STR_EQ(r.calls[0].name, "read_file");
    ASSERT_TRUE(strstr(r.calls[0].arguments, "src/main.c") != NULL);
    ASSERT_TRUE(strstr(r.calls[0].arguments, "10") != NULL);
    return 0;
}

static int test_glm47(void) {
    const char *input = "Let me check the file.\n"
                        "<tool_call>bash\n"
                        "<arg_key>command</arg_key>\n<arg_value>ls -la</arg_value>\n"
                        "</tool_call>";
    tc_parse_result_t r;
    ASSERT_INT_EQ(tc_parser_parse("glm47", input, &r), 0);
    ASSERT_INT_EQ(r.n_calls, 1);
    ASSERT_STR_EQ(r.calls[0].name, "bash");
    ASSERT_TRUE(strstr(r.calls[0].arguments, "ls -la") != NULL);
    ASSERT_TRUE(strstr(r.content, "check the file") != NULL);
    return 0;
}

static int test_longcat(void) {
    const char *input = "Here is the result.\n"
                        "<longcat_tool_call>\n"
                        "{\"name\": \"write_file\", \"arguments\": {\"path\": \"out.txt\", "
                        "\"content\": \"hello\"}}\n"
                        "</longcat_tool_call>";
    tc_parse_result_t r;
    ASSERT_INT_EQ(tc_parser_parse("longcat", input, &r), 0);
    ASSERT_INT_EQ(r.n_calls, 1);
    ASSERT_STR_EQ(r.calls[0].name, "write_file");
    ASSERT_TRUE(strstr(r.calls[0].arguments, "out.txt") != NULL);
    ASSERT_TRUE(strstr(r.content, "result") != NULL);
    return 0;
}

static int test_qwen_alias(void) {
    const char *input = "<tool_call>\n"
                        "{\"name\": \"bash\", \"arguments\": {\"command\": \"pwd\"}}\n"
                        "</tool_call>";
    tc_parse_result_t r;
    ASSERT_INT_EQ(tc_parser_parse("qwen", input, &r), 0);
    ASSERT_INT_EQ(r.n_calls, 1);
    ASSERT_STR_EQ(r.calls[0].name, "bash");
    return 0;
}

int test_tc_parser_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tc_parser");
    RUN_TEST(test_init);
    RUN_TEST(test_list);
    RUN_TEST(test_xml_tool_json_parse);
    RUN_TEST(test_xml_tool_json_multiple);
    RUN_TEST(test_xml_tool_json_no_match);
    RUN_TEST(test_qwen_coder);
    RUN_TEST(test_llama_json);
    RUN_TEST(test_llama_json_params_key);
    RUN_TEST(test_kimi_k2);
    RUN_TEST(test_mistral_pre_v11);
    RUN_TEST(test_mistral_v11);
    RUN_TEST(test_auto_detect_xml_tool_json);
    RUN_TEST(test_auto_detect_mistral);
    RUN_TEST(test_unknown_parser);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_content_preserved);
    RUN_TEST(test_glm45);
    RUN_TEST(test_glm47);
    RUN_TEST(test_longcat);
    RUN_TEST(test_qwen_alias);
    TEST_RETURN_FAILED();
}
