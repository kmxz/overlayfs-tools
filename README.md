# overlayfs-tools

[OverlayFS](https://www.kernel.org/doc/Documentation/filesystems/overlayfs.txt) is the union filesystem provided by Linux kernel.

Before reading further, make sure you understand the OverlayFS terminology and *how* `fsck` will treat it. If you are already familiar with it, please skip directly to [Project Description](#project-description) below.

### Basic items

Kernel do some basic checking for the workdir and upperdir at mount time, fsck
need to do the same things.

1) The workdir should not be subdir of the upperdir and vice versa.
2) The workdir and the upperdir should belong to the same base file system.
3) The upper layer should not be read-only.
4) Any layer of the whole underlying layers should not be mounted.

`fsck` will refuse to check and repair the file system if one of the above
mismatch. It will also check the lower layers that are real read-only or not,
and switch to "no change" mode when checking the read-only lower layer.

### Whiteouts

A whiteout is a character device with 0/0 device number. It is used to record
the removed files or directories, When a whiteout is found in a directory,
there should be at least one directory or file with the same name in any of the
corresponding lower layers. If not exist, the whiteout will be treated as orphan
whiteout and remove.

### Redirect directories

An redirect directory is a directory with "trusted.overlay.redirect" xattr
valued to the path of the original location from the root of the overlay. It
is only used when renaming a directory and "redirect dir" feature is enabled.
If an redirect directory is found, the following must be met:

1) The directory path pointed by redirect xattr should exist in one of lower
   layers.
2) There must be something with the same name to the rename origin in upper
   layer covering the lower target, could be a whiteout or a generic file,
   could be an opaque directory or another redirect direcotry but not a merge
   directory.
3) The origin directory should be redirected by only once, which means this
   origin directory should be redirected by an unique directory in all layers.

If not,
1) The redirect xattr is invalid and should remove.
2) If nothing covering the redirect origin target, fix the missing whiteout.
   If the redirect origin is covered by a generic directory, it becomes a
   subcase of duplicate redirect directory (redirect direcotry duplicate with
   an existing merge directory). Not sure the origin is a merge directory or
   a redirected directory, so there are two options can fix this inconsistency:
   a) remove the redirect xattr fsck found, or b) set opaque to the covering
   directory. Ask user by default or warn in auto mode.
3) Record redirect xattrs but not sure which one is invalid, ask user by
   default and warn in auto mode.

### Impure directories

An impure directory is a directory with "trusted.overlay.impure" xattr valued
'y', which indicate that this directory may contain copied up targets from lower
layers.
If a target copy-up from lower to upper layer, it's 'd_ino' (see getdents(2))
will change from lower's 'd_ino' to upper's (a new inode will be created in
upper layer). So the impure xattr should be set to the parent directory to
prompt overlay filesystem to get and return the origin 'd_ino', thus ensuring
the consistentcy of 'd_ino'.

There are three situations of setting impure xattr in overlay filesystem:
1) Copy-up lower target in a directory.
2) Link an origin target (already copied up, have origin xattr) into a
   directory.
3) Rename an origin target (include merge subdirectories) into a new
   directory.

So, the impure xattr should be set if a direcotry contains origin targets or
redirect/merge subdirectories. If not, fix the impure xattr.

## Project description

This project provides several tools:
- **fsck.overlay** - is used to check and optionally repair underlying directories of overlay-filesystem.
- **vacuum** - remove duplicated files in `upperdir` where `copy_up` is done but the file is not actually modified (see the sentence "the `copy_up` may turn out to be unnecessary" in the [Linux documentation](https://www.kernel.org/doc/Documentation/filesystems/overlayfs.txt)). This may reduce the size of `upperdir` without changing `lowerdir` or `overlay`.
- **diff** - show the list of actually changed files (the difference between `overlay` and `lowerdir`). A file with its type changed (i.e. from symbolic link to regular file) will shown as deleted then added, rather than modified. Similarly, for a opaque directory in `upperdir`, the corresponding directory in `lowerdir` (if exists) will be shown as entirely deleted, and a new directory with the same name added. File permission/owner changes will be simply shown as modified.
- **merge** - merge down the changes from `upperdir` to `lowerdir`. Unlike [aubrsync](http://aufs.sourceforge.net/aufs2/brsync/README.txt) for AuFS which bypasses the union filesystem mechanism, overlayfs-utils emulates the OverlayFS logic, which will be far more efficient. After this operation, `upperdir` will be empty and `lowerdir` will be the same as original `overlay`.
- **deref** - copy changes from `upperdir` to `uppernew` while unfolding redirect directories and metacopy regular files, so that new upperdir is compatible with legacy overlayfs driver.

For safety reasons, vacuum and merge will not actually modify the filesystem, but generate a shell script to do the changes instead.
Use -f to force execution.

## Build

You'll need to install the Meson build system on your system first, make sure to install a version ≥ 0.54:
- Using python package
``` bash 
python3 -m pip install meson ninja
```
- Or install directly on debian linux
``` bash
apt install meson ninja-build
```

To build the project then run the following:

``` bash
cd /path/to/overlayfs-tools
meson setup builddir && cd builddir
meson compile
sudo meson install
```

## Example usage

Most of the tools are called via `overlay` binary

    # ./overlay diff -l /lower -u /upper

See `./overlay --help` for more.

`fsck.overlay` is a separate binary, and has some extra parameters.

Ensure overlay filesystem is not mounted based on directories which need to check.

Run `fsck.overlay` program. Usage:

    fsck.overlay [-o lowerdir=<lowers>,upperdir=<upper>,workdir=<work>] [-pnyvhV]

    Options:
    -o,                       specify underlying directories of overlayfs:
                              multiple lower directories use ':' as separator
    -p,                       automatic repair (no questions)
    -n,                       make no changes to the filesystem
    -y,                       assume "yes" to all questions
    -v, --verbose             print more messages of overlayfs
    -h, --help                display this usage of overlayfs
    -V, --version             display version information

   Example:

    # fsck.overlay -o lowerdir=lower,upperdir=upper,workdir=work

Exit values:

    0      No errors
    1      Filesystem errors corrected
    2      System should be rebooted
    4      Filesystem errors left uncorrected
    8      Operational error
    16     Usage or syntax error
    32     Checking canceled by user request
    128    Shared-library error

## Why sudo

As [Linux documentation](https://www.kernel.org/doc/Documentation/filesystems/overlayfs.txt) said, 

> A directory is made opaque by setting the xattr "trusted.overlay.opaque" to "y".

However, only users with `CAP_SYS_ADMIN` can read `trusted.*` extended attributes.

## Warnings / limitations
**overlay** binary
- Only works for regular files and directories. Do not use it on OverlayFS with device files, socket files, etc..
- Hard links may be broken (i.e. resulting in duplicated independent files).
- File owner, group and permission bits will be preserved. File timestamps, attributes and extended attributes might be lost. 
- This program only works for OverlayFS with only one lower layer.
- It is recommended to have the OverlayFS unmounted before running this program.  

**fsck** binary
- It is strongly recommend to run this program after modifing underlying directories while overlay filesystem is offline.
- Enough file descriptors (more than the number of specified underlying directories) are required to run this program.
- Current version cannot support overlayfs which was mounted with new features introduced in Linux kernel >= 4.13, include `index`, `nfs_export` and other upcoming features. Checking overlayfs which has these features may lead to inconsistency.

## Contributions

Contributions to overlayfs-progs are very welcome.
Please send Pull Reqeusts to this project. For `fsck.overlay` utility you might want to CC linux-unionfs mailing list at linux-unionfs@vger.kernel.org.
