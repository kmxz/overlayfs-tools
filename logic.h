/*
 * logic.h / logic.c
 *
 * the logic for the three feature functions
 */

#ifndef OVERLAYFS_TOOLS_LOGIC_H
#define OVERLAYFS_TOOLS_LOGIC_H

#include <stdbool.h>

/*
 * always call this to set up global variables, so the three feature functions below
 */
void set_globals(const char* lowerdir, const char* upperdir, bool is_verbose);

/*
 * feature function. will take very long time to complete. returns 0 on success
 */
int vaccum();

/*
 * feature function. will take very long time to complete. returns 0 on success
 */
int diff();

/*
 * feature function. will take very long time to complete. returns 0 on success
 */
int merge();

#endif //OVERLAYFS_TOOLS_LOGIC_H
