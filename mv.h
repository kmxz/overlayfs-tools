/*
 * mv.h / mv.c
 *
 * function behave like "mv" command
 */

#ifndef OVERLAYFS_UTILS_MV_H
#define OVERLAYFS_UTILS_MV_H

#include <sys/stat.h>
#include <stdbool.h>

/*
 * returns 0 on success, -1 otherwise. errno will be available in such cases
 * is_dir is true: only when src is a directory, and dst does not exist at all
 * is_dir is false: only when src is a regular file, and dst is either a regular file or does not exist at all
 */
int mv_reg(const char *src, const char *dst);

int mv_dir(const char *src, const char *dst, int file_limit);

#endif //OVERLAYFS_UTILS_MV_H
