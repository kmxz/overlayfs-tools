#!/bin/sh
#
# Test suite for overlayfs-tools.
#
# Usage: run-tests.sh OVERLAY [DATA_DIR]
#
#   OVERLAY   path to the overlay binary under test
#   DATA_DIR  directory with the test data (default: this script's directory)
#
# The test data contains overlay whiteouts (character devices 0/0) and
# trusted.overlay.* xattrs, and the merged layer is mounted, so the tests
# must run as root, e.g. "sudo meson test -C build --no-rebuild".
#
# Prints one PASS/FAIL/SKIP line per test.  Exit status: 0 if no test
# failed, 1 if a test failed, 77 if the tests cannot run at all (meson
# reports that as skipped).  Set KEEP_WORKDIR=1 to keep the work directory.

usage() {
    echo "usage: $0 OVERLAY [DATA_DIR]" >&2
    exit 2
}

[ $# -ge 1 ] && [ $# -le 2 ] || usage
OVERLAY=$(readlink -f "$1") || usage
DATA=$(readlink -f "${2:-$(dirname "$0")}") || usage
[ -x "$OVERLAY" ] || { echo "$1: not an executable" >&2; exit 2; }

# The expected outputs are sorted in the C locale.
export LC_ALL=C

TESTS="diff diff-verbose diff-brief merge-mountable"
failed=0

skip_all() {
    echo "$1" >&2
    for t in $TESTS; do echo "SKIP: $t"; done
    exit 77
}

result() {
    if [ "$2" -eq 0 ]; then
        echo "PASS: $1"
    else
        echo "FAIL: $1"
        failed=1
    fi
}

[ "$(id -u)" -eq 0 ] || skip_all "overlayfs-tools tests need root"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/overlayfs-tools-tests.XXXXXX") || skip_all "cannot create work directory"
cleanup() {
    umount "$WORK/mnt" 2>/dev/null
    if [ -n "$KEEP_WORKDIR" ]; then
        echo "work directory kept: $WORK" >&2
    else
        rm -rf "$WORK"
    fi
}
trap cleanup EXIT
cd "$WORK" || skip_all "cannot enter $WORK"
# overlay prints resolved paths, so resolve symlinks (e.g. /var/tmp) as well
WORK=$(pwd -P)

# The work directory must support trusted.* xattrs.
touch xattr-probe
setfattr -n trusted.overlay.probe -v y xattr-probe 2>/dev/null || \
    skip_all "$WORK does not support trusted.* xattrs"
rm -f xattr-probe

# Lower layer ("permanent") and upper layer ("changes") with overlay metadata
if ! {
    tar xf "$DATA/lower.tar" &&
    tar xf "$DATA/upper.tar" --strip-components=2 &&
    setfattr -n trusted.overlay.opaque -v y changes/removed_tests/replaced_by_opaque &&
    setfattr -n trusted.overlay.opaque -v y changes/normal_merged_dir/more_test/by_opaque &&
    setfattr -n trusted.overlay.metacopy -v y changes/permission/file &&
    truncate -s 0 changes/permission/file &&
    truncate -s 95 changes/permission/file &&
    mkdir changes/renamed_dir &&
    setfattr -n trusted.overlay.redirect -v "/removed_tests/removed_dir" changes/renamed_dir
}; then
    echo "failed to set up the test layers" >&2
    for t in $TESTS; do echo "FAIL: $t"; done
    exit 1
fi

# diff, verbose diff and brief diff against the saved expected output.
# "diff -b" prints absolute paths, so the work directory is replaced
# with a placeholder.
"$OVERLAY" -l permanent -u changes diff | sort -u > diff.out
diff -u "$DATA/diff.saved" diff.out
result diff $?

"$OVERLAY" -l permanent -u changes diff -v | sort -u > verbose.out
diff -u "$DATA/verbose.saved" verbose.out
result diff-verbose $?

"$OVERLAY" -l permanent -u changes diff -b | sed "s|$WORK|@WORKDIR@|g" | sort -u > brief.out
diff -u "$DATA/brief.saved" brief.out
result diff-brief $?

# Merge the upper layer into the lower one and check that the result mounts
if ! grep -qw overlay /proc/filesystems && ! modprobe overlay 2>/dev/null; then
    echo "overlay filesystem not available" >&2
    echo "SKIP: merge-mountable"
    exit $failed
fi
# "overlay merge" generates and runs a shell script with "sh -x"; keep its
# trace out of the output unless the test fails.
{
    rm -rf changes/renamed_dir &&
    "$OVERLAY" -l permanent -u changes merge -f &&
    mkdir -p work lower mnt &&
    mount -t overlay -o lowerdir=lower,upperdir=permanent,workdir=work overlay mnt &&
    find mnt -type f -exec cat {} + > /dev/null
} > merge.log 2>&1
rc=$?
umount mnt 2>/dev/null
[ $rc -eq 0 ] || cat merge.log
result merge-mountable $rc

exit $failed
