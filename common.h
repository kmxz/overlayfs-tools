/*
 * Copyright (c) 2017 Huawei.  All Rights Reserved.
 * Author: zhangyi (F) <yi.zhang@huawei.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef OVL_COMMON_H
#define OVL_COMMON_H

#ifndef __attribute__
# if !defined __GNUC__ || __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 8) || __STRICT_ANSI__
#  define __attribute__(x)
# endif
#endif

#ifdef USE_GETTEXT
#include <libintl.h>
#define _(x)	gettext((x))
#else
#define _(x) 	(x)
#endif

/* Print an error message */
void print_err(char *, ...) __attribute__ ((__format__ (__printf__, 1, 2)));

/* Print an info message */
void print_info(char *, ...) __attribute__ ((__format__ (__printf__, 1, 2)));

/* Print an debug message */
void print_debug(char *, ...) __attribute__ ((__format__ (__printf__, 1, 2)));

/* Safety wrapper */
void *smalloc(size_t size);
void *srealloc(void *addr, size_t size);
char *sstrdup(const char *src);
char *sstrndup(const char *src, size_t num);

/* Print program version */
void version(void);

#endif /* OVL_COMMON_H */
