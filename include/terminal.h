#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define T_RESET "\033[0m"
#define T_CLEAR_LINE "\r\033[K"
#define T_BOLD "\033[1m"
#define T_DIM "\033[2m"
#define T_ITALIC "\033[3m"
#define T_ULINE "\033[4m"
#define T_REVERSE "\033[7m"
#define T_STRIKE "\033[9m"
#define T_RED "\033[31m"
#define T_GREEN "\033[32m"
#define T_YELLOW "\033[33m"
#define T_BLUE "\033[34m"
#define T_MAGENTA "\033[35m"
#define T_CYAN "\033[36m"
#define T_GRAY "\033[90m"
#define T_BRIGHT_RED "\033[91m"
#define T_BRIGHT_GREEN "\033[92m"
#define T_BRIGHT_YELLOW "\033[93m"
#define T_BRIGHT_MAGENTA "\033[95m"

void term_init(void);
void term_set_no_markdown(bool v);
void term_cleanup(void);
void term_color(const char *color, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void term_stream_start(void);
void term_stream_clear_waiting(void);
void term_stream_text(const char *text);
void term_stream_end(void);
double term_stream_elapsed(void);
void term_separator(void);
char *term_readline(const char *prompt);
bool term_is_tty(void);
int term_width(void);
bool td_has_unicode(void);
bool td_has_truecolor(void);
bool td_is_tmux(void);
bool td_is_ssh(void);
bool td_is_dumb(void);
int td_cols(void);
int td_rows(void);

/* Tab completion: list all slash commands (NULL-terminated) */
const char **term_get_slash_cmds(void);

/* Readline history persistence */
void term_history_load(void);
void term_history_save(void);

/* Terminal title (OSC 0/2) — set/clear xterm window title */
void term_title_set(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void term_title_clear(void);

/* Desktop notifications (BEL + OSC 9) */
void term_notify_bel(void);
void term_notify_osc9(const char *message);
void term_notify_task_done(const char *task_desc, double elapsed_s);

/* Frame rate limiter for stream output */
typedef struct {
    struct timespec last_draw;
    int suppressed;
} frame_limiter_t;

void frame_limiter_init(frame_limiter_t *fl);
bool frame_limiter_should_draw(frame_limiter_t *fl);
void frame_limiter_mark(frame_limiter_t *fl);

/* Adaptive stream chunking policy */
typedef enum { CHUNK_SMOOTH, CHUNK_CATCHUP } chunk_mode_t;

typedef struct {
    chunk_mode_t mode;
    int queued_lines;
    int64_t oldest_age_ms;
    struct timespec last_exit;
    struct timespec last_enter;
} chunk_policy_t;

void chunk_policy_init(chunk_policy_t *cp);
int chunk_policy_drain_count(chunk_policy_t *cp, int queued, int64_t age_ms);

/* Loading animation (spinner) */
typedef struct {
    int frame;
    struct timespec last_tick;
    bool active;
} spinner_t;

void spinner_init(spinner_t *sp);
void spinner_start(spinner_t *sp);
void spinner_stop(spinner_t *sp);
const char *spinner_tick(spinner_t *sp);
void spinner_render(spinner_t *sp, const char *label, double elapsed_s);
const char *spinner_verb(void);
const char *spinner_completion_verb(void);

#endif /* TERMINAL_H */
