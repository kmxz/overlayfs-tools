/*
 * check.c - Check and fix inconsistency for all underlying layers of overlay
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <sys/stat.h>
#include <linux/limits.h>

#include "common.h"
#include "lib.h"
#include "check.h"
#include "path.h"
#include "list.h"
#include "overlayfs.h"

/* Lookup context */
struct ovl_lookup_ctx {
	int dirfd;		/* base overlay dir descriptor */
	const char *pathname;	/* relative path to lookup */
	bool last;		/* in last lower layer ? */
	bool skip;		/* skip self check */

	char *redirect;		/* redirect path for next lookup */
	bool stop;		/* stop lookup */

	struct stat st;		/* target's stat(2) */
	bool exist;		/* tatget exist or not */
};

/* Underlying target information */
struct ovl_lookup_data {
	bool exist;			/* tatget exist or not */
	char pathname[PATH_MAX];	/* tatget's pathname found */
	int stack;			/* which lower stack we found */
	struct stat st;			/* target's stat(2) */
};

/* Redirect information */
struct ovl_redirect_entry {
	struct list_head list;
	char *origin;		/* origin dir path */
	int ostack;		/* origin dir stack */
	char *pathname; 	/* redirect dir path */
	int dirtype;		/* redirect dir type: OVL_UPPER or OVL_LOWER */
	int stack;		/* redirect dir stack (valid in OVL_LOWER) */
};

/* Whiteout */
#define WHITEOUT_DEV	0
#define WHITEOUT_MOD	0

extern char **lowerdir;
extern char *upperdir;
extern char *workdir;
extern int *lowerfd;
extern int upperfd;
extern int lower_num;
extern int flags;
extern int status;

static inline mode_t file_type(const struct stat *status)
{
	return status->st_mode & S_IFMT;
}

static inline bool is_whiteout(const struct stat *status)
{
	return (file_type(status) == S_IFCHR) && (status->st_rdev == WHITEOUT_DEV);
}

static inline bool is_dir(const struct stat *status)
{
	return file_type(status) == S_IFDIR;
}

static bool is_dir_xattr(int dirfd, const char *pathname,
			 const char *xattrname)
{
	char *val = NULL;
	ssize_t ret;
	bool exist;

	ret = get_xattr(dirfd, pathname, xattrname, &val, NULL);
	if (ret <= 0 || !val)
		return false;

	exist = (ret == 1 && val[0] == 'y') ? true : false;
	free(val);
	return exist;
}

static inline bool ovl_is_opaque(int dirfd, const char *pathname)
{
	return is_dir_xattr(dirfd, pathname, OVL_OPAQUE_XATTR);
}

static inline int ovl_remove_opaque(int dirfd, const char *pathname)
{
	return remove_xattr(dirfd, pathname, OVL_OPAQUE_XATTR);
}

static inline int ovl_set_opaque(int dirfd, const char *pathname)
{
	return set_xattr(dirfd, pathname, OVL_OPAQUE_XATTR, "y", 1);
}

static inline int ovl_is_impure(int dirfd, const char *pathname)
{
	return is_dir_xattr(dirfd, pathname, OVL_IMPURE_XATTR);
}

static inline int ovl_set_impure(int dirfd, const char *pathname)
{
	return set_xattr(dirfd, pathname, OVL_IMPURE_XATTR, "y", 1);
}

static int ovl_get_redirect(int dirfd, const char *pathname,
			    char **redirect)
{
	char *rd = NULL;
	ssize_t ret;

	ret = get_xattr(dirfd, pathname, OVL_REDIRECT_XATTR, &rd, NULL);
	if (ret <= 0 || !rd)
		return ret;

	if (rd[0] != '/') {
		char *tmp = sstrdup(pathname);

		*redirect = joinname(dirname(tmp), rd);
		free(tmp);
		free(rd);
	} else {
		ret -= 1;
		memmove(rd, rd+1, ret);
		rd[ret] = '\0';
		*redirect = rd;
	}

	return 0;
}

static inline int ovl_remove_redirect(int dirfd, const char *pathname)
{
	return remove_xattr(dirfd, pathname, OVL_REDIRECT_XATTR);
}

static inline int ovl_create_whiteout(int dirfd, const char *pathname)
{
	if (mknodat(dirfd, pathname, S_IFCHR | WHITEOUT_MOD, makedev(0, 0))) {
		print_err(_("Cannot mknod %s:%s\n"), pathname,
			    strerror(errno));
		return -1;
	}
	return 0;
}

static inline bool ovl_is_redirect(int dirfd, const char *pathname)
{
	bool exist = false;
	get_xattr(dirfd, pathname, OVL_REDIRECT_XATTR, NULL, &exist);
	return exist;
}

static inline bool ovl_is_origin(int dirfd, const char *pathname)
{
	bool exist = false;
	get_xattr(dirfd, pathname, OVL_ORIGIN_XATTR, NULL, &exist);
	return exist;
}

static inline int ovl_ask_action(const char *description, const char *pathname,
				 int dirtype, int stack,
				 const char *question, int action)
{
	if (dirtype == OVL_UPPER)
		print_info(_("%s: \"%s\" in %s "),
			     description, pathname, "upperdir");
	else
		print_info(_("%s: \"%s\" in %s-%d "),
			     description, pathname, "lowerdir", stack);

	return ask_question(question, action);
}

static inline int ovl_ask_question(const char *question, const char *pathname,
				   int dirtype, int stack,
				   int action)
{
	if (dirtype == OVL_UPPER)
		print_info(_("%s: \"%s\" in %s "),
			     question, pathname, "upperdir");
	else
		print_info(_("%s: \"%s\" in %s-%d "),
			     question, pathname, "lowerdir", stack);

	return ask_question("", action);
}

static int ovl_lookup_single(int dirfd, const char *pathname,
			     struct stat *st, bool *exist)
{
	if (fstatat(dirfd, pathname, st,
		    AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW)) {
		if (errno != ENOENT && errno != ENOTDIR) {
			print_err(_("Cannot stat %s: %s\n"), pathname,
				    strerror(errno));
			return -1;
		}
		*exist = false;
	} else {
		*exist = true;
	}
	return 0;
}

static int ovl_lookup_layer(struct ovl_lookup_ctx *lctx)
{
	char *pathname;
	int ret = 0;

	if (!lctx->skip) {
		if (ovl_lookup_single(lctx->dirfd, lctx->pathname,
				      &lctx->st, &lctx->exist))
			return -1;
	}

	/* If we found or in the bottom layer, no need to iterate */
	if (lctx->exist || lctx->last)
		return 0;

	/*
	 * Check if we should stop or redirect for the next layer's lookup.
	 *
	 * Iterate to the first item in the path. If a redirect dir was found,
	 * change path for the next lookup. If an opaque directory or a file
	 * was found, stop lookup.
	 */
	pathname = sstrdup(lctx->pathname);
	while (strcmp(dirname(pathname), ".")) {
		char *redirect = NULL;
		bool exist = false;
		struct stat st;

		ret = ovl_lookup_single(lctx->dirfd, pathname, &st, &exist);
		if (ret)
			goto out;

		if (!exist)
			continue;

		if (!is_dir(&st) || ovl_is_opaque(lctx->dirfd, pathname)) {
			lctx->stop = true;
			goto out;
		}

		if (ovl_is_redirect(lctx->dirfd, pathname)) {
			ret = ovl_get_redirect(lctx->dirfd, pathname, &redirect);
			if (ret)
				goto out;

			free(lctx->redirect);
			lctx->redirect = joinname(redirect,
					 basename2(lctx->pathname, pathname));
			free(redirect);
			goto out;
		}
	}
out:
	free(pathname);
	return ret;
}

/*
 * Lookup the lower layers have the same target with the specific one or not.
 *
 * Scan each lower directory start from the layer of 'start' and lookup target
 * until find something (skip target founded in start layer), Iterate parent
 * directories and check directory type, following redirect directory and
 * terminate scan if there is a file or an opaque directory exists.
 */
static int ovl_lookup_lower(const char *pathname, int dirtype,
			    int start, struct ovl_lookup_data *od)
{
	struct ovl_lookup_ctx lctx = {0};
	int i;
	int ret = 0;

	if (dirtype == OVL_UPPER)
		start = 0;

	if (dirtype == OVL_UPPER) {
		lctx.dirfd = upperfd;
		lctx.pathname = pathname;
		lctx.skip = true;

		ret = ovl_lookup_layer(&lctx);
		if (ret)
			goto out;
	}

	for (i = start; !lctx.stop && i < lower_num; i++) {
		lctx.dirfd = lowerfd[i];
		lctx.pathname = (lctx.redirect) ? lctx.redirect : pathname;
		lctx.skip = (dirtype == OVL_LOWER && i == start) ? true : false;
		lctx.last = (i == lower_num - 1) ? true : false;

		ret = ovl_lookup_layer(&lctx);
		if (ret)
			goto out;

		if (lctx.exist)
			break;
	}

	od->exist = lctx.exist;
	if (od->exist) {
		strncpy(od->pathname, lctx.pathname, sizeof(od->pathname));
		od->stack = i;
		od->st = lctx.st;
	}
out:
	free(lctx.redirect);
	return ret;
}

/*
 * The same as ovl_lookup_lower() except start from the layer of 'start',
 * not skip any target founded.
 */
static int ovl_lookup(const char *pathname, int start,
		      struct ovl_lookup_data *od)
{
	struct ovl_lookup_ctx lctx = {0};
	int i;
	int ret = 0;

	for (i = start; !lctx.stop && i < lower_num; i++) {
		lctx.dirfd = lowerfd[i];
		lctx.pathname = (lctx.redirect) ? lctx.redirect : pathname;
		lctx.last = (i == lower_num - 1) ? true : false;

		ret = ovl_lookup_layer(&lctx);
		if (ret)
			goto out;

		if (lctx.exist)
			break;
	}

	od->exist = lctx.exist;
	if (od->exist) {
		strncpy(od->pathname, lctx.pathname, sizeof(od->pathname));
		od->stack = i;
		od->st = lctx.st;
	}
out:
	free(lctx.redirect);
	return ret;
}

/*
 * Scan each underlying dirs under specified dir if a whiteout is
 * found, check it's orphan or not. In auto-mode, orphan whiteouts
 * will be removed directly.
 */
static int ovl_check_whiteout(struct scan_ctx *sctx)
{
	const char *pathname = sctx->pathname;
	const struct stat *st = sctx->st;
	struct ovl_lookup_data od = {0};
	int ret = 0;

	/* Is a whiteout ? */
	if (!is_whiteout(st))
		return 0;

	sctx->t_whiteouts++;

	/* Is whiteout in the bottom lower dir ? */
	if (sctx->dirtype == OVL_LOWER && sctx->stack == lower_num-1)
		goto remove;

	/*
	 * Scan each corresponding lower directroy under this layer,
	 * check is there a file or dir with the same name.
	 */
	ret = ovl_lookup_lower(pathname, sctx->dirtype, sctx->stack, &od);
	if (ret)
		goto out;

	if (od.exist && !is_whiteout(&od.st))
		goto out;

remove:
	sctx->i_whiteouts++;

	/* Remove orphan whiteout directly or ask user */
	if (!ovl_ask_action("Orphan whiteout", pathname, sctx->dirtype,
			    sctx->stack, "Remove", 1))
		return 0;

	ret = unlinkat(sctx->dirfd, pathname, 0);
	if (ret) {
		print_err(_("Cannot unlink %s: %s\n"), pathname,
			    strerror(errno));
		goto out;
	}
	sctx->t_whiteouts--;
	sctx->i_whiteouts--;
out:
	return ret;
}

LIST_HEAD(redirect_list);

static void ovl_redirect_entry_add(const char *pathname, int dirtype, int stack,
				   const char *origin, int ostack)
{
	struct ovl_redirect_entry *new;

	new = smalloc(sizeof(*new));
	INIT_LIST_HEAD(&new->list);

	print_debug(_("Redirect entry add: [%s %s %d][%s %d]\n"),
		      pathname, (dirtype == OVL_UPPER) ? "upper" : "lower",
		      (dirtype == OVL_UPPER) ? 0 : stack,
		      origin, ostack);

	new->pathname = sstrdup(pathname);
	new->dirtype = dirtype;
	new->stack = stack;
	new->origin = sstrdup(origin);
	new->ostack = ostack;

	list_add(&new->list, &redirect_list);
}

static bool ovl_redirect_entry_find(const char *origin, int ostack,
				    int *dirtype, int *stack, char **pathname)
{
	struct ovl_redirect_entry *entry;
	struct list_head *node;

	if (list_empty(&redirect_list))
		return false;

	list_for_each(node, &redirect_list) {
		entry = list_entry(node, struct ovl_redirect_entry, list);

		if (entry->ostack == ostack && !strcmp(entry->origin, origin)) {
			*pathname = entry->pathname;
			*dirtype = entry->dirtype;
			*stack = entry->stack;
			return true;
		}
	}

	return false;
}

static void ovl_redirect_entry_del(const char *origin, int ostack)
{
	struct ovl_redirect_entry *entry;
	struct list_head *node, *tmp;

	if (list_empty(&redirect_list))
		return;

	list_for_each_safe(node, tmp, &redirect_list) {
		entry = list_entry(node, struct ovl_redirect_entry, list);

		if (entry->ostack == ostack && !strcmp(entry->origin, origin)) {
			print_debug(_("Redirect entry del: [%s %s %d][%s %d]\n"),
				      entry->pathname,
				      (entry->dirtype == OVL_UPPER) ? "upper" : "lower",
				      (entry->dirtype == OVL_UPPER) ? 0 : entry->stack,
				      entry->origin, entry->ostack);

			list_del_init(node);
			free(entry->pathname);
			free(entry->origin);
			free(entry);
			return;
		}
	}
}

static bool ovl_redirect_is_duplicate(const char *origin, int ostack)
{
	int dirtype, stack;
	char *dup;

	if (ovl_redirect_entry_find(origin, ostack, &dirtype, &stack, &dup)) {
		print_debug("Duplicate redirect dir found: Origin:%s in lower %d, "
			    "Previous:%s in %s %d\n",
			    origin, ostack, dup,
			    (dirtype == OVL_UPPER) ? "upper" : "lower", stack);
		return true;
	}
	return false;
}

static void ovl_redirect_free(void)
{
	struct ovl_redirect_entry *entry;
	struct list_head *node, *tmp;

	list_for_each_safe(node, tmp, &redirect_list) {
		entry = list_entry(node, struct ovl_redirect_entry, list);
		list_del_init(node);
		free(entry->origin);
		free(entry->pathname);
		free(entry);
	}
}

static int ovl_do_remove_redirect(int dirfd, const char *pathname,
				  int dirtype, int stack,
				  int *total, int *invalid)
{
	struct ovl_lookup_data od = {0};
	int du_dirtype, du_stack;
	char *duplicate;
	int ret;

	ret = ovl_remove_redirect(dirfd, pathname);
	if (ret)
		goto out;

	(*total)--;
	(*invalid)--;

	/* If lower corresponding dir exists, ask user to set opaque */
	ret = ovl_lookup_lower(pathname, dirtype, stack, &od);
	if (ret)
		goto out;

	if (!od.exist || !is_dir(&od.st))
		goto out;

	if (ovl_ask_question("Should set opaque dir", pathname,
			     dirtype, stack, 0)) {
		ret = ovl_set_opaque(dirfd, pathname);
		goto out;
	}

	if (ovl_redirect_entry_find(od.pathname, od.stack, &du_dirtype,
				    &du_stack, &duplicate)) {
		/*
		 * The redirect dir point to the same lower origin becomes
		 * duplicate, should re-ask
		 */
		if ((du_dirtype == dirtype) && (du_stack == stack) &&
		    ovl_ask_action("Duplicate redirect directory",
                                   duplicate, du_dirtype, du_stack,
                                   "Remove redirect", 0)) {
			(*invalid)++;
			ret = ovl_do_remove_redirect(dirfd, duplicate, dirtype,
						     stack, total, invalid);
			if (ret)
				goto out;

			ovl_redirect_entry_del(od.pathname, od.stack);
		}
	}
out:
	return ret;
}

/*
 * Get redirect origin directory stored in the xattr, check it's invlaid
 * or not, In auto-mode, invalid redirect xattr will be removed directly.
 * Do the follow checking:
 * 1) Check the origin directory exist or not. If not, remove xattr.
 * 2) Check and fix the missing whiteout of the redirect origin target.
 * 3) Check redirect xattr duplicate with merge directory or another
 *    redirect directory.
 * 4) When an invalid redirect xattr is removed from a directory, also check
 *    whether it will duplicate with another redirect directory if the lower
 *    corresponding directory exists.
 * 5) If a duplicate redirect xattr is found, not sure which one is invalid
 *    and how to deal with it, so ask user by default.
 */
static int ovl_check_redirect(struct scan_ctx *sctx)
{
	const char *pathname = sctx->pathname;
	struct ovl_lookup_data od = {0};
	struct stat cover_st;
	bool cover_exist = false;
	char *redirect = NULL;
	int start;
	int ret;

	/* Get redirect */
	ret = ovl_get_redirect(sctx->dirfd, pathname, &redirect);
	if (ret || !redirect)
		return ret;

	print_debug(_("Dir \"%s\" has redirect \"%s\"\n"), pathname, redirect);
	sctx->t_redirects++;

	/* Redirect dir in last lower dir ? */
	if (sctx->dirtype == OVL_LOWER && sctx->stack == lower_num-1)
		goto remove;

	/* Scan lower directories to check redirect dir exist or not */
	start = (sctx->dirtype == OVL_LOWER) ? sctx->stack + 1 : 0;
	ret = ovl_lookup(redirect, start, &od);
	if (ret)
		goto out;

	if (od.exist && is_dir(&od.st)) {
		/* Check duplicate with another redirect dir */
		if (ovl_redirect_is_duplicate(od.pathname, od.stack)) {
			sctx->i_redirects++;

			/*
			 * Not sure which one is invalid, don't remove in
			 * auto mode
			 */
			if (ovl_ask_action("Duplicate redirect directory",
					   pathname, sctx->dirtype, sctx->stack,
					   "Remove redirect", 0))
				goto remove_d;
			else
				goto out;
		}

		/* Check duplicate with merge dir */
		ret = ovl_lookup_single(sctx->dirfd, redirect, &cover_st,
					&cover_exist);
		if (ret)
			goto out;
		if (!cover_exist) {
			/* Found nothing, create a whiteout */
			if (ovl_ask_action("Missing whiteout", pathname,
					   sctx->dirtype, sctx->stack,
					   "Add", 1)) {
				ret = ovl_create_whiteout(sctx->dirfd, redirect);
				if (ret)
					goto out;

				sctx->t_whiteouts++;
			}
		} else if (is_dir(&cover_st) &&
			   !ovl_is_opaque(sctx->dirfd, redirect) &&
			   !ovl_is_redirect(sctx->dirfd, redirect)) {
			/*
			 * Found a directory merge with the same origin,
			 * ask user to remove this duplicate redirect xattr
			 * or set opaque to the cover directory
			 */
			sctx->i_redirects++;
			if (ovl_ask_action("Duplicate redirect directory",
					   pathname, sctx->dirtype, sctx->stack,
					   "Remove redirect", 0)) {
				goto remove_d;
			} else if (ovl_ask_question("Should set opaque dir",
						    redirect, sctx->dirtype,
						    sctx->stack, 0)) {
				ret = ovl_set_opaque(sctx->dirfd, redirect);
				if (ret)
					goto out;
			} else {
				goto out;
			}
		}

		/* Now, this redirect xattr is valid */
		ovl_redirect_entry_add(pathname, sctx->dirtype, sctx->stack,
				       od.pathname, od.stack);

		goto out;
	}

remove:
	sctx->i_redirects++;

	/* Remove redirect xattr or ask user */
	if (!ovl_ask_action("Invalid redirect directory", pathname, sctx->dirtype,
			    sctx->stack, "Remove redirect", 1))
		goto out;
remove_d:
	ret = ovl_do_remove_redirect(sctx->dirfd, pathname, sctx->dirtype,
				     sctx->stack, &sctx->t_redirects,
				     &sctx->i_redirects);
out:
	free(redirect);
	return ret;
}

/*
 * If a directory has origin target and redirect/merge subdirectories in it,
 * it may contain copied up targets. In order to avoid 'd_ino' change after
 * lower target copy-up or rename (which will create a new inode),
 * 'impure xattr' will be set in the parent directory, it is used to prompt
 * overlay filesystem to get and return the origin 'd_ino' in getdents(2).
 *
 * Missing "impure xattr" will lead to return wrong 'd_ino' of impure directory.
 * So check origin target and redirect/merge subdirs in a specified directory,
 * and fix "impure xattr" if necessary.
 */
static int ovl_check_impure(struct scan_ctx *sctx)
{
	struct scan_dir_data *dirdata = sctx->dirdata;

	if (!dirdata)
		return 0;

	/*
	 * Impure xattr should be set if directory has redirect/merge
	 * subdir or origin targets.
	 */
	if (!dirdata->origins && !dirdata->mergedirs &&
	    !dirdata->redirects)
		return 0;

	if (ovl_is_impure(sctx->dirfd, sctx->pathname))
		return 0;

	/* Fix impure xattrs */
	if (ovl_ask_action("Missing impure xattr", sctx->pathname,
			   sctx->dirtype, sctx->stack, "Fix", 1)) {
		if (ovl_set_impure(sctx->dirfd, sctx->pathname))
			return -1;
	} else {
		/*
		 * Note: not enforce to fix the case of directory that
		 * only contains general merge subdirs because it could
		 * be an newly created overlay image and fix by overlay
		 * filesystem after mount.
		 */
		if (dirdata->origins || dirdata->redirects)
			sctx->m_impure++;
	}

	return 0;
}

static inline bool ovl_is_merge(int dirfd, const char *pathname,
                               int dirtype, int stack)
{
	struct ovl_lookup_data od = {0};

	if (ovl_is_opaque(dirfd, pathname))
		return false;
	if (ovl_lookup_lower(pathname, dirtype, stack, &od))
		return false;
	if (od.exist && is_dir(&od.st))
		return true;

	return false;
}

/*
 * Count impurities in a specified directory, which includes origin
 * targets, redirect dirs and merge dirs.
 */
static int ovl_count_impurity(struct scan_ctx *sctx)
{
	struct scan_dir_data *parent = sctx->dirdata;

	if (!parent)
		return 0;

	if (ovl_is_origin(sctx->dirfd, sctx->pathname))
		parent->origins++;

	if (is_dir(sctx->st)) {
		if (ovl_is_redirect(sctx->dirfd, sctx->pathname))
			parent->redirects++;
		if (ovl_is_merge(sctx->dirfd, sctx->pathname,
				 sctx->dirtype, sctx->stack))
			parent->mergedirs++;
	}

	return 0;
}

/*
 * Scan Pass:
 * -Pass one: Iterate through all directories, and check validity
 *            of redirect directory, include duplicate redirect
 *            directory. After this pass, the hierarchical structure
 *            of each layer's directories becomes consistent.
 * -Pass two: Iterate through all directories, and find and check
 *            validity of whiteouts, and check missing impure xattr
 *            in upperdir.
 */

static struct scan_operations ovl_scan_ops[OVL_SCAN_PASS][2] = {
	{
		[OVL_UPPER] = {
			.redirect = ovl_check_redirect,
		},
		[OVL_LOWER] = {
			.redirect = ovl_check_redirect,
		},
	},/* Pass One */
	{
		[OVL_UPPER] = {
			.whiteout = ovl_check_whiteout,
			.impurity = ovl_count_impurity,
			.impure = ovl_check_impure,
		},
		[OVL_LOWER] = {
			.whiteout = ovl_check_whiteout,
		},
	}/* Pass Two */
};

static char *ovl_scan_desc[OVL_SCAN_PASS] = {
	"Checking redirect xattr and directory tree",
	"Checking whiteouts and impure xattr"
};

static void ovl_scan_clean(void)
{
	/* Clean redirect entry record */
	ovl_redirect_free();
}

static void ovl_scan_report(struct scan_ctx *sctx)
{
	if (flags & FL_VERBOSE) {
		print_info(_("Scan %d directories, %d files, "
			     "%d/%d whiteouts, %d/%d redirect dirs "
			     "%d missing impure\n"),
			     sctx->directories, sctx->files,
			     sctx->i_whiteouts, sctx->t_whiteouts,
			     sctx->i_redirects, sctx->t_redirects,
			     sctx->m_impure);
	}
}

static void ovl_scan_check(struct scan_ctx *sctx)
{
	if (sctx->i_whiteouts)
		print_info(_("Invalid whiteouts %d left!\n"),
			     sctx->i_whiteouts);
	else if (sctx->i_redirects)
		print_info(_("Invalid redirect directories %d left!\n"),
			     sctx->i_redirects);
	else if (sctx->m_impure)
		print_info(_("Directories %d missing impure xattr!\n"),
			     sctx->m_impure);
	else
		return;

	set_inconsistency(&status);
}

/* Scan upperdir and each lowerdirs, check and fix inconsistency */
int ovl_scan_fix(void)
{
	struct scan_ctx sctx = {0};
	int i,j;
	int ret;

	if (flags & FL_VERBOSE)
		print_info(_("Scan and fix: "
			     "[whiteouts|redirect dir|impure dir]\n"));

	for (i = 0; i < OVL_SCAN_PASS; i++) {
		if (flags & FL_VERBOSE)
			print_info(_("Pass %d: %s\n"), i, ovl_scan_desc[i]);

		sctx.directories = 0;
		sctx.files = 0;

		/* Scan every lower directories */
		for (j = lower_num - 1; j >= 0; j--) {
			print_debug(_("Scan lower directory %d\n"), j);

			sctx.dirname = lowerdir[j];
			sctx.dirfd = lowerfd[j];
			sctx.dirtype = OVL_LOWER;
			sctx.stack = j;

			ret = scan_dir(&sctx, &ovl_scan_ops[i][OVL_LOWER]);
			if (ret)
				goto out;
		}

		/* Scan upper directory */
		if (flags & FL_UPPER) {
			print_debug(_("Scan upper directory\n"));

			sctx.dirname = upperdir;
			sctx.dirfd = upperfd;
			sctx.dirtype = OVL_UPPER;
			sctx.stack = 0;

			ret = scan_dir(&sctx, &ovl_scan_ops[i][OVL_UPPER]);
			if (ret)
				goto out;
		}

		/* Check scan result for this pass */
		ovl_scan_check(&sctx);
	}
out:
	ovl_scan_report(&sctx);
	ovl_scan_clean();
	return ret;
}
