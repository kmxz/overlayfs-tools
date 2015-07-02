**This project is unfinished and NOT usable yet.**

overlayfs-tools
========

[OverlayFS](https://www.kernel.org/doc/Documentation/filesystems/overlayfs.txt) is the union filesystem provided by Linux kernel.

This program comes provides three tools:
- **vacuum** - remove duplicated files in `upperdir` where `copy_up` is done but the file is not actually modified (see the sentence "the `copy_up` may turn out to be unnecessary" in the [Linux documentation](https://www.kernel.org/doc/Documentation/filesystems/overlayfs.txt)). This may reduce the size of `upperdir` without changing `lowerdir` or `overlay`.
- **diff** - show the list of actually changed files (the difference between `overlay` and `lowerdir`). The added and modified files are basically the files in `upperdir`, excluding unnecessary `copy_up`s. The removed files will also be listed.
- **merge** - merge down the changes from `upperdir` to `lowerdir`. Unlike [aubrsync](http://aufs.sourceforge.net/aufs2/brsync/README.txt) for AuFS which bypasses the union filesystem mechanism, overlayfs-utils emulates the OverlayFS logic, which will be far more efficient. After this operation, `upperdir` will be empty and `lowerdir` will be the same as original `overlay`. Of course, `lowerdir` must be writable.

Prerequisite and Building
--------

To build it, you must have libattr headers installed. The package is usually named `libattr1-dev` or `libattr-devel`. For example,

    # apt-get install libattr1-dev

Then, you just build this program:

    $ make

Example usage
--------

    # ./overlay diff -l /lower -u /upper

See `./overlay --help` for more.

Why sudo
--------

As [Linux documentation](https://www.kernel.org/doc/Documentation/filesystems/overlayfs.txt) said, 

> A directory is made opaque by setting the xattr "trusted.overlay.opaque" to "y".

However, only users with `CAP_SYS_ADMIN` can read `trusted.*` extended attributes.

Warnings / limitations
--------

- Only works for regular files and directories. Do not use it on OverlayFS with device files, socket files, etc..
- Hard links may be broken (i.e. resulting in duplicated independent files).
- The current version only take care about file content. All attributes (owner, time, etc.) except permission bits will be lost.
- This program only works for OverlayFS with only one lower layer.
- It is recommended to have the OverlayFS unmounted before running this program.
