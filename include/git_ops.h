#ifndef GIT_OPS_H
#define GIT_OPS_H

#include <stdbool.h>

bool git_is_repo(const char *path);
int git_repo_root(const char *path, char *out, int cap);
int git_current_branch(const char *path, char *out, int cap);
int git_head_sha(const char *path, char *out, int cap);
int git_log_oneline(const char *path, int count, char *out, int cap);
int git_dirty_count(const char *path);
int git_tracked_file_count(const char *path);
bool git_has_uncommitted(const char *path);
int git_add_all(const char *path);
int git_write_tree(const char *path, char *out, int cap);
int git_commit(const char *path, const char *message);
int git_checkpoint_commit(const char *path, const char *message);
int git_commit_tree(const char *path, const char *tree, const char *message, char *out, int cap);
int git_ref_set(const char *path, const char *ref, const char *value);
int git_ref_delete(const char *path, const char *ref);
int git_ref_list(const char *path, const char *prefix, char *out, int cap);
int git_read_tree(const char *path, const char *ref);
int git_checkout_index_all(const char *path);
int git_stash_push(const char *path, const char *message, bool include_untracked, char *out,
                   int cap);
int git_stash_list(const char *path, char *out, int cap);
int git_stash_list_refs(const char *path, char *out, int cap);
int git_stash_list_hashes(const char *path, char *out, int cap);
int git_stash_apply(const char *path, const char *stash_ref);
int git_stash_restore_exact(const char *path, const char *stash_ref);
int git_tag_create(const char *path, const char *tag);
int git_tag_delete(const char *path, const char *tag);
int git_restore_all_from_ref(const char *path, const char *ref);
int git_push(const char *path);
int git_remote_url(const char *path, char *out, int cap);
int git_diff_stat(const char *path, char *out, int cap);
int git_diff_output(const char *path, char *out, int cap);

int git_status_short(const char *path, char *out, int cap);
int git_diff_ex(const char *path, bool staged, const char *target, char *out, int cap);
int git_diff_stat_ex(const char *path, bool staged, const char *target, char *out, int cap);
int git_ls_untracked(const char *path, char *out, int cap);
int git_log_format(const char *path, int count, const char *fmt, char *out, int cap);
int git_worktree_add(const char *path, const char *dir, const char *branch, char *out, int cap);
int git_worktree_remove(const char *path, const char *dir, char *out, int cap);

/* Dynamic-allocation capture: returns malloc'd string, NULL on failure. */
char *git_cmd_alloc(const char *path, const char *const *args, int argc, int max_bytes);

#endif /* GIT_OPS_H */
