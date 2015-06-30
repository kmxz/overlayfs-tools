#define _GNU_SOURCE
#include <ftw.h>
#include <stdio.h>
#include <sys/resource.h>
#include <linux/limits.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "logic.h"

#define WHITEOUT_DEV 0 // exactly the same as in linux/fs.h

bool verbose;
rlim_t file_limit; // number of file descriptors might be kept at the same time, limited by the os
char lower[PATH_MAX]; // lower directory: it might be appended with other components of the path, so only take lower_dir_len for the root
const char *upper_dir; // upper directory (it will not be copied, yet directly pointed to the string supplied)
size_t lower_dir_len; // length of lower directory path
size_t upper_dir_len; // length of upper directory path (will be frequently used, so we compute it ahead and store it)
struct stat sb; // buffer for calling stat

const char* const FILE = "file";
const char* const SYMBOLIC_LINK = "symbolic link";
const char* const DIRECTORY = "directory";
const char* const SPECIAL_FILE = "special file";

const char* const get_type_string(int type) {
    switch (type) {
        case S_IFREG:
            return FILE;
        case S_IFLNK:
            return SYMBOLIC_LINK;
        case S_IFDIR:
            return DIRECTORY;
        default:
            return SPECIAL_FILE;
    }
}

inline mode_t file_type(struct stat *status) {
    return status->st_mode & S_IFMT;
}

bool is_whiteout(struct stat *status) {
    return (file_type(status) == S_IFCHR) && (status->rdev == WHITEOUT_DEV);
}

int regular_file_identical(const char *lower_path, const struct stat *lower_status, const char *upper_path, const struct stat *upper_status, bool *output) {
    size_t blksize = lower_status->st_blksize < upper_status->st_blksize ? lower_status->st_blksize : upper_status->st_blksize;
    if (lower_status->st_size != upper_status->st_size) {
        return 0;
    }
    char lower_buffer[blksize];
    char upper_buffer[blksize];
    FILE* lower_file = fopen(lower_path, "rb");
    FILE* upper_file = fopen(upper_path, "rb");
    if (lower_file == NULL) {
        fprintf(stderr, "File %s can not be read for content.\n", lower_path);
        return EXIT_FAILURE;
    }
    if (upper_file == NULL) {
        fprintf(stderr, "File %s can not be read for content.\n", upper_path);
        return EXIT_FAILURE;
    }
    while (!(feof(lower_file) || feof(upper_file))) { // we can assume one will not reach EOF earlier than the other, as the checked file sizes are checked above
        fread(lower_buffer, 1, blksize, lower_file);
        fread(upper_buffer, 1, blksize, upper_file);
        if (ferror(lower_file) || ferror(upper_file)) {
            fprintf(stderr, "Error occured when reading file %s.\n", &lower_path[lower_dir_len]);
            return EXIT_FAILURE;
        }
        if (memcmp(lower_buffer, upper_buffer, blksize)) { *output = false; return 0; } // the output is by default false, but we still set it for ease of reading
    }
    *output = true; // now we can say they are identical
    return 0;
}

int symbolic_link_identical(const char *lower_path, const char *upper_path, bool *output) {
    char lower_buffer[PATH_MAX];
    char upper_buffer[PATH_MAX];
    ssize_t lower_len = readlink(lower_path, lower_buffer, PATH_MAX);
    ssize_t upper_len = readlink(upper_path, upper_buffer, PATH_MAX);
    if (lower_len < 0 || upper_len < 0 || lower_len == PATH_MAX || upper_len == PATH_MAX) {
        fprintf(stderr, "Symbolic link %s cannot be resolved.", &lower_path[lower_dir_len]);
        return EXIT_FAILURE;
    }
    lower_buffer[lower_len] = '\0';
    upper_buffer[upper_len] = '\0';
    *output = (strcmp(lower_buffer, upper_buffer) == 0);
    return 0;
}

int is_directory_empty(const char *dir_path, bool* output) {
    DIR *dir = opendir(dir_path);
    struct dirent *direntb;
    if (!dir) { return EXIT_FAILURE; }
    while ((direntb = readdir(dir)) != NULL) {
        if (strcmp(".", direntb->d_name) == 0) { continue; }
        if (strcmp("..", direntb->d_name) == 0) { continue; }
        *output = false; return 0;
    }
    *output = true; return 0;
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
            return is_directory_empty(upper_path, *output);
        default:
            fprintf(stderr, "File %s is not file, symbolic link or directory. We don't know what to do.\n", &lower_path[lower_dir_len]);
            return EXIT_FAILURE;
    }
}

int vacuum_callback(const char *filename, const struct stat *status, int flag, struct FTW* ftwb) {
    if (flag == FTW_NS || flag == FTW_DNR) { fprintf(stderr, "Failed to stat %s.\n", filename); return EXIT_FAILURE; } // stat failed for the upper file
    strcpy(&lower[lower_dir_len], &filename[upper_dir_len]); // now lower contains the full path of corresponding file
    if (stat(lower, &sb) != 0) {
        if (errno == ENONET) { // the corresponding lower file does not exist at all
            return 0;
        } else { // stat failed for some unknown reason
            fprintf(stderr, "Failed to stat %s.\n", lower);
            return EXIT_FAILURE;
        }
    }
    if (is_whiteout(status)) {
        return 0;
    }
    bool identical;
    if (file_identical(lower, status, upper_dir, &sb, &identical)) { // error occured. file_identical has printed to stderr, so no need to print again
        return EXIT_FAILURE;
    }
    if (identical) {
        if (remove(filename)) {
            fprintf(stderr, "Deleting %s failed: %s.\n", get_type_string(file_type(status)), filename);
        } else {
            printf("Deleting %s successful: %s.\n", get_type_string(file_type(status)), filename);
        }
    }
    return 0;
}

int diff_callback(const char *filename, const struct stat *status, int flag, struct FTW* ftwb) {
    if (flag == FTW_NS || flag == FTW_DNR) { fprintf(stderr, "Failed to stat %s.\n", filename); return EXIT_FAILURE; }
    strcpy(&lower[lower_dir_len], &filename[upper_dir_len]); // now lower contains the full path of corresponding file
    if (stat(lower, &sb) != 0) {
        if (errno == ENONET) { // the corresponding lower file does not exist at all
            printf("%s");
            if ((flag == FTW_D) && (!verbose)) {
                return FTW_SKIP_SUBTREE;
            }
            return 0;
        } else { // stat failed for some unknown reason
            fprintf(stderr, "Failed to stat %s.\n", lower);
            return EXIT_FAILURE;
        }
    }
}

void set_globals(const char *lower_root, const char *upper_root, bool is_verbose) {
    struct rlimit rlimit_file;
    strcpy(lower, lower_root);
    upper_dir = upper_root;
    lower_dir_len = strlen(lower_root);
    upper_dir_len = strlen(upper_root);
    verbose = is_verbose;
    getrlimit(RLIMIT_NOFILE, &rlimit_file);
    file_limit = (rlimit_file.rlim_cur / 2);
    if (file_limit < 2) { file_limit = 2; }
}

int vacuum() {
    return nftw(upper_dir, vacuum_callback, file_limit, FTW_DEPTH | FTW_PHYS); // post-order, so we delete the children first, enabling us to see whether the parent can be deleted
}

int diff() {
    return nftw(upper_dir, diff_callback, file_limit, FTW_ACTIONRETVAL | FTW_PHYS);
}