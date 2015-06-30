/*
 * logic.h / logic.c
 *
 * the logic for the three feature functions are written in logic.c
 * call set_globals to set the lowerdir and upperdir, then call the three feature functions
 */

#ifndef OVERLAYFS_UTILS_LOGIC_H
#define OVERLAYFS_UTILS_LOGIC_H

#include <stdbool.h>

void set_globals(const char* lowerdir, const char* upperdir, bool is_verbose);

int vaccum(); // returns 0 on success

int diff(); // returns 0 on success

int merge(); // returns 0 on success

#endif //OVERLAYFS_UTILS_LOGIC_H
