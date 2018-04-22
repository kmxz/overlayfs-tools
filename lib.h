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

#ifndef OVL_LIB_H
#define OVL_LIB_H

/* Common return value */
#define FSCK_OK          0	/* No errors */
#define FSCK_NONDESTRUCT 1	/* File system errors corrected */
#define FSCK_REBOOT      2	/* System should be rebooted */
#define FSCK_UNCORRECTED 4	/* File system errors left uncorrected */
#define FSCK_ERROR       8	/* Operational error */
#define FSCK_USAGE       16	/* Usage or syntax error */
#define FSCK_CANCELED	 32	/* Aborted with a signal or ^C */
#define FSCK_LIBRARY     128	/* Shared library error */

/* Fsck status */
#define OVL_ST_INCONSISTNECY	(1 << 0)
#define OVL_ST_ABORT		(1 << 1)

/* Option flags */
#define FL_VERBOSE	(1 << 0)	/* verbose */
#define FL_UPPER	(1 << 1)	/* specify upper directory */
#define FL_OPT_AUTO	(1 << 3)	/* automactically scan dirs and repair */
#define FL_OPT_NO	(1 << 4)	/* no changes to the filesystem */
#define FL_OPT_YES	(1 << 5)	/* yes to all questions */
#define FL_OPT_MASK	(FL_OPT_AUTO|FL_OPT_NO|FL_OPT_YES)

/* Scan pass */
#define OVL_SCAN_PASS_ONE	0
#define OVL_SCAN_PASS_TWO	1
#define OVL_SCAN_PASS_MAX	2

/* Scan path type */
#define OVL_UPPER	0
#define OVL_LOWER	1
#define OVL_WORKER	2
#define OVL_PTYPE_MAX	3

/* Information for each underlying layer */
struct ovl_layer {
	char *path;		/* root dir path for this layer */
	int fd;			/* root dir fd for this layer */
	int type;		/* OVL_UPPER or OVL_LOWER */
	int stack;		/* lower layer stack number, OVL_LOWER use only */
};

/* Information for the whole overlay filesystem */
struct ovl_fs {
	struct ovl_layer upper_layer;
	struct ovl_layer *lower_layer;
	int lower_num;
	struct ovl_layer workdir;
};

/* Directories scan data structs */
struct scan_dir_data {
       int origins;		/* origin number in this directory (no iterate) */
       int mergedirs;		/* merge subdir number in this directory (no iterate) */
       int redirects;		/* redirect subdir number in this directory (no iterate) */
};

struct scan_result {
	int files;		/* total files */
	int directories;	/* total directories */
	int t_whiteouts;	/* total whiteouts */
	int i_whiteouts;	/* invalid whiteouts */
	int t_redirects;	/* total redirect dirs */
	int i_redirects;	/* invalid redirect dirs */
	int m_impure;		/* missing inpure dirs */
};

struct scan_ctx {
	struct ovl_fs *ofs;		/* scan ovl fs */
	struct ovl_layer *layer;	/* scan layer */
	struct scan_result result;	/* scan count result */

	const char *pathname;	/* path relative to overlay root */
	const char *filename;	/* filename */
	struct stat *st;	/* file stat */
	struct scan_dir_data *dirdata;	/* parent dir data of current (could be null) */
};

/* Directories scan callback operations struct */
struct scan_operations {
	int (*whiteout)(struct scan_ctx *);
	int (*redirect)(struct scan_ctx *);
	int (*origin)(struct scan_ctx *);
	int (*impurity)(struct scan_ctx *);
	int (*impure)(struct scan_ctx *);
};

static inline void set_inconsistency(int *status)
{
	*status |= OVL_ST_INCONSISTNECY;
}

static inline void set_abort(int *status)
{
	*status |= OVL_ST_ABORT;
}

int scan_dir(struct scan_ctx *sctx, struct scan_operations *sop);
int ask_question(const char *question, int def);
ssize_t get_xattr(int dirfd, const char *pathname, const char *xattrname,
		  char **value, bool *exist);
int set_xattr(int dirfd, const char *pathname, const char *xattrname,
	      void *value, size_t size);
int remove_xattr(int dirfd, const char *pathname, const char *xattrname);

#endif /* OVL_LIB_H */
