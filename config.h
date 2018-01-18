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

#ifndef OVL_CONFIG_H
#define OVL_CONFIG_H

/* program version */
#define PACKAGE_VERSION	"v0.1.0"

/* overlay max lower stacks (the same to kernel overlayfs driver) */
#define OVL_MAX_STACK 500

/* File with mounted filesystems */
#define MOUNT_TAB "/proc/mounts"

/* Name of overlay filesystem type */
#define OVERLAY_NAME "overlay"
#define OVERLAY_NAME_OLD "overlayfs"

/* Mount options */
#define OPT_LOWERDIR "lowerdir="
#define OPT_UPPERDIR "upperdir="
#define OPT_WORKDIR "workdir="

#endif
