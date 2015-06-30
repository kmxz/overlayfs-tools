#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "mv.h"

#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))

int slow_mv(const char* src, const char* dst) {
    int src_fd = open(dst, O_RDONLY);
    int dst_fd = open(dst, O_WRONLY | O_CREAT);
    struct stat src_stat;
    struct stat dst_stat;
    fstat(src_fd, &src_stat);
    fstat(dst_fd, &dst_stat);
    ftruncate(dst_fd, src_stat.st_size);
    size_t blksize = (size_t) MIN(src_stat.st_blksize, dst_stat.st_blksize);
    char buffer[blksize];
    ssize_t read_r;
    while ((read_r = read(src_fd, buffer, blksize)) > 0) {
        if (write(dst_fd, buffer, read_r) < 0) {
            return -1; // in any case that error occured, the program will exit. so there is no need to close the file descriptors here
        }
    }
    if (read_r < 0) {
        return -1;
    }
    if (close(src_fd)) { return -1; }
    if (close(dst_fd)) { return -1; }
    return 0;
}

int mv(const char* src, const char* dst) { // assume dst does not exist if status_dst is NULL
    int renamed = rename(src, dst);
    if (renamed) {
        if (errno == EXDEV) {
            return (slow_mv(src, dst) < 0) ? errno : 0;
        } else {
            return errno;
        }
    } else {
        return 0;
    }
}