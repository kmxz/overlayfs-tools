/*
 * common.c - Common things for all utilities
 *
 * Copyright (c) 2017 Huawei.  All Rights Reserved.
 * Author: zhangyi (F) <yi.zhang@huawei.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "common.h"
#include "config.h"

extern char *program_name;

/* #define DEBUG 1 */
#ifdef DEBUG
void print_debug(char *fmtstr, ...)
{
	va_list args;

	va_start(args, fmtstr);
	fprintf(stdout, "%s:[Debug]: ", program_name);
	vfprintf(stdout, fmtstr, args);
	va_end(args);
}
#else
void print_debug (char *fmtstr, ...) {}
#endif

void print_info(char *fmtstr, ...)
{
	va_list args;

	va_start(args, fmtstr);
	vfprintf(stdout, fmtstr, args);
	va_end(args);
}

void print_err(char *fmtstr, ...)
{
	va_list args;

	va_start(args, fmtstr);
	fprintf(stderr, "%s:[Error]: ", program_name);
	vfprintf(stderr, fmtstr, args);
	va_end(args);
}

void *smalloc(size_t size)
{
	void *new = malloc(size);

	if (!new) {
		print_err(_("malloc error:%s\n"), strerror(errno));
		exit(1);
	}

	memset(new, 0, size);
	return new;
}

void *srealloc(void *addr, size_t size)
{
	void *re = realloc(addr, size);

	if (!re) {
		print_err(_("malloc error:%s\n"), strerror(errno));
		exit(1);
	}
	return re;
}

char *sstrdup(const char *src)
{
	char *dst = strdup(src);

	if (!dst) {
		print_err(_("strdup error:%s\n"), strerror(errno));
		exit(1);
	}

	return dst;
}

char *sstrndup(const char *src, size_t num)
{
	char *dst = strndup(src, num);

	if (!dst) {
		print_err(_("strndup error:%s\n"), strerror(errno));
		exit(1);
	}

	return dst;
}

void version(void)
{
	printf(_("Overlay utilities version %s\n"), PACKAGE_VERSION);
}
