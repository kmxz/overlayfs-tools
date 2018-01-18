/*
 * path.c - path manipulation
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

#include <stdlib.h>
#include <string.h>

char *joinname(const char *path, const char *name)
{
	int lenp = strlen(path);
	int lenn = strlen(name);
	int len;
	int slash = 0;
	char *str;

	if (path[0] == '.' && lenp == 1)
		lenp = 0;
	if (name[0] == '.' && lenn == 1)
		lenn = 0;

	len = lenp + lenn + 1;
	if ((lenp > 0) && (path[lenp-1] != '/') &&
	    (lenn > 0) && (name[0] != '/')) {
		slash = 1;
		len++;
	}

	str = malloc(len);
	if (!str)
		goto out;

	memcpy(str, path, lenp);
	if (slash) {
		str[lenp] = '/';
		lenp++;
	}
	memcpy(str+lenp, name, lenn+1);
out:
	return str;
}

char *basename2(const char *path, const char *dir)
{
	static const char dot[] = ".";
	int lend = strlen(dir);
	const char *str;

	if (dir[0] == '.')
		lend = 0;

	if (lend > 0 && (dir[lend-1] == '/'))
		lend--;

	if (!strncmp(path, dir, lend)) {
		for (str = path + lend; *str == '/'; str++);
		if (str[0] == '\0')
			str = dot;
		return (char *)str;
	} else {
		return (char *)path;
	}
}
