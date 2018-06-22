/*
 * fsck.c - Utility to fsck overlay
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <linux/limits.h>

#include "common.h"
#include "config.h"
#include "lib.h"
#include "check.h"
#include "mount.h"

char *program_name;

struct ovl_fs ofs = {};
int flags = 0;		/* user input option flags */
int status = 0;		/* fsck scan status */

/*
 * Open underlying dirs (include upper dir and lower dirs), check system
 * file descriptor limits and try to expend it if necessary.
 */
static int ovl_open_dirs(struct ovl_fs *ofs)
{
	unsigned int i;
	struct rlimit rlim;
	rlim_t rlim_need = ofs->lower_num + 20;

	/* If RLIMIT_NOFILE limit is small than we need, try to expand limit */
	if ((getrlimit(RLIMIT_NOFILE, &rlim))) {
		print_err(_("Failed to getrlimit:%s\n"), strerror(errno));
		return -1;
	}
	if (rlim.rlim_cur < rlim_need) {
		print_info(_("Process fd number limit=%lu "
			     "too small, need %lu\n"),
			     rlim.rlim_cur, rlim_need);

		rlim.rlim_cur = rlim_need;
		if (rlim.rlim_max < rlim.rlim_cur)
			rlim.rlim_max = rlim.rlim_cur;

		if ((setrlimit(RLIMIT_NOFILE, &rlim))) {
			print_err(_("Failed to setrlimit:%s\n"),
				    strerror(errno));
			return -1;
		}
	}

	if (ofs->upper_layer.path) {
		ofs->upper_layer.fd = open(ofs->upper_layer.path,
			       O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
		if (ofs->upper_layer.fd < 0) {
			print_err(_("Failed to open %s:%s\n"),
				    ofs->upper_layer.path, strerror(errno));
			return -1;
		}
	}

	for (i = 0; i < ofs->lower_num; i++) {
		ofs->lower_layer[i].fd = open(ofs->lower_layer[i].path,
				  O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
		if (ofs->lower_layer[i].fd < 0) {
			print_err(_("Failed to open %s:%s\n"),
				    ofs->lower_layer[i].path, strerror(errno));
			goto err;
		}
	}

	return 0;
err:
	for (i--; i >= 0; i--) {
		close(ofs->lower_layer[i].fd);
		ofs->lower_layer[i].fd = 0;
	}
	close(ofs->upper_layer.fd);
	ofs->upper_layer.fd = 0;
	return -1;
}

/* Cleanup underlying directories buffers */
static void ovl_clean_dirs(struct ovl_fs *ofs)
{
	int i;

	for (i = 0; i < ofs->lower_num; i++) {
		if (ofs->lower_layer && ofs->lower_layer[i].fd) {
			close(ofs->lower_layer[i].fd);
			ofs->lower_layer[i].fd = 0;
		}
		free(ofs->lower_layer[i].path);
		ofs->lower_layer[i].path = NULL;
	}
	free(ofs->lower_layer);
	ofs->lower_layer = NULL;
	ofs->lower_num = 0;

	if (ofs->upper_layer.path) {
		close(ofs->upper_layer.fd);
		ofs->upper_layer.fd = 0;
		free(ofs->upper_layer.path);
		ofs->upper_layer.path = NULL;
	}
	if (ofs->workdir.path) {
		free(ofs->workdir.path);
		ofs->workdir.path = NULL;
	}
}

static void usage(void)
{
	print_info(_("Usage:\n\t%s [-o lowerdir=<lowers>,upperdir=<upper>,workdir=<work>] "
		    "[-pnyvhV]\n\n"), program_name);
	print_info(_("Options:\n"
		    "-o,                       specify underlying directories of overlayfs\n"
		    "                          multiple lower directories use ':' as separator\n"
		    "-p,                       automatic repair (no questions)\n"
		    "-n,                       make no changes to the filesystem\n"
		    "-y,                       assume \"yes\" to all questions\n"
		    "-v, --verbose             print more messages of overlayfs\n"
		    "-h, --help                display this usage of overlayfs\n"
		    "-V, --version             display version information\n"));
	exit(1);
}

/* Parse options from user and check correctness */
static void parse_options(int argc, char *argv[])
{
	struct ovl_config config = {0};
	char *ovl_opts = NULL;
	int i, c;
	char **lowerdir = NULL;
	bool conflict = false;

	struct option long_options[] = {
		{"verbose", no_argument, NULL, 'v'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	while ((c = getopt_long(argc, argv, "o:apnyvVh",
		long_options, NULL)) != -1) {

		switch (c) {
		case 'o':
			ovl_opts = sstrdup(optarg);
			ovl_parse_opt(ovl_opts, &config);
			free(ovl_opts);
			break;
		case 'p':
			if (flags & (FL_OPT_YES | FL_OPT_NO))
				conflict = true;
			else
				flags |= FL_OPT_AUTO;
			break;
		case 'n':
			if (flags & (FL_OPT_YES | FL_OPT_AUTO))
				conflict = true;
			else
				flags |= FL_OPT_NO;
			break;
		case 'y':
			if (flags & (FL_OPT_NO | FL_OPT_AUTO))
				conflict = true;
			else
				flags |= FL_OPT_YES;
			break;
		case 'v':
			flags |= FL_VERBOSE;
			break;
		case 'V':
			version();
			exit(0);
		case 'h':
		default:
			usage();
			return;
		}
	}

	/* Resolve and get each underlying directory of overlay filesystem */
	if (ovl_get_dirs(&config, &lowerdir, &ofs.lower_num,
			 &ofs.upper_layer.path, &ofs.workdir.path))
		goto err_out;

	ofs.lower_layer = smalloc(ofs.lower_num * sizeof(struct ovl_layer));
	for (i = 0; i < ofs.lower_num; i++) {
		ofs.lower_layer[i].path = lowerdir[i];
		ofs.lower_layer[i].type = OVL_LOWER;
		ofs.lower_layer[i].stack = i;
	}
	if (ofs.upper_layer.path) {
		ofs.upper_layer.type = OVL_UPPER;
		flags |= FL_UPPER;
	}
	if (ofs.workdir.path)
		ofs.workdir.type = OVL_WORKER;

	if (!ofs.lower_num ||
	    (!(flags & FL_UPPER) && ofs.lower_num == 1)) {
		print_info(_("Please specify correct lowerdirs and upperdir!\n\n"));
		goto err_out2;
	}

	if (ofs.upper_layer.path && !ofs.workdir.path) {
		print_info(_("Please specify correct workdir!\n\n"));
		goto err_out2;
	}

	if (conflict) {
		print_info(_("Only one of the options -p/-a, -n or -y "
			     "can be specified!\n\n"));
		goto err_out2;
	}

	ovl_free_opt(&config);
	free(lowerdir);
	return;

err_out2:
	ovl_free_opt(&config);
	ovl_clean_dirs(&ofs);
	free(lowerdir);
err_out:
	usage();
	exit(1);
}

/* Check file system status after fsck and return the exit value */
static void fsck_status_report(int *exit)
{
	if (status & OVL_ST_INCONSISTNECY) {
		*exit |= FSCK_UNCORRECTED;
		print_info(_("Still have unexpected inconsistency!\n"));
	}

	if (status & OVL_ST_ABORT) {
		*exit |= FSCK_ERROR;
		print_info(_("Cannot continue, aborting\n"));
	}
}

int main(int argc, char *argv[])
{
	bool mounted = false;
	int exit = 0;

	program_name = basename(argv[0]);

	parse_options(argc, argv);

	/* Open all specified base dirs */
	if (ovl_open_dirs(&ofs))
		goto err;

	/* Ensure overlay filesystem not mounted */
	if (ovl_check_mount(&ofs, &mounted))
		goto err;

	if (mounted && !(flags & FL_OPT_NO)) {
		set_abort(&status);
		goto out;
	}

	/* Scan and fix */
	if (ovl_scan_fix(&ofs))
		goto err;

out:
	ovl_clean_dirs(&ofs);
	fsck_status_report(&exit);
	if (exit)
		print_info("WARNING: Filesystem check failed, may not clean\n");
	else
		print_info("Filesystem clean\n");

	return exit;
err:
	exit |= FSCK_ERROR;
	goto out;
}
