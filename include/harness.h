/*
 * harness.h — Long-running session harness
 *
 * Infrastructure for multi-hour agent runs: handoffs, failure routing,
 * goal tracking, checkpoints, and watchdog-style guards.
 */

#ifndef HARNESS_H
#define HARNESS_H

#include "nevercode.h"

/* ── Failure Classification ── */

typedef enum {
    FAIL_TRANSIENT = 0,
    FAIL_PERSISTENT = 1,
    FAIL_RESOURCE = 2,
    FAIL_SEMANTIC = 3,
    FAIL_FATAL = 4,
    FAIL_CLASS_COUNT = 5
} failure_class_t;

typedef struct {
    failure_class_t cls;
    int consecutive;
    long last_ms;
    char *last_msg;
} failure_state_t;

typedef struct {
    long timestamps[FR_WINDOW_SIZE];
    int head;
    int count;
} failure_window_snapshot_t;

typedef struct {
    failure_window_snapshot_t classes[FAIL_CLASS_COUNT];
} failure_router_snapshot_t;

void failure_router_init(void);
void failure_router_free(void);
failure_class_t harness_classify_failure(const char *error_msg, int http_status);
const char *harness_failure_name(failure_class_t cls);
bool harness_should_retry(failure_class_t cls, int attempt);
int harness_retry_delay_ms(failure_class_t cls, int attempt);
double harness_adaptive_scale(failure_class_t cls);
int harness_failure_window_count(failure_class_t cls);
void failure_router_snapshot(failure_router_snapshot_t *out);
void failure_router_restore(const failure_router_snapshot_t *snap);

/* ── Context Handoff ── */

typedef struct {
    char *goal_summary;
    char *decisions_made;
    char *pending_tasks;
    char *files_modified;
    char *key_findings;
    char *errors_encountered;
    char *working_branch;
    int dirty_files;
    int turn_count;
    int handoff_count;
    long elapsed_s;
} handoff_state_t;

void handoff_state_init(handoff_state_t *h);
void handoff_state_free(handoff_state_t *h);
char *handoff_state_serialize(const handoff_state_t *h);
int handoff_state_deserialize(handoff_state_t *h, const char *json);
char *handoff_build_resume_prompt(const handoff_state_t *h);

/* ── Goal Evolution ── */

typedef enum {
    GOAL_PENDING = 0,
    GOAL_ACTIVE = 1,
    GOAL_COMPLETED = 2,
    GOAL_FAILED = 3,
    GOAL_EVOLVED = 4
} goal_status_t;

typedef struct {
    char *description;
    goal_status_t status;
    int priority;
    long created_at;
    long completed_at;
    char *result;
} goal_entry_t;

typedef struct {
    goal_entry_t goals[HARNESS_MAX_GOAL_DEPTH];
    int count;
    int active_idx;
} goal_tracker_t;

void goal_tracker_init(goal_tracker_t *g);
void goal_tracker_free(goal_tracker_t *g);
int goal_tracker_push(goal_tracker_t *g, const char *description, int priority);
int goal_tracker_complete(goal_tracker_t *g, int idx, const char *result);
int goal_tracker_fail(goal_tracker_t *g, int idx, const char *reason);
int goal_tracker_evolve(goal_tracker_t *g, int idx, const char **sub_goals, int n_sub);
int goal_tracker_active_idx(const goal_tracker_t *g);
int goal_tracker_count(const goal_tracker_t *g);
const char *goal_tracker_current(const goal_tracker_t *g);
int goal_tracker_pending_count(const goal_tracker_t *g);
int goal_tracker_completed_count(const goal_tracker_t *g);
char *goal_tracker_format_status(const goal_tracker_t *g);
const char *goal_status_name(goal_status_t s);

/* ── Checkpoint Persistence ── */

int harness_checkpoint_save(const char *session_id, int turn, const handoff_state_t *state);
int harness_checkpoint_load(const char *session_id, handoff_state_t *state, int *turn);
int harness_checkpoint_cleanup(const char *session_id, int keep_recent);

/* ── Daemon Mode ── */

typedef enum {
    DAEMON_IDLE = 0,
    DAEMON_RUNNING = 1,
    DAEMON_PAUSED = 2,
    DAEMON_SHUTDOWN = 3
} daemon_state_t;

typedef struct {
    daemon_state_t state;
    long start_time;
    long last_activity;
    int total_turns;
    int total_handoffs;
    int total_failures;
    double total_cost;
    bool auto_evolve;
    goal_tracker_t goals;
    failure_state_t fail;
} daemon_ctx_t;

void daemon_ctx_init(daemon_ctx_t *d);
void daemon_ctx_free(daemon_ctx_t *d);
bool daemon_is_running(const daemon_ctx_t *d);
void daemon_record_turn(daemon_ctx_t *d);
void daemon_record_failure(daemon_ctx_t *d, failure_class_t cls, const char *msg);
void daemon_record_handoff(daemon_ctx_t *d);
char *daemon_format_status(const daemon_ctx_t *d);
long daemon_uptime_s(const daemon_ctx_t *d);

/* ── Harness Session Controller ── */

typedef struct {
    daemon_ctx_t daemon;
    handoff_state_t handoff;
    int context_pct;
    bool needs_handoff;
    int handoff_retries;
} harness_ctx_t;

void harness_init(harness_ctx_t *h);
void harness_free(harness_ctx_t *h);
void harness_post_turn(harness_ctx_t *h, int context_pct, int tool_calls, bool had_error);
bool harness_needs_handoff(const harness_ctx_t *h);
char *harness_do_handoff(harness_ctx_t *h, const void *conv, const char *session_id);
harness_ctx_t *harness_global(void);

/* ── Stall Detector — watchdog for stuck agent loops ── */

typedef enum {
    STALL_OK = 0,
    STALL_LOOP_DETECTED = 1,
    STALL_NO_PROGRESS = 2,
    STALL_TIMEOUT = 3
} stall_verdict_t;

typedef struct {
    char *tool_signature;
    int output_chars;
    bool files_changed;
    long timestamp;
} stall_turn_t;

typedef struct {
    stall_turn_t window[STALL_WINDOW_SIZE];
    int window_count;
    int total_turns;
    long last_progress_time;
} stall_ctx_t;

typedef struct {
    char tool_signature[NC_MSG_MAX];
    int output_chars;
    bool files_changed;
    long age_s;
} stall_turn_snapshot_t;

typedef struct {
    unsigned long hash;
    int count;
} stall_read_snapshot_t;

typedef struct {
    stall_turn_snapshot_t window[STALL_WINDOW_SIZE];
    int window_count;
    int total_turns;
    long last_progress_age_s;
    stall_read_snapshot_t reads[STALL_READ_LOOP_WINDOW];
    int read_freq_count;
} stall_snapshot_t;

void stall_detector_init(void);
void stall_detector_free(void);
void stall_detector_record_turn(const char *tools_used, int output_chars, bool files_changed);
stall_verdict_t stall_detector_check(void);
const char *stall_verdict_name(stall_verdict_t v);
char *stall_detector_suggest(stall_verdict_t v);
long stall_detector_idle_s(void);
int stall_detector_window_count(void);
void stall_detector_record_read(const char *path);
bool stall_detector_read_loop(void);
void stall_detector_reset_reads(void);
void stall_detector_snapshot(stall_snapshot_t *out);
void stall_detector_restore(const stall_snapshot_t *snap);

/* ── Tool Call Verification ── */

typedef enum {
    TV_PASS = 0,
    TV_EMPTY_OUTPUT = 1,
    TV_SUSPICIOUS = 2,
    TV_EXPLICIT_FAIL = 3,
    TV_PATTERN_ALERT = 4
} tv_verdict_t;

typedef struct {
    char tool[NC_TAG_MAX];
    int output_len;
    bool flagged;
    long age_s;
} tool_verify_record_snapshot_t;

typedef struct {
    tool_verify_record_snapshot_t records[NC_TV_HISTORY_MAX];
    int count;
    int total_verified;
    int total_flagged;
    int consecutive_empty;
} tool_verify_snapshot_t;

void tool_verify_init(void);
void tool_verify_free(void);
tv_verdict_t tool_verify_check(const char *tool_name, const char *output, int output_len,
                               int exit_code, bool is_error);
const char *tv_verdict_name(tv_verdict_t v);
bool tool_verify_needs_intervention(tv_verdict_t v);
int tool_verify_flagged_count(void);
int tool_verify_total_count(void);
char *tool_verify_format_stats(void);
void tool_verify_snapshot(tool_verify_snapshot_t *out);
void tool_verify_restore(const tool_verify_snapshot_t *snap);

/* ── Ghost Snapshots — transparent git rollback points ── */

void ghost_snapshot_init(const char *session_id);
void ghost_snapshot_free(void);
bool ghost_snapshot_available(void);
int ghost_snapshot_create(int turn);
int ghost_snapshot_restore(int turn);
int ghost_snapshot_restore_latest(void);
int ghost_snapshot_count(void);
int ghost_snapshot_turn_at(int idx);
int ghost_snapshot_cleanup(const char *session_id);
char *ghost_snapshot_format_list(void);

/* ── Process Lifecycle — unified state machine ── */

void proc_lifecycle_init(void);
void proc_lifecycle_free(void);
int proc_lifecycle_register(pid_t pid, const char *label);
void proc_lifecycle_set_running(pid_t pid);
void proc_lifecycle_set_exited(pid_t pid, int exit_code);
void proc_lifecycle_set_killed(pid_t pid);
void proc_lifecycle_record_output(pid_t pid, long bytes);
int proc_lifecycle_reap_exited(void);
int proc_lifecycle_kill_overtime(int max_seconds);
int proc_lifecycle_active_count(void);
int proc_lifecycle_total_count(void);
char *proc_lifecycle_format(void);

/* ── Context Fatigue Predictor ── */

typedef struct {
    int turn;
    int tokens_used;
    int tokens_added;
    long age_s;
} fatigue_sample_snapshot_t;

typedef struct {
    fatigue_sample_snapshot_t samples[FATIGUE_SAMPLE_MAX];
    int count;
    int context_limit;
    double ema_velocity;
    int last_total;
    bool initialized;
} context_fatigue_snapshot_t;

void context_fatigue_init(int context_window_tokens);
void context_fatigue_free(void);
void context_fatigue_record(int turn, int total_tokens);
int context_fatigue_pct(void);
int context_fatigue_turns_remaining(void);
int context_fatigue_level(void);
bool context_fatigue_needs_compact(void);
double context_fatigue_velocity(void);
char *context_fatigue_format(void);
void context_fatigue_snapshot(context_fatigue_snapshot_t *out);
void context_fatigue_restore(const context_fatigue_snapshot_t *snap);

/* ── Progress Tracker — cross-session persistence ── */

void progress_tracker_init(void);
void progress_tracker_load(void);
int progress_tracker_save(void);
int progress_tracker_add(const char *text, bool done);
int progress_tracker_mark_done(int idx);
int progress_tracker_count(void);
int progress_tracker_pending_count(void);
int progress_tracker_done_count(void);
char *progress_tracker_format(void);
char *progress_tracker_context(void);
void progress_tracker_free(void);

/* ── Checkpoint Discipline — automatic git checkpoints ── */

typedef struct {
    int turn;
    long age_s;
    char tag[NC_TAG_MAX];
} checkpoint_entry_snapshot_t;

typedef struct {
    checkpoint_entry_snapshot_t entries[NC_CHECKPOINT_MAX_ENTRIES];
    int count;
    int interval;
    int turns_since_cp;
    int consecutive_failures;
    bool active;
    char session_id[NC_LABEL_MAX];
} checkpoint_discipline_snapshot_t;

void checkpoint_discipline_init(const char *session_id, int interval_turns);
void checkpoint_discipline_free(void);
bool checkpoint_discipline_should_fire(int turn, bool files_changed);
int checkpoint_discipline_create(int turn);
int checkpoint_discipline_restore(int turn);
int checkpoint_discipline_count(void);
int checkpoint_discipline_cleanup(void);
char *checkpoint_discipline_format(void);
void checkpoint_discipline_snapshot(checkpoint_discipline_snapshot_t *out);
void checkpoint_discipline_restore_state(const checkpoint_discipline_snapshot_t *snap);
int checkpoint_exclude_count(void);
const char *checkpoint_exclude_pattern(int idx);

/* ── Episode Memory — cross-session learning ── */

void episode_memory_init(void);
void episode_memory_free(void);
int episode_memory_record(const char *symptom, const char *diagnosis, const char *resolution,
                          int outcome, int turns_spent);
int episode_memory_tag(int idx, const char *tag);
int episode_memory_lookup(const char *symptom, int *out_indices, int max_results);
char *episode_memory_build_prompt(const char *symptom);
int episode_memory_count(void);
int episode_memory_lookup_count(void);
int episode_memory_hit_count(void);
int episode_memory_save(void);
int episode_memory_load(void);
char *episode_memory_format(void);

/* ── Hypothesis-Driven Debugging ── */

void hypothesis_debug_init(void);
void hypothesis_debug_free(void);
int hypothesis_debug_begin(const char *symptom);
int hypothesis_debug_propose(int session, const char *description, double initial_confidence);
int hypothesis_debug_auto_populate(int session, int stall_type);
int hypothesis_debug_test(int session, int hyp_idx);
int hypothesis_debug_confirm(int session, int hyp_idx, const char *evidence);
int hypothesis_debug_refute(int session, int hyp_idx, const char *evidence);
int hypothesis_debug_update_confidence(int session, int hyp_idx, double new_confidence);
int hypothesis_debug_end(int session);
int hypothesis_debug_session_count(void);
int hypothesis_debug_total_hypotheses(void);
int hypothesis_debug_hyp_count(int session);
int hypothesis_debug_confirmed_count(int session);
int hypothesis_debug_refuted_count(int session);
char *hypothesis_debug_build_prompt(int session);
char *hypothesis_debug_format(int session);
char *hypothesis_debug_format_all(void);

/* ── Feature List — JSON-based incremental progress anchor ── */

void feature_list_init(void);
void feature_list_free(void);
int feature_list_load(void);
int feature_list_save(void);
int feature_list_add(const char *description, const char *category);
int feature_list_mark_pass(int idx);
int feature_list_mark_fail(int idx);
int feature_list_record_attempt(int idx);
int feature_list_count(void);
int feature_list_passing_count(void);
int feature_list_failing_count(void);
int feature_list_next_failing(void);
bool feature_list_all_passing(void);
const char *feature_list_description(int idx);
bool feature_list_is_passing(int idx);
char *feature_list_build_prompt(void);
char *feature_list_format(void);

/* ── Session Preflight — pre-session health validation ── */

typedef enum { PREFLIGHT_OK = 0, PREFLIGHT_WARN = 1, PREFLIGHT_DEGRADED = 2 } preflight_status_t;

typedef struct {
    preflight_status_t status;
    bool git_clean;
    int git_dirty_count;
    int feature_total;
    int feature_passing;
    int progress_pending;
    int progress_done;
    int episode_count;
    bool progress_file_exists;
    bool feature_file_exists;
    char git_branch[NC_LABEL_MAX];
    char last_commit[NC_MSG_MAX];
    char git_log[NC_CMD_MAX];
    long elapsed_ms;
} preflight_report_t;

void session_preflight_init(void);
void session_preflight_free(void);
preflight_report_t session_preflight_run(void);
char *session_preflight_format(const preflight_report_t *r);
char *session_preflight_context(const preflight_report_t *r);
const char *preflight_status_name(preflight_status_t s);

/* ── Repo Map (context engineering) ── */

char *repo_map_generate(const char *root_path, int max_tokens);
int repo_map_entry_count(const char *root_path);
char *repo_map_summary(const char *root_path);

/* ── Output evaluator (goal-aware quality check) ── */

typedef struct {
    int total_evals;
    int pass_count;
    int weak_count;
    int incomplete_count;
    int off_topic_count;
    int empty_count;
    int consecutive_weak;
} output_eval_snapshot_t;

void output_eval_init(void);
void output_eval_free(void);
/* eval_result_t is internal; verdict is returned as int */
typedef struct {
    int verdict;
    double confidence;
    int word_count;
    int code_blocks;
    int tool_calls;
    bool has_apology;
    bool has_refusal;
    bool mentions_goal;
} oeval_result_t;
oeval_result_t output_eval_check_ex(const char *output, const char *goal, int tool_calls_made);
const char *output_eval_verdict_name(int verdict);
bool output_eval_needs_retry(int verdict);
bool output_eval_degrading(void);
char *output_eval_retry_prompt(int verdict, const char *goal);
int output_eval_total(void);
int output_eval_pass_count(void);
int output_eval_weak_count(void);
int output_eval_consecutive_weak(void);
char *output_eval_format_stats(void);
void output_eval_snapshot(output_eval_snapshot_t *out);
void output_eval_restore(const output_eval_snapshot_t *snap);

/* ── Auto-Test (self-verification loop) ── */

typedef struct {
    char pattern[NC_LABEL_MAX];
    char command[NC_CMD_MAX];
    bool detected;
} auto_test_pattern_snapshot_t;

typedef struct {
    auto_test_pattern_snapshot_t patterns[ATEST_MAX_PATTERNS];
    int count;
    char root[NC_PATH_MAX];
    int last_turn;
    int total_runs;
    int total_pass;
    int total_fail;
    long last_age_s;
    bool enabled;
} auto_test_snapshot_t;

void auto_test_init(const char *project_root);
void auto_test_free(void);
void auto_test_set_enabled(bool enabled);
bool auto_test_is_enabled(void);
bool auto_test_should_run(int current_turn);
char *auto_test_find_test_file(const char *edited_file);
char *auto_test_build_command(const char *edited_file);
void auto_test_record_run(int turn, bool passed);
char *auto_test_suggest_prompt(const char *edited_file);
int auto_test_run_count(void);
int auto_test_pass_count(void);
int auto_test_fail_count(void);
char *auto_test_format_stats(void);
void auto_test_snapshot(auto_test_snapshot_t *out);
void auto_test_restore(const auto_test_snapshot_t *snap);

/* ── Initializer Agent — first-run environment setup ── */

bool init_agent_needed(const char *project_root);
int init_agent_detect_type(const char *project_root);
const char *init_agent_type_name(int recipe_idx);
const char *init_agent_run_cmd(int recipe_idx);
const char *init_agent_test_cmd(int recipe_idx);
int init_agent_gen_script(const char *project_root, int recipe_idx);
int init_agent_write_marker(const char *project_root);
char *init_agent_build_context(const char *project_root, int recipe_idx);
int init_agent_run(const char *project_root, int *out_recipe);
int init_agent_recipe_count(void);

/* ── Session Watchdog — per-turn timeout guard ── */

typedef enum { WD_IDLE = 0, WD_RUNNING = 1, WD_FIRED = 2 } watchdog_state_t;

typedef enum {
    WD_VERDICT_OK = 0,
    WD_VERDICT_WARN = 1,
    WD_VERDICT_TIMEOUT = 2,
    WD_VERDICT_HANDOFF = 3
} watchdog_verdict_t;

typedef struct {
    watchdog_state_t state;
    int deadline_ms;
    long turn_start_ms;
    int current_turn;
    int total_turns;
    int successful_turns;
    int timeout_count;
    int consecutive_timeouts;
    long longest_turn_ms;
} watchdog_ctx_t;

typedef struct {
    int deadline_ms;
    int total_turns;
    int successful_turns;
    int timeout_count;
    int consecutive_timeouts;
    long longest_turn_ms;
} watchdog_snapshot_t;

void watchdog_init(int deadline_ms);
void watchdog_free(void);
void watchdog_begin_turn(int turn);
void watchdog_end_turn(bool success);
long watchdog_elapsed_ms(void);
watchdog_verdict_t watchdog_check(void);
void watchdog_set_deadline(int ms);
/* Poll from libcurl xfer callback; non-zero means abort the transfer (watchdog fired). */
int watchdog_stream_xfer_poll(void);
watchdog_verdict_t watchdog_stream_xfer_take_verdict(void);
char *watchdog_recovery_prompt(watchdog_verdict_t v);
char *watchdog_format_status(void);
int watchdog_timeout_count(void);
int watchdog_consecutive_timeouts(void);
long watchdog_longest_turn_ms(void);
void watchdog_snapshot(watchdog_snapshot_t *out);
void watchdog_restore(const watchdog_snapshot_t *snap);

/* ── Sprint Contracts — adversarial evaluation loop ── */

void sprint_contract_init(void);
void sprint_contract_free(void);
int sprint_contract_create(const char *description, int max_iterations);
int sprint_contract_add_criterion(int idx, const char *text);
int sprint_contract_grade(int contract_idx, int criterion_idx, bool passed, const char *feedback);
int sprint_contract_iterate(int idx);
bool sprint_contract_all_pass(int idx);
bool sprint_contract_finalized(int idx);
int sprint_contract_count(void);
int sprint_contract_active(void);
int sprint_contract_criteria_count(int idx);
int sprint_contract_pass_count(int idx);
int sprint_contract_fail_count(int idx);
int sprint_contract_iteration(int idx);
char *sprint_contract_build_feedback(int idx);
char *sprint_contract_build_prompt(int idx);
char *sprint_contract_format_status(void);
int sprint_contract_save(int idx, const char *dir);

/* ── Pipeline DAG — declarative dependency graph for multi-step tasks ── */

typedef enum {
    PIPE_PENDING = 0,
    PIPE_QUEUED = 1,
    PIPE_RUNNING = 2,
    PIPE_DONE = 3,
    PIPE_FAILED = 4,
    PIPE_SKIPPED = 5,
    PIPE_CANCELLED = 6
} pipe_status_t;

typedef struct {
    char id[PIPE_ID_MAX];
    char prompt[PIPE_PROMPT_MAX];
    char deps[PIPE_MAX_DEPS][PIPE_ID_MAX];
    int ndeps;
    pipe_status_t status;
    char *output;
    int exit_code;
    int retries;
    int attempt;
    long started_ms;
    long finished_ms;
} pipe_node_t;

typedef struct {
    char name[NC_LABEL_MAX];
    pipe_node_t nodes[PIPE_MAX_NODES];
    int nnodes;
    int concurrency;
    int max_iterations;
    bool fail_fast;
    long started_ms;
    long finished_ms;
    pipe_status_t status;
} pipeline_t;

void pipeline_init(pipeline_t *p, const char *name, int concurrency);
void pipeline_free(pipeline_t *p);
int pipeline_add_node(pipeline_t *p, const char *id, const char *prompt);
int pipeline_add_dep(pipeline_t *p, const char *node_id, const char *dep_id);
int pipeline_set_retries(pipeline_t *p, const char *node_id, int retries);
int pipeline_find_node(const pipeline_t *p, const char *id);
bool pipeline_validate(const pipeline_t *p, char *err, size_t errsz);
int pipeline_ready_nodes(const pipeline_t *p, int *out, int max);
int pipeline_mark_running(pipeline_t *p, int idx);
int pipeline_mark_done(pipeline_t *p, int idx, const char *output, int exit_code);
int pipeline_mark_failed(pipeline_t *p, int idx, const char *output, int exit_code);
int pipeline_mark_cancelled(pipeline_t *p, int idx);
bool pipeline_all_terminal(const pipeline_t *p);
int pipeline_skip_downstream(pipeline_t *p, int failed_idx);
const char *pipe_status_name(pipe_status_t s);
char *pipeline_format_status(const pipeline_t *p);

/* ── Fanout / Merge — MapReduce for parallel agent coordination ── */

typedef enum { FANOUT_COUNT = 0, FANOUT_VALUES = 1, FANOUT_MATRIX = 2 } fanout_mode_t;

typedef struct {
    char group_id[PIPE_ID_MAX];
    char template_prompt[PIPE_PROMPT_MAX];
    fanout_mode_t mode;
    int count;
    char *values[FANOUT_MAX_COPIES];
    int nvalues;
    char expanded_ids[FANOUT_MAX_COPIES][PIPE_ID_MAX];
    int nexpanded;
} fanout_spec_t;

typedef enum { MERGE_BATCH = 0, MERGE_GROUP = 1 } merge_mode_t;

typedef struct {
    char group_id[PIPE_ID_MAX];
    char source_group[PIPE_ID_MAX];
    merge_mode_t mode;
    int batch_size;
    char group_field[NC_LABEL_MAX];
    int nbatches;
} merge_spec_t;

void fanout_init(fanout_spec_t *f, const char *group_id, const char *tpl);
void fanout_free(fanout_spec_t *f);
int fanout_set_count(fanout_spec_t *f, int n);
int fanout_add_value(fanout_spec_t *f, const char *val);
int fanout_expand(fanout_spec_t *f, pipeline_t *p);
int fanout_expanded_count(const fanout_spec_t *f);
const char *fanout_expanded_id(const fanout_spec_t *f, int idx);
void merge_init(merge_spec_t *m, const char *group_id, const char *source);
int merge_set_batch(merge_spec_t *m, int size);
int merge_set_group(merge_spec_t *m, const char *field);
int merge_expand(merge_spec_t *m, const fanout_spec_t *src, pipeline_t *p, const char *tpl);

/* ── Declarative Success Criteria — structured output validation ── */

typedef enum {
    SC_OUTPUT_CONTAINS = 0,
    SC_FILE_EXISTS = 1,
    SC_FILE_CONTAINS = 2,
    SC_FILE_NONEMPTY = 3,
    SC_EXIT_CODE_ZERO = 4,
    SC_OUTPUT_NOT_CONTAINS = 5
} sc_kind_t;

typedef struct {
    sc_kind_t kind;
    char value[SC_VALUE_MAX];
    char path[NC_URL_MAX];
    bool case_sensitive;
} sc_rule_t;

typedef struct {
    sc_rule_t rules[SC_MAX_RULES];
    int nrules;
} sc_ruleset_t;

void sc_ruleset_init(sc_ruleset_t *rs);
int sc_add_output_contains(sc_ruleset_t *rs, const char *needle, bool case_sensitive);
int sc_add_output_not_contains(sc_ruleset_t *rs, const char *needle, bool case_sensitive);
int sc_add_file_exists(sc_ruleset_t *rs, const char *path);
int sc_add_file_contains(sc_ruleset_t *rs, const char *path, const char *needle, bool case_sens);
int sc_add_file_nonempty(sc_ruleset_t *rs, const char *path);
int sc_add_exit_zero(sc_ruleset_t *rs);
bool sc_evaluate(const sc_ruleset_t *rs, const char *output, int exit_code, const char *workdir,
                 char *detail, size_t detail_sz);
const char *sc_kind_name(sc_kind_t k);

/* ── Cooperative Cancel Scope — hierarchical multi-level cancellation ── */

typedef enum {
    CSCOPE_ACTIVE = 0,
    CSCOPE_CANCEL_REQUESTED = 1,
    CSCOPE_CANCELLED = 2
} cscope_state_t;

typedef struct cscope_s {
    char token[CSCOPE_MAX_TOKENS];
    cscope_state_t state;
    struct cscope_s *parent;
    int depth;
    long requested_ms;
} cscope_t;

void cscope_init(cscope_t *cs, const char *token, cscope_t *parent);
void cscope_request_cancel(cscope_t *cs);
bool cscope_is_cancelled(const cscope_t *cs);
bool cscope_should_stop(const cscope_t *cs);
void cscope_reset(cscope_t *cs);
const char *cscope_state_name(cscope_state_t s);
int cscope_depth(const cscope_t *cs);

/* ── Multi-Agent Shared Pad — concurrent shared findings board ── */

typedef struct {
    char *text;
    char author[SPAD_AUTHOR_MAX];
    long timestamp;
} spad_line_t;

typedef struct {
    char author[SPAD_AUTHOR_MAX];
    char text[SPAD_LINE_MAX];
    long age_s;
} shared_pad_line_snapshot_t;

typedef struct {
    shared_pad_line_snapshot_t lines[SPAD_MAX_LINES];
    int count;
    bool persistent;
    char path[NC_PATH_MAX];
    char run_token[CSCOPE_MAX_TOKENS];
} shared_pad_snapshot_t;

typedef struct {
    spad_line_t lines[SPAD_MAX_LINES];
    int nlines;
    pthread_mutex_t lock;
    char path[NC_PATH_MAX];
    char current_run_token[CSCOPE_MAX_TOKENS];
    bool persistent;
} shared_pad_t;

void shared_pad_init(shared_pad_t *sp);
void shared_pad_init_persistent(shared_pad_t *sp, const char *path);
void shared_pad_free(shared_pad_t *sp);
void shared_pad_set_run_token(shared_pad_t *sp, const char *run_token);
int shared_pad_append(shared_pad_t *sp, const char *author, const char *text);
int shared_pad_merge(shared_pad_t *sp, const char *author, const char *text);
int shared_pad_line_count(shared_pad_t *sp);
char *shared_pad_read(shared_pad_t *sp);
int shared_pad_persist(shared_pad_t *sp);
int shared_pad_load(shared_pad_t *sp);
char *shared_pad_build_suffix(const shared_pad_t *sp);
void shared_pad_snapshot(const shared_pad_t *sp, shared_pad_snapshot_t *out);
int shared_pad_merge_snapshot(shared_pad_t *sp, const shared_pad_snapshot_t *snap);
void shared_pad_restore(shared_pad_t *sp, const shared_pad_snapshot_t *snap);

/* ── Event Ledger — append-only log + snapshot dual-write ── */

typedef struct {
    char type[LEDGER_FIELD_MAX];
    char node_id[PIPE_ID_MAX];
    char detail[LEDGER_DETAIL_MAX];
    char output[LEDGER_OUTPUT_MAX];
    char run_token[CSCOPE_MAX_TOKENS];
    long timestamp;
    int sequence;
    int exit_code;
    bool has_output;
    bool has_exit_code;
    bool has_run_token;
} ledger_event_t;

typedef struct ledger_watcher_s ledger_watcher_t; /* forward decl for watchers */

typedef struct {
    ledger_event_t events[LEDGER_MAX_EVENTS];
    int nevents;
    int next_seq;
    char log_path[NC_PATH_MAX];
    char snap_path[NC_PATH_MAX];
    char current_run_token[CSCOPE_MAX_TOKENS];
    bool persistent;
    ledger_watcher_t *watchers[LEDGER_MAX_WATCHERS];
    int nwatchers;
} event_ledger_t;

void ledger_init(event_ledger_t *el);
void ledger_init_persistent(event_ledger_t *el, const char *log_path, const char *snap_path);
void ledger_free(event_ledger_t *el);
void ledger_prepare_new_run(event_ledger_t *el);
void ledger_set_run_token(event_ledger_t *el, const char *run_token);
int ledger_append(event_ledger_t *el, const char *type, const char *node_id, const char *detail);
int ledger_append_result(event_ledger_t *el, const char *type, const char *node_id,
                         const char *detail, const char *output, int exit_code);
int ledger_flush_log(event_ledger_t *el);
int ledger_write_snapshot(event_ledger_t *el, const char *json_state);
int ledger_load_log(event_ledger_t *el);
int ledger_event_count(const event_ledger_t *el);
const ledger_event_t *ledger_event_at(const event_ledger_t *el, int idx);
int ledger_events_since(const event_ledger_t *el, int seq, const ledger_event_t **out, int max);
char *ledger_format_tail(const event_ledger_t *el, int n);

/* ── Execution Preflight — dry-run / plan preview ── */

typedef struct {
    char node_id[PIPE_ID_MAX];
    char agent[NC_LABEL_MAX];
    char prompt_preview[PFLT_PREVIEW_MAX];
    char deps_str[NC_MSG_MAX];
    int dep_count;
    bool needs_write;
    bool needs_network;
} pflt_step_t;

typedef struct {
    char pipeline_name[NC_LABEL_MAX];
    pflt_step_t steps[PFLT_MAX_STEPS];
    int nsteps;
    char warnings[PFLT_MAX_WARNINGS][NC_MSG_MAX];
    int nwarnings;
    int total_agents;
    bool has_cycles;
    long build_ms;
} pflt_report_t;

void pflt_report_init(pflt_report_t *r);
int pflt_build_from_pipeline(pflt_report_t *r, const pipeline_t *p);
int pflt_add_warning(pflt_report_t *r, const char *msg);
char *pflt_format_report(const pflt_report_t *r);
char *pflt_format_summary(const pflt_report_t *r);
bool pflt_has_issues(const pflt_report_t *r);

/* ── Periodic Probe — tick-based health monitor for parallel agents ── */

typedef enum { PROBE_IDLE = 0, PROBE_WATCHING = 1, PROBE_DONE = 2 } probe_state_t;

typedef enum {
    PROBE_ACT_NONE = 0,
    PROBE_ACT_CANCEL = 1,
    PROBE_ACT_RESTART = 2
} probe_action_kind_t;

typedef struct {
    probe_action_kind_t kind;
    char target_id[PIPE_ID_MAX];
    char reason[NC_MSG_MAX];
} probe_action_t;

typedef struct {
    char watch_group[PIPE_ID_MAX];
    int interval_s;
    int tick_count;
    long last_tick_ms;
    long next_tick_ms;
    probe_state_t state;
    probe_action_t actions[PROBE_MAX_ACTIONS];
    int nactions;
} periodic_probe_t;

typedef struct {
    char watch_group[PIPE_ID_MAX];
    int interval_s;
    int tick_count;
    long last_tick_age_ms;
    long next_tick_delay_ms;
    probe_state_t state;
    probe_action_t actions[PROBE_MAX_ACTIONS];
    int nactions;
} probe_snapshot_t;

void probe_init(periodic_probe_t *pp, const char *group, int interval_s);
void probe_free(periodic_probe_t *pp);
bool probe_should_tick(const periodic_probe_t *pp, long now_ms);
int probe_record_tick(periodic_probe_t *pp);
int probe_add_action(periodic_probe_t *pp, probe_action_kind_t kind, const char *target,
                     const char *reason);
int probe_apply_actions(periodic_probe_t *pp, pipeline_t *p);
bool probe_group_settled(const periodic_probe_t *pp, const pipeline_t *p);
void probe_finalize(periodic_probe_t *pp);
char *probe_format_status(const periodic_probe_t *pp);
void probe_snapshot(const periodic_probe_t *pp, probe_snapshot_t *out);
void probe_restore(periodic_probe_t *pp, const probe_snapshot_t *snap);

/* ── Retry Cycle — controlled failure back-edge iteration ── */

typedef struct {
    char tail_id[PIPE_ID_MAX];
    char targets[RCYCLE_MAX_CHAIN][PIPE_ID_MAX];
    int ntargets;
} rcycle_edge_t;

typedef struct {
    rcycle_edge_t edges[RCYCLE_MAX_EDGES];
    int nedges;
    int iteration_counts[RCYCLE_MAX_EDGES];
    int max_iterations;
} retry_cycle_t;

void rcycle_init(retry_cycle_t *rc, int max_iters);
void rcycle_free(retry_cycle_t *rc);
int rcycle_add_edge(retry_cycle_t *rc, const char *tail_id, const char **targets, int ntargets);
bool rcycle_should_restart(const retry_cycle_t *rc, const char *failed_node);
int rcycle_do_restart(retry_cycle_t *rc, const char *failed_node, pipeline_t *p);
int rcycle_iteration(const retry_cycle_t *rc, const char *tail_id);
bool rcycle_exhausted(const retry_cycle_t *rc, const char *tail_id);
char *rcycle_format_status(const retry_cycle_t *rc);

/* ── Pipeline Orchestrator — unified run-loop tying all modules ── */

#define PIPE_ORCH_MAX_PROBES 4
#define PIPE_ORCH_MAX_CRITERIA 16
#define PIPE_ORCH_MAX_CANCEL_FLAGS 32
#define PIPE_ORCH_MAX_RERUN_QUEUE 32

typedef struct artifact_store_s artifact_store_t; /* forward decl */

typedef enum {
    ORCH_IDLE = 0,
    ORCH_RUNNING = 1,
    ORCH_DONE = 2,
    ORCH_FAILED = 3,
    ORCH_CANCELLED = 4
} orch_state_t;

typedef struct {
    char node_id[PIPE_ID_MAX];
    sc_ruleset_t rules;
} orch_criteria_entry_t;

typedef struct {
    pipeline_t *pipeline;
    cscope_t run_scope;
    event_ledger_t ledger;
    retry_cycle_t cycles;
    shared_pad_t pad;
    periodic_probe_t probes[PIPE_ORCH_MAX_PROBES];
    int nprobes;
    orch_criteria_entry_t criteria_map[PIPE_ORCH_MAX_CRITERIA];
    int ncriteria;
    orch_state_t state;
    long started_ms;
    long finished_ms;
    artifact_store_t *artifacts;
    bool ctx_rendering;
    char cancel_flags[PIPE_ORCH_MAX_CANCEL_FLAGS][PIPE_ID_MAX];
    int ncancel_flags;
    char rerun_queue[PIPE_ORCH_MAX_RERUN_QUEUE][PIPE_ID_MAX];
    int nrerun_queued;
} pipe_orch_t;

void pipe_orch_init(pipe_orch_t *o, pipeline_t *p);
void pipe_orch_init_persistent(pipe_orch_t *o, pipeline_t *p, const char *log_path,
                               const char *snap_path, const char *pad_path);
void pipe_orch_free(pipe_orch_t *o);
int pipe_orch_add_criteria(pipe_orch_t *o, const char *node_id, const sc_ruleset_t *rs);
int pipe_orch_add_probe(pipe_orch_t *o, const char *group, int interval_s);
int pipe_orch_add_cycle(pipe_orch_t *o, const char *tail, const char **targets, int ntargets);
int pipe_orch_begin(pipe_orch_t *o);
int pipe_orch_step(pipe_orch_t *o, int *ready, int max_ready);
int pipe_orch_complete_node(pipe_orch_t *o, const char *node_id, const char *output, int exit_code);
void pipe_orch_cancel(pipe_orch_t *o);
void pipe_orch_cancel_node(pipe_orch_t *o, const char *node_id);
int pipe_orch_pad_write(pipe_orch_t *o, const char *author, const char *text);
char *pipe_orch_pad_read(pipe_orch_t *o);
char *pipe_orch_collect_outputs(const pipe_orch_t *o);
char *pipe_orch_format_status(const pipe_orch_t *o);
bool pipe_orch_is_running(const pipe_orch_t *o);
bool pipe_orch_is_terminal(const pipe_orch_t *o);
const event_ledger_t *pipe_orch_ledger(const pipe_orch_t *o);
void pipe_orch_attach_artifacts(pipe_orch_t *o, artifact_store_t *as);
void pipe_orch_enable_ctx_rendering(pipe_orch_t *o);
char *pipe_orch_render_prompt(const pipe_orch_t *o, const char *node_id);
int pipe_orch_flag_node_cancel(pipe_orch_t *o, const char *node_id);
bool pipe_orch_node_cancel_pending(const pipe_orch_t *o, const char *node_id);
void pipe_orch_clear_node_cancel(pipe_orch_t *o, const char *node_id);
int pipe_orch_queue_rerun(pipe_orch_t *o, const char *node_id);
int pipe_orch_drain_reruns(pipe_orch_t *o);
int pipe_orch_rerun_depth(const pipe_orch_t *o);

/* ── Scope Aggregate — structured fanout result collection ── */

typedef struct {
    int total;
    int completed;
    int failed;
    int skipped;
    int cancelled;
    int pending;
    int with_output;
    int without_output;
} scope_summary_t;

typedef struct {
    char node_id[PIPE_ID_MAX];
    pipe_status_t status;
    char *output;
    int exit_code;
    int attempt;
} scope_member_t;

typedef struct {
    char group_id[PIPE_ID_MAX];
    scope_member_t members[FANOUT_MAX_COPIES];
    int nmembers;
    scope_summary_t summary;
} scope_aggregate_t;

void scope_agg_init(scope_aggregate_t *sa, const char *group_id);
void scope_agg_free(scope_aggregate_t *sa);
int scope_agg_collect(scope_aggregate_t *sa, const pipeline_t *p, const fanout_spec_t *f);
int scope_agg_collect_prefix(scope_aggregate_t *sa, const pipeline_t *p, const char *prefix);
scope_summary_t scope_agg_summarize(const scope_aggregate_t *sa);
char *scope_agg_joined_outputs(const scope_aggregate_t *sa);
char *scope_agg_filtered_outputs(const scope_aggregate_t *sa, pipe_status_t filter);
int scope_agg_count_with_output(const scope_aggregate_t *sa);
int scope_agg_count_by_status(const scope_aggregate_t *sa, pipe_status_t s);
char *scope_agg_format(const scope_aggregate_t *sa);
char *scope_agg_build_ctx(const scope_aggregate_t *sa);
int scope_agg_subset(scope_aggregate_t *dst, const scope_aggregate_t *src, pipe_status_t filter);
int scope_agg_partition_output(const scope_aggregate_t *sa, scope_aggregate_t *with_out,
                               scope_aggregate_t *without_out);

/* ── Prompt Context Rendering — template interpolation with upstream results ── */

typedef struct {
    char key[PROMPT_CTX_KEY_MAX];
    char *value;
} prompt_ctx_var_t;

typedef struct {
    prompt_ctx_var_t vars[PROMPT_CTX_MAX_VARS];
    int nvars;
} prompt_ctx_t;

void prompt_ctx_init(prompt_ctx_t *pc);
void prompt_ctx_free(prompt_ctx_t *pc);
int prompt_ctx_set(prompt_ctx_t *pc, const char *key, const char *value);
const char *prompt_ctx_get(const prompt_ctx_t *pc, const char *key);
int prompt_ctx_populate(prompt_ctx_t *pc, const pipeline_t *p);
int prompt_ctx_populate_scope(prompt_ctx_t *pc, const char *prefix, const scope_aggregate_t *sa);
char *prompt_ctx_render(const prompt_ctx_t *pc, const char *tpl);
int prompt_ctx_populate_filtered(prompt_ctx_t *pc, const char *prefix, const scope_aggregate_t *sa,
                                 pipe_status_t filter);

/* ── Node Artifact Storage — per-node structured output collection ── */

typedef struct {
    char name[NODE_ART_NAME_MAX];
    char path[NODE_ART_PATH_MAX];
    long size;
} node_art_slot_t;

typedef struct {
    char node_id[PIPE_ID_MAX];
    char base_dir[NODE_ART_PATH_MAX];
    node_art_slot_t slots[NODE_ART_MAX_SLOTS];
    int nslots;
} node_artifact_t;

struct artifact_store_s {
    node_artifact_t nodes[PIPE_MAX_NODES];
    int nnodes;
    char root_dir[NODE_ART_PATH_MAX];
};

void artifact_store_init(artifact_store_t *as, const char *root_dir);
void artifact_store_free(artifact_store_t *as);
int artifact_store_rebuild(artifact_store_t *as);
int artifact_store_ensure_node(artifact_store_t *as, const char *node_id);
int artifact_append(artifact_store_t *as, const char *node_id, const char *name,
                    const char *content);
int artifact_write(artifact_store_t *as, const char *node_id, const char *name,
                   const char *content);
char *artifact_read(const artifact_store_t *as, const char *node_id, const char *name);
const char *artifact_path(const artifact_store_t *as, const char *node_id, const char *name);
int artifact_slot_count(const artifact_store_t *as, const char *node_id);
char *artifact_format_manifest(const artifact_store_t *as, const char *node_id);
int artifact_write_attempt(artifact_store_t *as, const char *node_id, const char *name, int attempt,
                           const char *content);
int artifact_append_attempt(artifact_store_t *as, const char *node_id, const char *name,
                            int attempt, const char *content);

/* ── Ledger Watcher — real-time event notification ── */

struct ledger_watcher_s {
    ledger_event_t queue[LEDGER_WATCHER_QUEUE];
    int head;
    int tail;
    int count;
    int watcher_id;
    bool active;
};

void ledger_watcher_init(ledger_watcher_t *w, int id);
int ledger_subscribe(event_ledger_t *el, ledger_watcher_t *w);
void ledger_unsubscribe(event_ledger_t *el, ledger_watcher_t *w);
int ledger_watcher_poll(ledger_watcher_t *w, ledger_event_t *out, int max);
bool ledger_watcher_has_events(const ledger_watcher_t *w);
int ledger_watcher_pending(const ledger_watcher_t *w);
int ledger_notify_watchers(event_ledger_t *el, const ledger_event_t *ev);

/* ── Run Persistence — full orchestrator state checkpoint/restore ── */

int run_persist_save(const pipe_orch_t *o, const char *path);
int run_persist_load(pipe_orch_t *o, pipeline_t *p, const char *path);
char *run_persist_snapshot_json(const pipe_orch_t *o);
int run_persist_restore_json(pipe_orch_t *o, pipeline_t *p, const char *json);

/* ── Remote Compaction ── */

struct config_t; /* forward decl */
char *remote_compact_summarize(const message_t *msgs, int n, const char *base_url,
                               const char *api_key, const char *model, provider_type_t provider);
bool remote_compact_available(const void *cfg);

#endif /* HARNESS_H */
