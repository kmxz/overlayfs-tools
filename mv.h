/*
 * mv.h / mv.c
 *
 * function behave like "mv" command
 * if src and dst are in same filesystem, use "rename" directly
 * if they are in different filesystems, and dst exists, copy chunk by chunk to overwrite the existing inode
 * otherwise, copy chunk by chunk to create a new file
 */

#ifndef OVERLAYFS_UTILS_MV_H
#define OVERLAYFS_UTILS_MV_H

#include <sys/stat.h>

int mv(const char* src, const char* dst); // assume dst does not exist if status_dst is NULL. returns 0 on success

#endif //OVERLAYFS_UTILS_MV_H
