#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <ftw.h>
#include <linux/limits.h>
#include <string.h>
#include "mv.h"

#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
#define TRY_RENAME int renamed = rename(src, dst); if (renamed == 0 || (errno != EXDEV)) { return renamed; }

char mv_dir_dst[PATH_MAX];
size_t mv_dir_src_root_len;
size_t mv_dir_dst_root_len;

int slow_mv_reg(const char *src, const char *dst) {
    int src_fd = open(src, O_RDONLY);
    int dst_fd = open(dst, O_RDWR | O_CREAT);
    struct stat src_stat;
    struct stat dst_stat;
    if (fstat(src_fd, &src_stat) || fstat(dst_fd, &dst_stat)) {
        return -1;
    }
    if (ftruncate(dst_fd, src_stat.st_size)) { return -1; }
    size_t blksize = (size_t) MIN(src_stat.st_blksize, dst_stat.st_blksize);
    char buffer[blksize];
    ssize_t read_r;
    while ((read_r = read(src_fd, buffer, blksize)) > 0) {
        if (write(dst_fd, buffer, (size_t) read_r) < 0) {
            return -1; // in any case that error occured, the program will exit. so there is no need to close the file descriptors here
        }
    }
    if (read_r < 0) {
        return -1;
    }
    if (fchmod(dst_fd, src_stat.st_mode)) { return -1; }
    if (close(src_fd)) { return -1; }
    if (close(dst_fd)) { return -1; }
    if (unlink(src)) {
        return -1;
    }
    return 0;
}

int slow_mv_lnk(const char *src, const char *dst) {
    char buf[PATH_MAX];
    ssize_t read_r = readlink(src, buf, PATH_MAX);
    if (read_r < 0 || read_r == PATH_MAX) {
        return -1;
    }
    buf[read_r] = '\0';
    return symlink(buf, dst);
}

int slow_mv_dir_callback(const char *filename, const struct stat *status, int flag, struct FTW* ftwb) {
    strcpy(&mv_dir_dst[mv_dir_dst_root_len], &filename[mv_dir_src_root_len]);
    switch (flag) {
        case FTW_D:
            return mkdir(mv_dir_dst, status->st_mode);
        case FTW_F:
            return slow_mv_reg(filename, mv_dir_dst); // we are certainly in slow
        case FTW_SL:
            return slow_mv_lnk(filename, mv_dir_dst);
        default:
            return -1;
    }
}

int slow_mv_dir(const char *src, const char *dst, int file_limit) {
    strcpy(mv_dir_dst, dst);
    mv_dir_src_root_len = strlen(src);
    mv_dir_dst_root_len = strlen(dst);
    return nftw(src, slow_mv_dir_callback, file_limit, FTW_PHYS);
}

int mv_reg(const char *src, const char *dst) {
    TRY_RENAME
    return slow_mv_reg(src, dst);
}

int mv_dir(const char *src, const char *dst, int file_limit) {
    TRY_RENAME
    return slow_mv_dir(src, dst, file_limit);
}

int mv_lnk(const char *src, const char *dst) {
    TRY_RENAME
    return slow_mv_lnk(src, dst);
}

int main(int argc, char **argv) {
    mv_dir(argv[1], argv[2], 4);
}