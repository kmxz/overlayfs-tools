#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <attr/xattr.h>
#include <attr/attributes.h>
#include <fts.h>
#include "logic.h"
#include "sh.h"

// exactly the same as in linux/fs.h
#define WHITEOUT_DEV 0

// exact the same as in fs/overlayfs/overlayfs.h
const char *ovl_opaque_xattr = "trusted.overlay.opaque";
const char *ovl_redirect_xattr = "trusted.overlay.redirect";

#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))

#define TRAILING_SLASH(status) ((file_type(status) == S_IFDIR) ? "/" : "")

static inline mode_t file_type(const struct stat *status) {
    return status->st_mode & S_IFMT;
}

static inline bool is_whiteout(const struct stat *status) {
    return (file_type(status) == S_IFCHR) && (status->st_rdev == WHITEOUT_DEV);
}

static inline mode_t permission_bits(const struct stat *status) { // not used yet. I haven't decided how to treat permission bit changes
    return status->st_mode & (S_IRWXU | S_IRWXG | S_IRWXO | S_ISVTX);
}

int is_opaque(const char *path, bool *output) {
    char val;
    ssize_t res = getxattr(path, ovl_opaque_xattr, &val, 1);
    if ((res < 0) && (errno != ENODATA)) {
        return -1;
    }
    *output = (res == 1 && val == 'y');
    return 0;
}

int is_redirect(const char *path, bool *output) {
    ssize_t res = getxattr(path, ovl_redirect_xattr, NULL, 0);
    if ((res < 0) && (errno != ENODATA)) {
        fprintf(stderr, "File %s redirect xattr can not be read.\n", path);
        return -1;
    }
    *output = (res > 0);
    return 0;
}

// Treat redirect as opaque dir because it hides the tree in lower_path
// and we do not support following to redirected lower path
int is_opaquedir(const char *path, bool *output) {
    bool opaque, redirect;
    if (is_opaque(path, &opaque) < 0) { return -1; }
    if (is_redirect(path, &redirect) < 0) { return -1; }
    *output = opaque || redirect;
    return 0;
}

bool permission_identical(const struct stat *lower_status, const struct stat *upper_status) {
    return (permission_bits(lower_status) == permission_bits(upper_status)) && (lower_status->st_uid == upper_status->st_uid) && (lower_status->st_gid == upper_status->st_gid);
}

int read_chunk(int fd, char *buf, int len) {
    ssize_t ret;
    ssize_t remain = len;
    while (remain > 0 && (ret = read(fd, buf, remain)) != 0) {
        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        remain -= ret;
        buf += ret;
    }
    return len - remain;
}

int regular_file_identical(const char *lower_path, const struct stat *lower_status, const char *upper_path, const struct stat *upper_status, bool *output) {
    size_t blksize = (size_t) MIN(lower_status->st_blksize, upper_status->st_blksize);
    if (lower_status->st_size != upper_status->st_size) { // different sizes
        *output = false;
        return 0;
    }
    char lower_buffer[blksize];
    char upper_buffer[blksize];
    int lower_file = open(lower_path, O_RDONLY);
    int upper_file = open(upper_path, O_RDONLY);
    if (lower_file < 0) {
        fprintf(stderr, "File %s can not be read for content.\n", lower_path);
        return -1;
    }
    if (upper_file < 0) {
        fprintf(stderr, "File %s can not be read for content.\n", upper_path);
        return -1;
    }
    ssize_t read_lower; ssize_t read_upper;
    do { // we can assume one will not reach EOF earlier than the other, as the file sizes are checked to be the same earlier
        read_lower = read_chunk(lower_file, lower_buffer, blksize);
        read_upper = read_chunk(upper_file, upper_buffer, blksize);
        if (read_lower < 0) {
            fprintf(stderr, "Error occured when reading file %s.\n", lower_path);
            return -1;
        }
        if (read_upper < 0) {
            fprintf(stderr, "Error occured when reading file %s.\n", upper_path);
            return -1;
        }
        if (read_upper != read_lower) { // this should not happen as we've checked the sizes
            fprintf(stderr, "Unexpected size difference: %s.\n", upper_path);
            return -1;
        }
        if (memcmp(lower_buffer, upper_buffer, read_upper)) { *output = false; return 0; } // the output is by default false, but we still set it for ease of reading
    } while (read_lower || read_upper);
    *output = true; // now we can say they are identical
    if (close(lower_file) || close(upper_file)) { return -1; }
    return 0;
}

int symbolic_link_identical(const char *lower_path, const char *upper_path, bool *output) {
    char lower_buffer[PATH_MAX];
    char upper_buffer[PATH_MAX];
    ssize_t lower_len = readlink(lower_path, lower_buffer, PATH_MAX);
    ssize_t upper_len = readlink(upper_path, upper_buffer, PATH_MAX);
    if (lower_len < 0 || lower_len == PATH_MAX) {
        fprintf(stderr, "Symbolic link %s cannot be resolved.\n", lower_path);
        return -1;
    }
    if (upper_len < 0 || upper_len == PATH_MAX) {
        fprintf(stderr, "Symbolic link %s cannot be resolved.\n", upper_path);
        return -1;
    }
    lower_buffer[lower_len] = '\0';
    upper_buffer[upper_len] = '\0';
    *output = (strcmp(lower_buffer, upper_buffer) == 0);
    return 0;
}

int vacuum_d(const char *lower_path, const char* upper_path, const size_t lower_root_len, const struct stat *lower_status, const struct stat *upper_status, bool verbose, FILE* script_stream, int *fts_instr) {
    bool opaque;
    if (is_opaquedir(upper_path, &opaque) < 0) { return -1; }
    if (opaque) { // TODO: sometimes removing opaque directory (and combine with lower directory) might be better
        *fts_instr = FTS_SKIP;
    }
    return 0;
}

int vacuum_dp(const char *lower_path, const char* upper_path, const size_t lower_root_len, const struct stat *lower_status, const struct stat *upper_status, bool verbose, FILE* script_stream, int *fts_instr) {
    if (lower_status == NULL) { return 0; } // lower does not exist
    if (file_type(lower_status) != S_IFDIR) { return 0; }
    if (!permission_identical(lower_status, upper_status)) { return 0; }
    bool opaque;
    if (is_opaquedir(upper_path, &opaque) < 0) {
        return -1;
    }
    if (opaque) { return 0; }
    // this directory might be empty if all children are deleted in previous commands. but we simply don't test whether it's that case
    return command(script_stream, "rmdir --ignore-fail-on-non-empty %", upper_path);
}

int vacuum_f(const char *lower_path, const char* upper_path, const size_t lower_root_len, const struct stat *lower_status, const struct stat *upper_status, bool verbose, FILE* script_stream, int *fts_instr) {
    if (lower_status == NULL) { return 0; } // lower does not exist
    if (file_type(lower_status) != S_IFREG) { return 0; }
    if (!permission_identical(lower_status, upper_status)) { return 0; }
    bool identical;
    if (regular_file_identical(lower_path, lower_status, upper_path, upper_status, &identical) < 0) {
        return -1;
    }
    if (!identical) { return 0; }
    return command(script_stream, "rm %", upper_path);
}

int vacuum_sl(const char *lower_path, const char* upper_path, const size_t lower_root_len, const struct stat *lower_status, const struct stat *upper_status, bool verbose, FILE* script_stream, int *fts_instr) {
    if (lower_status == NULL) { return 0; } // lower does not exist
    if (file_type(lower_status) != S_IFLNK) { return 0; }
    if (!permission_identical(lower_status, upper_status)) { return 0; }
    bool identical;
    if (symbolic_link_identical(lower_path, upper_path, &identical) < 0) {
        return -1;
    }
    if (!identical) { return 0; }
    return command(script_stream, "rm %", upper_path);
}

int list_deleted_files(const char *path, size_t root_path_len, bool verbose) { // This WORKS with files and itself is listed. However, prefixs are WRONG!
    if (!verbose) {
        printf("Removed: %s/\n", &path[root_path_len]);
        return 0;
    }
    FTSENT *cur;
    char *paths[2] = {(char *) path, NULL };
    FTS *ftsp = fts_open(paths, FTS_NOCHDIR | FTS_PHYSICAL, NULL);
    if (ftsp == NULL) { return -1; }
    int return_val = 0;
    while (((cur = fts_read(ftsp)) != NULL) && (return_val == 0)) {
        switch (cur->fts_info) {
            case FTS_D:
                break; // do nothing
            case FTS_DP:
                printf("Removed: %s/\n", &cur->fts_path[root_path_len]);
                break;
            case FTS_F:
            case FTS_SL:
                printf("Removed: %s\n", &cur->fts_path[root_path_len]);
                break;
            case FTS_DEFAULT:
                fprintf(stderr, "File %s is a special file (device or pipe). We cannot handle that.\n", cur->fts_path);
                return_val = -1;
                break;
            default:
                fprintf(stderr, "Error occured when opening %s.\n", cur->fts_path);
                return_val = -1;
        }
    }
    if (errno) { return_val = -1; } // if no error happened, fts_read will "sets the external variable errno to 0" according to the documentation
    return fts_close(ftsp) || return_val;
}

int diff_d(const char *lower_path, const char* upper_path, const size_t lower_root_len, const struct stat *lower_status, const struct stat *upper_status, bool verbose, FILE* script_stream, int *fts_instr) {
    if (lower_status != NULL) {
        if (file_type(lower_status) == S_IFDIR) {
            bool opaque = false;
            if (is_opaquedir(upper_path, &opaque) < 0) { return -1; }
            if (opaque) {
                if (list_deleted_files(lower_path, lower_root_len, verbose) < 0) { return -1; }
            } else {
                if (!permission_identical(lower_status, upper_status)) {
                    printf("Modified: %s/\n", &lower_path[lower_root_len]);
                }
                return 0; // children must be recursed, and directory itself does not need to be printed
            }
        } else { // other types of files
            printf("Removed: %s\n", &lower_path[lower_root_len]);
        }
    }
    if (!verbose) {
        *fts_instr = FTS_SKIP;
    }
    printf("Added: %s/\n", &lower_path[lower_root_len]);
    return 0;
}

int diff_f(const char *lower_path, const char* upper_path, const size_t lower_root_len, const struct stat *lower_status, const struct stat *upper_status, bool verbose, FILE* script_stream, int *fts_instr) {
    bool identical;
    if (lower_status != NULL) {
        switch (file_type(lower_status)) {
            case S_IFDIR:
                if (list_deleted_files(lower_path, lower_root_len, verbose) < 0) { return -1; }
                break;
            case S_IFREG:
                if (regular_file_identical(lower_path, lower_status, upper_path, upper_status, &identical) < 0) {
                    return -1;
                }
                if (!(identical && permission_identical(lower_status, upper_status))) {
                    printf("Modified: %s\n", &lower_path[lower_root_len]);
                }
                return 0;
            case S_IFLNK:
                printf("Removed: %s\n", &lower_path[lower_root_len]);
                break;
            default:
                fprintf(stderr, "File %s is a special file (device or pipe). We cannot handle that.\n", lower_path);
                return -1;
        }
    }
    printf("Added: %s\n", &lower_path[lower_root_len]);
    return 0;
}

int diff_sl(const char *lower_path, const char* upper_path, const size_t lower_root_len, const struct stat *lower_status, const struct stat *upper_status, bool verbose, FILE* script_stream, int *fts_instr) {
    bool identical;
    if (lower_status != NULL) {
        switch (file_type(lower_status)) {
            case S_IFDIR:
                if (list_deleted_files(lower_path, lower_root_len, verbose) < 0) { return -1; }
                break;
            case S_IFREG:
                printf("Removed: %s\n", &lower_path[lower_root_len]);
                break;
            case S_IFLNK:
                if (symbolic_link_identical(lower_path, upper_path, &identical) < 0) {
                    return -1;
                }
                if (!(identical && permission_identical(lower_status, upper_status))) {
                    printf("Modified: %s\n", &lower_path[lower_root_len]);
                }
                return 0;
            default:
                fprintf(stderr, "File %s is a special file (device or pipe). We cannot handle that.\n", lower_path);
                return -1;
        }
    }
    printf("Added: %s\n", &lower_path[lower_root_len]);
    return 0;
}

int diff_whiteout(const char *lower_path, const char* upper_path, const size_t lower_root_len, const struct stat *lower_status, const struct stat *upper_status, bool verbose, FILE* script_stream, int *fts_instr) {
    if (lower_status != NULL) {
        if (file_type(lower_status) == S_IFDIR) {
            if (list_deleted_files(lower_path, lower_root_len, verbose) < 0) { return -1; }
        } else {
            printf("Removed: %s\n", &lower_path[lower_root_len]);
        }
    } // else: whiteouting a nonexistent file? must be an error. but we ignore that :)
    return 0;
}

int merge_d(const char *lower_path, const char* upper_path, const size_t lower_root_len, const struct stat *lower_status, const struct stat *upper_status, bool verbose, FILE* script_stream, int *fts_instr) {
    bool redirect;
    if (is_redirect(upper_path, &redirect) < 0) { return -1; }
    // merging redirects is not supported, we must abort merge so redirected lower (under whiteout) won't be deleted
    // upper_path may be hiding the directory in lower_path, but there may be another redirect upper pointing at it
    if (redirect) {
        fprintf(stderr, "Found redirect on %s. Merging redirect is not supported - Abort.\n", upper_path);
        return -1;
    }
    if (lower_status != NULL) {
        if (file_type(lower_status) == S_IFDIR) {
            bool opaque = false;
            if (is_opaquedir(upper_path, &opaque) < 0) { return -1; }
            if (opaque) {
                if (command(script_stream, "rm -r %", lower_path) < 0) { return -1; };
            } else {
                if (!permission_identical(lower_status, upper_status)) {
                    command(script_stream, "chmod --reference % %", upper_path, lower_path);
                }
                return 0; // children must be recursed, and directory itself does not need to be printed
            }
        } else {
            command(script_stream, "rm %", lower_path);
        }
    }
    *fts_instr = FTS_SKIP;
    return command(script_stream, "mv -T % %", upper_path, lower_path);
}

int merge_dp(const char *lower_path, const char* upper_path, const size_t lower_root_len, const struct stat *lower_status, const struct stat *upper_status, bool verbose, FILE* script_stream, int *fts_instr) {
    if (lower_status != NULL) {
        if (file_type(lower_status) == S_IFDIR) {
            bool opaque = false;
            if (is_opaquedir(upper_path, &opaque) < 0) { return -1; }
            if (!opaque) { // delete the directory: it should be empty already
                return command(script_stream, "rmdir %", upper_path);
            }
        }
    }
    return 0;
}

int merge_f_sl(const char *lower_path, const char* upper_path, const size_t lower_root_len, const struct stat *lower_status, const struct stat *upper_status, bool verbose, FILE* script_stream, int *fts_instr) {
    return command(script_stream, "rm -rf %", lower_path) || command(script_stream, "mv -T % %", upper_path, lower_path);
}

int merge_whiteout(const char *lower_path, const char* upper_path, const size_t lower_root_len, const struct stat *lower_status, const struct stat *upper_status, bool verbose, FILE* script_stream, int *fts_instr) {
    return command(script_stream, "rm -r %", lower_path) || command(script_stream, "rm %", upper_path);
}

typedef int (*TRAVERSE_CALLBACK)(const char *lower_path, const char* upper_path, const size_t lower_root_len, const struct stat *lower_status, const struct stat *upper_status, bool verbose, FILE* script_stream, int *fts_instr);

int traverse(const char *lower_root, const char *upper_root, bool verbose, FILE* script_stream, TRAVERSE_CALLBACK callback_d, TRAVERSE_CALLBACK callback_dp, TRAVERSE_CALLBACK callback_f, TRAVERSE_CALLBACK callback_sl, TRAVERSE_CALLBACK callback_whiteout) { // returns 0 on success
    FTSENT *cur;
    char *paths[2] = {(char *) upper_root, NULL };
    char lower_path[PATH_MAX];
    strcpy(lower_path, lower_root);
    size_t upper_root_len = strlen(upper_root);
    size_t lower_root_len = strlen(lower_root);
    FTS *ftsp = fts_open(paths, FTS_NOCHDIR | FTS_PHYSICAL, NULL);
    if (ftsp == NULL) { return -1; }
    int return_val = 0;
    while ((return_val == 0) && ((cur = fts_read(ftsp)) != NULL)) {
        TRAVERSE_CALLBACK callback = NULL;
        switch (cur->fts_info) {
            case FTS_D:
                callback = callback_d;
                break;
            case FTS_DP:
                callback = callback_dp;
                break;
            case FTS_F:
                callback = callback_f;
                break;
            case FTS_SL:
                callback = callback_sl;
                break;
            case FTS_DEFAULT:
                if (is_whiteout(cur->fts_statp)) {
                    callback = callback_whiteout;
                } else {
                    return_val = -1;
                    fprintf(stderr, "File %s is a special file (device or pipe). We cannot handle that.\n", cur->fts_path);
                }
                break;
            default:
                return_val = -1;
                fprintf(stderr, "Error occured when opening %s.\n", cur->fts_path);
        }
        if (callback != NULL) {
            int fts_instr = 0;
            struct stat lower_status;
            bool lower_exist = true;
            strcpy(&lower_path[lower_root_len], &(cur->fts_path[upper_root_len]));
            if (lstat(lower_path, &lower_status) != 0) {
                if (errno == ENOENT || errno == ENOTDIR) { // the corresponding lower file (or its ancestor) does not exist at all
                    lower_exist = false;
                } else { // stat failed for some unknown reason
                    fprintf(stderr, "Failed to stat %s.\n", lower_path);
                    return_val = -1;
                    break; // do not call callback in this case
                }
            }
            return_val = callback(lower_path, cur->fts_path, lower_root_len, lower_exist ? &lower_status : NULL, cur->fts_statp, verbose, script_stream, &fts_instr); // return_val must previously be 0
            if (fts_instr) {
                fts_set(ftsp, cur, fts_instr);
            }
        }
    }
    if (errno) { return_val = -1; } // if no error happened, fts_read will "sets the external variable errno to 0" according to the documentation
    return fts_close(ftsp) || return_val;
}

int vacuum(const char* lowerdir, const char* upperdir, bool is_verbose, FILE* script_stream) {
    return traverse(lowerdir, upperdir, is_verbose, script_stream, vacuum_d, vacuum_dp, vacuum_f, vacuum_sl, NULL);
}

int diff(const char* lowerdir, const char* upperdir, bool is_verbose) {
    return traverse(lowerdir, upperdir, is_verbose, NULL, diff_d, NULL, diff_f, diff_sl, diff_whiteout);
}

int merge(const char* lowerdir, const char* upperdir, bool is_verbose, FILE* script_stream) {
    return traverse(lowerdir, upperdir, is_verbose, script_stream, merge_d, merge_dp, merge_f_sl, merge_f_sl, merge_whiteout);
}
