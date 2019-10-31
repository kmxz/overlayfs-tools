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
#include <sys/sysmacros.h>
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
	if (dirtype == OVL_UPPER || dirtype == OVL_WORK)
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
	if (dirtype == OVL_UPPER || dirtype == OVL_WORK)
		print_info(_("%s: \"%s\" in %s "),
			     question, pathname, "upperdir");
	else
		print_info(_("%s: \"%s\" in %s-%d "),
			     question, pathname, "lowerdir", stack);

	return ask_question("", action);
}

/*
 * Lookup a specified target exist or not, return the stat struct if exist
 */
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

/*
 * Lookup a specified target exist or not in a specified layer.
 * If not exist, we may want to scan the next layer, so iterate to the
 * overlay root dir to make sure we are now in redirect or opaque contex,
 * it will change base scan dirs for next layer's lookup or stop scan
 * directly.
 */
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
static int ovl_lookup_lower(const struct ovl_fs * ofs,
			    const char *pathname,
			    int dirtype, int start,
			    struct ovl_lookup_data *od)
{
	struct ovl_lookup_ctx lctx = {0};
	int i;
	int ret = 0;

	if (dirtype == OVL_UPPER)
		start = 0;

	if (dirtype == OVL_UPPER) {
		lctx.dirfd = ofs->upper_layer.fd;
		lctx.pathname = pathname;
		lctx.skip = true;

		ret = ovl_lookup_layer(&lctx);
		if (ret)
			goto out;
	}

	for (i = start; !lctx.stop && i < ofs->lower_num; i++) {
		lctx.dirfd = ofs->lower_layer[i].fd;
		lctx.pathname = (lctx.redirect) ? lctx.redirect : pathname;
		lctx.skip = (dirtype == OVL_LOWER && i == start) ? true : false;
		lctx.last = (i == ofs->lower_num - 1) ? true : false;

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
static int ovl_lookup(const struct ovl_fs *ofs, const char *pathname,
		      int start, struct ovl_lookup_data *od)
{
	struct ovl_lookup_ctx lctx = {0};
	int i;
	int ret = 0;

	for (i = start; !lctx.stop && i < ofs->lower_num; i++) {
		lctx.dirfd = ofs->lower_layer[i].fd;
		lctx.pathname = (lctx.redirect) ? lctx.redirect : pathname;
		lctx.last = (i == ofs->lower_num - 1) ? true : false;

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
	const struct ovl_fs *ofs = sctx->ofs;
	const struct ovl_layer *layer = sctx->layer;
	const struct stat *st = sctx->st;
	struct ovl_lookup_data od = {0};
	int ret = 0;

	/* Is a whiteout ? */
	if (!is_whiteout(st))
		return 0;

	sctx->result.t_whiteouts++;

	/* Is whiteout in the bottom lower dir ? */
	if (layer->type == OVL_LOWER && layer->stack == ofs->lower_num-1)
		goto remove;

	/*
	 * Scan each corresponding lower directroy under this layer,
	 * check is there a file or dir with the same name.
	 */
	ret = ovl_lookup_lower(ofs, pathname, layer->type,
			       layer->stack, &od);
	if (ret)
		goto out;

	if (od.exist && !is_whiteout(&od.st))
		goto out;

remove:
	sctx->result.i_whiteouts++;

	/* Remove orphan whiteout directly or ask user */
	if (!ovl_ask_action("Orphan whiteout", pathname, layer->type,
			    layer->stack, "Remove", 1))
		return 0;

	ret = unlinkat(layer->fd, pathname, 0);
	if (ret) {
		print_err(_("Cannot unlink %s: %s\n"), pathname,
			    strerror(errno));
		goto out;
	}
	set_changed(&status);
	sctx->result.t_whiteouts--;
	sctx->result.i_whiteouts--;
out:
	return ret;
}

/* Record the valid redirect target founded */
LIST_HEAD(redirect_list);

/*
 * Record a redirect entry into list
 *
 * @pathname: redirect dir pathname
 * @dirtype: layer type of the redirect dir (OVL_UPPER or OVL_LOWER)
 * @stack: stack number (valid if OVL_LOWER)
 * @origin: redirected origin dir pathname
 * @ostack: retidected origin dir stack number
 */
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

/*
 * Find a redirect entry through the redirected origin target
 *
 * @origin: redirected origin dir pathname
 * @ostack: retidected origin dir stack number
 * @pathname: redirect dir pathname found
 * @dirtype: layer type of the redirect dir (OVL_UPPER or OVL_LOWER)
 * @stack: stack number (valid if OVL_LOWER)
 */
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

/*
 * Delete a redirect entry through the redirected origin target
 */
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

/*
 * Remove an invalid redirect xattr.
 * If the lower dir with the same name exists, it may become a
 * merge dir and duplicate with another recirect xattr already
 * checked, so iterate the list to recheck them.
 */
static int ovl_do_remove_redirect(const struct ovl_fs *ofs,
				  const struct ovl_layer *layer,
				  const char *pathname,
				  int *total,
				  int *invalid)
{
	struct ovl_lookup_data od = {0};
	int du_dirtype, du_stack;
	char *duplicate;
	int ret;

	ret = ovl_remove_redirect(layer->fd, pathname);
	if (ret)
		goto out;

	(*total)--;
	(*invalid)--;
	set_changed(&status);

	/* If lower corresponding dir exists, ask user to set opaque */
	ret = ovl_lookup_lower(ofs, pathname, layer->type,
			       layer->stack, &od);
	if (ret)
		goto out;

	if (!od.exist || !is_dir(&od.st))
		goto out;

	if (ovl_ask_question("Should set opaque dir", pathname,
			     layer->type, layer->stack, 0)) {
		ret = ovl_set_opaque(layer->fd, pathname);
		if (!ret)
			set_changed(&status);
		goto out;
	}

	if (ovl_redirect_entry_find(od.pathname, od.stack, &du_dirtype,
				    &du_stack, &duplicate)) {
		/*
		 * The redirect dir point to the same lower origin becomes
		 * duplicate, should re-ask
		 */
		if ((du_dirtype == layer->type) && (du_stack == layer->stack) &&
		    ovl_ask_action("Duplicate redirect directory",
                                   duplicate, du_dirtype, du_stack,
                                   "Remove redirect", 0)) {
			(*invalid)++;
			ret = ovl_do_remove_redirect(ofs, layer, duplicate,
						     total, invalid);
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
	const struct ovl_fs *ofs = sctx->ofs;
	const struct ovl_layer *layer = sctx->layer;
	struct ovl_lookup_data od = {0};
	struct stat cover_st;
	bool cover_exist = false;
	char *redirect = NULL;
	int start;
	int ret;

	/* Get redirect */
	ret = ovl_get_redirect(layer->fd, pathname, &redirect);
	if (ret || !redirect)
		return ret;

	print_debug(_("Dir \"%s\" has redirect \"%s\"\n"), pathname, redirect);
	sctx->result.t_redirects++;

	/* Redirect dir in last lower dir ? */
	if (layer->type == OVL_LOWER && layer->stack == ofs->lower_num-1)
		goto remove;

	/* Scan lower directories to check redirect dir exist or not */
	start = (layer->type == OVL_LOWER) ? layer->stack + 1 : 0;
	ret = ovl_lookup(ofs, redirect, start, &od);
	if (ret)
		goto out;

	if (od.exist && is_dir(&od.st)) {
		/* Check duplicate with another redirect dir */
		if (ovl_redirect_is_duplicate(od.pathname, od.stack)) {
			sctx->result.i_redirects++;

			/*
			 * Not sure which one is invalid, don't remove in
			 * auto mode
			 */
			if (ovl_ask_action("Duplicate redirect directory",
					   pathname, layer->type, layer->stack,
					   "Remove redirect", 0))
				goto remove_d;
			else
				goto out;
		}

		/* Check duplicate with merge dir */
		ret = ovl_lookup_single(layer->fd, redirect, &cover_st,
					&cover_exist);
		if (ret)
			goto out;
		if (!cover_exist) {
			/* Found nothing, create a whiteout */
			if (ovl_ask_action("Missing whiteout", pathname,
					   layer->type, layer->stack,
					   "Add", 1)) {
				ret = ovl_create_whiteout(layer->fd, redirect);
				if (ret)
					goto out;

				set_changed(&status);
				sctx->result.t_whiteouts++;
			}
		} else if (is_dir(&cover_st) &&
			   !ovl_is_opaque(layer->fd, redirect) &&
			   !ovl_is_redirect(layer->fd, redirect)) {
			/*
			 * Found a directory merge with the same origin,
			 * ask user to remove this duplicate redirect xattr
			 * or set opaque to the cover directory
			 */
			sctx->result.i_redirects++;
			if (ovl_ask_action("Duplicate redirect directory",
					   pathname, layer->type, layer->stack,
					   "Remove redirect", 0)) {
				goto remove_d;
			} else if (ovl_ask_question("Should set opaque dir",
						    redirect, layer->type,
						    layer->stack, 0)) {
				ret = ovl_set_opaque(layer->fd, redirect);
				if (ret)
					goto out;
				set_changed(&status);
				sctx->result.i_redirects--;
			} else {
				goto out;
			}
		}

		/* Now, this redirect xattr is valid */
		ovl_redirect_entry_add(pathname, layer->type, layer->stack,
				       od.pathname, od.stack);

		goto out;
	}

remove:
	sctx->result.i_redirects++;

	/* Remove redirect xattr or ask user */
	if (!ovl_ask_action("Invalid redirect directory", pathname, layer->type,
			    layer->stack, "Remove redirect", 1))
		goto out;
remove_d:
	ret = ovl_do_remove_redirect(ofs, layer, pathname,
				     &sctx->result.t_redirects,
				     &sctx->result.i_redirects);
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
	const struct ovl_layer *layer = sctx->layer;
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

	if (ovl_is_impure(layer->fd, sctx->pathname))
		return 0;

	/* Fix impure xattrs */
	if (ovl_ask_action("Missing impure xattr", sctx->pathname,
			   layer->type, layer->stack, "Fix", 1)) {
		if (ovl_set_impure(layer->fd, sctx->pathname))
			return -1;

		set_changed(&status);
	} else {
		/*
		 * Note: not enforce to fix the case of directory that
		 * only contains general merge subdirs because it could
		 * be an newly created overlay image and fix by overlay
		 * filesystem after mount.
		 */
		if (dirdata->origins || dirdata->redirects)
			sctx->result.m_impure++;
	}

	return 0;
}

static inline bool ovl_is_merge(const struct ovl_fs *ofs,
				const struct ovl_layer *layer,
				const char *pathname)
{
	struct ovl_lookup_data od = {0};

	if (ovl_is_opaque(layer->fd, pathname))
		return false;
	if (ovl_lookup_lower(ofs, pathname, layer->type, layer->stack, &od))
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
	const struct ovl_fs *ofs = sctx->ofs;
	const struct ovl_layer *layer = sctx->layer;
	struct scan_dir_data *parent = sctx->dirdata;

	if (!parent)
		return 0;

	if (ovl_is_origin(layer->fd, sctx->pathname))
		parent->origins++;

	if (is_dir(sctx->st)) {
		if (ovl_is_redirect(layer->fd, sctx->pathname))
			parent->redirects++;
		if (ovl_is_merge(ofs, layer, sctx->pathname))
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
static char *ovl_scan_desc[OVL_SCAN_PASS_MAX] = {
	"Checking redirect xattr and directory tree",
	"Checking whiteouts and impure xattr"
};

static void ovl_scan_clean(void)
{
	/* Clean redirect entry record */
	ovl_redirect_free();
}

static void ovl_scan_report(struct scan_result *result)
{
	if (flags & FL_VERBOSE) {
		print_info(_("Scan %d directories, %d files, "
			     "%d/%d whiteouts, %d/%d redirect dirs "
			     "%d missing impure\n"),
			     result->directories, result->files,
			     result->i_whiteouts, result->t_whiteouts,
			     result->i_redirects, result->t_redirects,
			     result->m_impure);
	}
}

/* Report the invalid targets left */
static void ovl_scan_check(struct scan_result *result)
{
	bool inconsistency = false;

	if (result->i_whiteouts) {
		print_info(_("Invalid whiteouts %d left!\n"),
			     result->i_whiteouts);
		inconsistency = true;
	}
	if (result->i_redirects) {
		print_info(_("Invalid redirect directories %d left!\n"),
			     result->i_redirects);
		inconsistency = true;
	}
	if (result->m_impure) {
		print_info(_("Directories %d missing impure xattr!\n"),
			     result->m_impure);
		inconsistency = true;
	}

	if (inconsistency)
		set_inconsistency(&status);
}

static void ovl_scan_cumsum_result(struct scan_result *layer,
				   struct scan_result *pass)
{
	pass->files += layer->files;
	pass->directories += layer->directories;
	pass->t_whiteouts += layer->t_whiteouts;
	pass->i_whiteouts += layer->i_whiteouts;
	pass->t_redirects += layer->t_redirects;
	pass->i_redirects += layer->i_redirects;
	pass->m_impure += layer->m_impure;
}

static void ovl_scan_update_result(struct scan_result *pass,
				   struct scan_result *total)
{
	total->files = max(pass->files, total->files);
	total->directories = max(pass->directories, total->directories);
	total->t_whiteouts = max(pass->t_whiteouts, total->t_whiteouts);
	total->i_whiteouts = max(pass->i_whiteouts, total->i_whiteouts);
	total->t_redirects = max(pass->t_redirects, total->t_redirects);
	total->i_redirects = max(pass->i_redirects, total->i_redirects);
	total->m_impure = max(pass->m_impure, total->m_impure);
}

static int ovl_scan_layer(struct ovl_fs *ofs, struct ovl_layer *layer,
			  int pass, struct scan_result *result)
{
	struct scan_ctx sctx = {.ofs = ofs};
	struct scan_operations ops = {};
	char skip[256] = {0};
	bool scan = false;
	int ret;

	if (flags & FL_VERBOSE)
		print_info(_("Scan and fix: "
			     "[whiteouts|redirect dir|impure dir]\n"));

	/* Init scan operation for this casn pass and scan underlying dir */
	switch (pass) {
	case OVL_SCAN_PASS_ONE:
		/* PASS 1: Checking redirect xattr and directory tree */
		if (layer->flag & FS_LAYER_XATTR) {
			ops.redirect = ovl_check_redirect;
			scan = true;
		} else {
			/* Skip redirect dir if not support xattr */
			snprintf(skip, sizeof(skip) - strlen(skip),
				 " %s,", "redirect dir");
		}
		break;
	case OVL_SCAN_PASS_TWO:
		/* PASS 2: Checking whiteouts and impure xattr */
		if (layer->type == OVL_UPPER) {
			if (layer->flag & FS_LAYER_XATTR) {
				ops.impurity = ovl_count_impurity;
				ops.impure = ovl_check_impure;
			} else {
				/* Skip impure if not support xattr */
				snprintf(skip, sizeof(skip) - strlen(skip),
					 " %s,", "impure xattr");
			}
		}
		ops.whiteout = ovl_check_whiteout;
		scan = true;
		break;
	default:
		print_err(_("Unknown scan pass %d\n"), pass);
		return -1;
	}

	/*
	 * No need to check some features if this layer not
	 * support xattr.
	 */
	if ((skip[0] != '\0') && (flags & FL_VERBOSE))
		print_info(_("Xattr not supported in %s:%d, "
			     "do not check %s\n"),
			     layer->type == OVL_UPPER ? "upper" : "lower",
			     layer->stack, skip);

	if (!scan)
		return 0;

	sctx.layer = layer;
	ret = scan_dir(&sctx, &ops);

	/* Check scan result for this pass */
	ovl_scan_check(&sctx.result);
	ovl_scan_cumsum_result(&sctx.result, result);

	return ret;
}

/* Scan upperdir and each lowerdirs, check and fix inconsistency */
int ovl_scan_fix(struct ovl_fs *ofs)
{
	struct scan_result result = {0};
	int pass, stack;
	int ret;

	for (pass = 0; pass < OVL_SCAN_PASS_MAX; pass++) {
		struct scan_result pass_result = {0};

		if (flags & FL_VERBOSE)
			print_info(_("Pass %d: %s\n"), pass,
				     ovl_scan_desc[pass]);

		/* Scan each lower layer */
		for (stack = ofs->lower_num - 1; stack >= 0; stack--) {
			print_debug(_("Scan lower layer %d\n"), stack);

			/*
			 * If lower layer is read-only, switch to -n scan
			 * option, because this layer cannot modifiy.
			 */
			if (ofs->lower_layer[stack].flag & FS_LAYER_RO) {
				int save_flags = flags & FL_OPT_MASK;

				print_info(_("Lower layer %d is read-only, "
					     "switch to -n option this layer\n"),
					     stack);

				flags = (flags & !FL_OPT_MASK) | FL_OPT_NO;
				ret = ovl_scan_layer(ofs, &ofs->lower_layer[stack],
						     pass, &pass_result);
				flags = (flags & !FL_OPT_NO) | save_flags;
			} else {
				ret = ovl_scan_layer(ofs, &ofs->lower_layer[stack],
						     pass, &pass_result);
			}

			if (ret)
				goto out;
		}

		/* Scan upper layer */
		if (flags & FL_UPPER) {
			print_debug(_("Scan upper layer\n"));
			ret = ovl_scan_layer(ofs, &ofs->upper_layer, pass,
					     &pass_result);
			if (ret)
				goto out;
		}

		/* Update scan result */
		ovl_scan_update_result(&pass_result, &result);
	}
out:
	ovl_scan_report(&result);
	ovl_scan_clean();
	return ret;
}
