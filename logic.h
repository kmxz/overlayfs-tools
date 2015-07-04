/*
 * logic.h / logic.c
 *
 * the logic for the three feature functions
 */

#ifndef OVERLAYFS_TOOLS_LOGIC_H
#define OVERLAYFS_TOOLS_LOGIC_H

#include <stdbool.h>

/*
 * feature function. will take very long time to complete. returns 0 on success
 */
int vaccum(const char* lowerdir, const char* upperdir, bool is_verbose, FILE* script_stream);

/*
 * feature function. will take very long time to complete. returns 0 on success
 */
int diff(const char* lowerdir, const char* upperdir, bool is_verbose);

/*
 * feature function. will take very long time to complete. returns 0 on success
 */
int merge(const char* lowerdir, const char* upperdir, bool is_verbose, FILE* script_stream);

#endif //OVERLAYFS_TOOLS_LOGIC_H
