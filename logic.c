#define _GNU_SOURCE
#include <ftw.h>
#include <stdio.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <attr/xattr.h>
#include "logic.h"

#define WHITEOUT_DEV 0 // exactly the same as in linux/fs.h
const char *ovl_opaque_xattr = "trusted.overlay.opaque"; // exact the same as in fs/overlayfs/super.c

#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))

#define TRAILING_SLASH(status) ((file_type(status) == S_IFDIR) ? "/" : "")

bool verbose;
int file_limit; // number of file descriptors might be kept at the same time, limited by the os
char lower[PATH_MAX]; // lower directory: it might be appended with other components of the path, so only take lower_dir_len for the root
const char *upper_dir; // upper directory (it will not be copied, yet directly pointed to the string supplied) // FIXME: not used?!
size_t lower_dir_len; // length of lower directory path
size_t upper_dir_len; // length of upper directory path (will be frequently used, so we compute it ahead and store it)
struct stat sb; // buffer for calling stat

inline mode_t file_type(const struct stat *status) {
    return status->st_mode & S_IFMT;
}

int is_opaquedir(const char *path, bool *output) {
    char val;
    int res = getxattr(path, ovl_opaque_xattr, &val, 1);
    if (res < 0) { return -1; }
    *output = (res == 1 && val == 'y');
    return 0;
}

bool is_whiteout(const struct stat *status) {
    return (file_type(status) == S_IFCHR) && (status->st_rdev == WHITEOUT_DEV);
}

int regular_file_identical(const char *lower_path, const struct stat *lower_status, const char *upper_path, const struct stat *upper_status, bool *output) {
    size_t blksize = (size_t) MIN(lower_status->st_blksize, upper_status->st_blksize);
    if (lower_status->st_size != upper_status->st_size) {
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
        read_lower = read(lower_file, lower_buffer, blksize);
        read_upper = read(upper_file, upper_buffer, blksize);
        if (read_lower < 0 || read_upper < 0) {
            fprintf(stderr, "Error occured when reading file %s.\n", &lower_path[lower_dir_len]);
            return 1;
        }
        if (memcmp(lower_buffer, upper_buffer, blksize)) { *output = false; return 0; } // the output is by default false, but we still set it for ease of reading
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
    if (lower_len < 0 || upper_len < 0 || lower_len == PATH_MAX || upper_len == PATH_MAX) {
        fprintf(stderr, "Symbolic link %s cannot be resolved.", &lower_path[lower_dir_len]);
        return -1;
    }
    lower_buffer[lower_len] = '\0';
    upper_buffer[upper_len] = '\0';
    *output = (strcmp(lower_buffer, upper_buffer) == 0);
    return 0;
}

int is_directory_empty(const char *dir_path, bool* output) {
    DIR *dir = opendir(dir_path);
    struct dirent *direntb;
    if (!dir) { return -1; }
    while ((direntb = readdir(dir)) != NULL) {
        if (strcmp(".", direntb->d_name) == 0) { continue; }
        if (strcmp("..", direntb->d_name) == 0) { continue; }
        *output = false; return 0;
    }
    *output = true;
    return closedir(dir);
}

int file_identical(const char *lower_path, const struct stat *lower_status, const char *upper_path, const struct stat *upper_status, bool *output) {
    int type = file_type(lower_status);
    if (type != file_type(upper_status)) { *output = false; return 0; }
    switch (type) { // more reliable than using flag directly
        case S_IFREG: // regular file
            return regular_file_identical(lower_path, lower_status, upper_path, upper_status, output);
        case S_IFLNK: // symbolic link
            return symbolic_link_identical(lower_path, upper_path, output);
        case S_IFDIR: // directory is special: if the upper one is empty, that means it is trivial
            return is_directory_empty(upper_path, output);
        default:
            fprintf(stderr, "File %s is not file, symbolic link or directory. We don't know what to do.\n", &lower_path[lower_dir_len]);
            return -1;
    }
}

int vacuum_callback(const char *filename, const struct stat *status, int flag, struct FTW* ftwb) {
    if (flag == FTW_NS || flag == FTW_DNR) { fprintf(stderr, "Failed to stat %s.\n", filename); return 1; }
    strcpy(&lower[lower_dir_len], &filename[upper_dir_len]); // now lower contains the full path of corresponding file
    if (stat(lower, &sb) != 0) {
        if (errno == ENONET) { // the corresponding lower file does not exist at all
            return 0;
        } else { // stat failed for some unknown reason
            fprintf(stderr, "Failed to stat %s.\n", lower);
            return 1;
        }
    }
    if (is_whiteout(status)) {
        return 0;
    }
    bool identical;
    if (file_identical(lower, &sb, filename, status, &identical)) { // error occured. file_identical has printed to stderr, so no need to print again
        return 1;
    }
    if (identical) {
        if (remove(filename)) {
            fprintf(stderr, "Deletion failed: %s%s\n", filename, TRAILING_SLASH(status));
        } else {
            printf("Deletion successful: %s%s\n", filename, TRAILING_SLASH(status));
        }
    }
    return 0;
}

int list_deleted_files_callback(const char *filename, const struct stat *status, int flag, struct FTW* ftwb) {
    if (flag == FTW_NS || flag == FTW_DNR) { fprintf(stderr, "Failed to stat %s.\n", filename); return 1; }
    printf("Deleted: %s%s\n", &filename[lower_dir_len], TRAILING_SLASH(status));
    return 0;
}

int diff_callback(const char *filename, const struct stat *status, int flag, struct FTW* ftwb) {
    if (flag == FTW_NS || flag == FTW_DNR) { fprintf(stderr, "Failed to stat %s.\n", filename); return FTW_STOP; }
    strcpy(&lower[lower_dir_len], &filename[upper_dir_len]); // now lower contains the full path of corresponding file
    if (stat(lower, &sb) != 0) {
        if (errno == ENONET) { // the corresponding lower file does not exist at all
            printf("Added: %s%s\n", &filename[upper_dir_len], TRAILING_SLASH(status));
            return ((file_type(status) == S_IFDIR) && (!verbose)) ? FTW_SKIP_SUBTREE : FTW_CONTINUE;
        } else { // stat failed for some unknown reason
            fprintf(stderr, "Failed to stat %s.\n", lower);
            return FTW_STOP;
        }
    }
    if (file_type(status) == S_IFDIR) {
        if (file_type(&sb) != S_IFDIR) { // only upper is directory
            printf("Removed: %s%s\n", &lower[lower_dir_len], "");
            printf("Added: %s%s\n", &filename[upper_dir_len], "/");
            return verbose ? FTW_CONTINUE : FTW_SKIP_SUBTREE;
        }
    } else { // upper is not directory
        if (file_type(&sb) == S_IFDIR) { // but lower is
            if (verbose) {
                if (nftw(lower, list_deleted_files_callback, file_limit, FTW_DEPTH | FTW_PHYS)) { // calling ftw inside ftw... is it ok?
                    return FTW_STOP;
                }
            }
            printf("Removed: %s%s\n", &lower[lower_dir_len], "/");
            if (!is_whiteout(status)) {
                printf("Added: %s%s\n", &filename[upper_dir_len], "");
            }
        } else { // the lower is not directory either
            bool identical;
            if (file_identical(lower, &sb, filename, status, &identical)) {
                return FTW_STOP;
            }
            if (!identical) {
                printf("Modified: %s\n", &lower[lower_dir_len]);
            }
        }
    }
    return FTW_CONTINUE;
}

int merge_callback(const char *filename, const struct stat *status, int flag, struct FTW* ftwb) {
    if (flag == FTW_NS || flag == FTW_DNR) { fprintf(stderr, "Failed to stat %s.\n", filename); return FTW_STOP; }
    strcpy(&lower[lower_dir_len], &filename[upper_dir_len]); // now lower contains the full path of corresponding file
    if (stat(lower, &sb) != 0) {
        if (errno == ENONET) { // the corresponding lower file does not exist at all
            printf("Moving added file to lowerdir: %s%s\n", &filename[upper_dir_len], TRAILING_SLASH(status));
            // TODO
        } else { // stat failed for some unknown reason
            fprintf(stderr, "Failed to stat %s.\n", lower);
            return FTW_STOP;
        }
    }
    // TODO
}

void set_globals(const char *lower_root, const char *upper_root, bool is_verbose) {
    struct rlimit rlimit_file;
    strcpy(lower, lower_root);
    upper_dir = upper_root;
    lower_dir_len = strlen(lower_root);
    upper_dir_len = strlen(upper_root);
    verbose = is_verbose;
    if (!getrlimit(RLIMIT_NOFILE, &rlimit_file)) {
        file_limit = (int) ((rlimit_file.rlim_cur - 4) / 2); // preserve 4 file descriptors for global use
        if (file_limit < 2) { file_limit = 2; }
    } else {
        file_limit = 2; // default to be 2 if getrlimit fails
    }
}

int vacuum() { // returns 0 on success
    return nftw(upper_dir, vacuum_callback, file_limit, FTW_DEPTH | FTW_PHYS); // post-order, so we delete the children first, enabling us to see whether the parent can be deleted
}

int diff() { // returns 0 on success
    return nftw(upper_dir, diff_callback, file_limit, FTW_ACTIONRETVAL | FTW_PHYS);
}

int merge() { // returns 0 on success
    return nftw(upper_dir, merge_callback, file_limit, FTW_PHYS);
}