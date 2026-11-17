project('overlayfs-tools', 'c',
    default_options : ['c_std=gnu11'],
    version : '2025.01')

# Source files for executables
overlay_src = ['main.c', 'logic.c', 'sh.c', 'common.c']
fsck_src = ['fsck.c', 'common.c', 'lib.c', 'check.c', 'mount.c', 'path.c', 'overlayfs.c']

# Dependencies for executables
# We have to link against fts on musl based systems, but this is a soft dependency
# If it's not found on the glibc systems, it's just ignored
musl_fts = dependency('musl-fts', method : 'pkg-config', required : false)
fsck_dep = meson.get_compiler('c').find_library('m', required : false)
setfattr_available = find_program('setfattr')

# Executables
overlay = executable('overlay', overlay_src,
    install : true,
    c_args : '-DOVERLAYFS_TOOLS_VERSION="@0@"'.format(meson.project_version()),
    dependencies : musl_fts)
executable('fsck.overlay', fsck_src,
    install : true,
    c_args : '-DOVERLAYFS_TOOLS_VERSION="@0@"'.format(meson.project_version()),
    dependencies : [fsck_dep, musl_fts])

if get_option('tests')
# Custom targets for testing overlay functionality
lower_tar = 'test_cases/lower.tar'
upper_tar = 'test_cases/upper.tar'

permanent = custom_target('permanent',
    input : lower_tar,
    output : 'permanent',
    command : ['sh', '-c', 'tar xf @INPUT@']
)

changes = custom_target('changes',
    input : upper_tar,
    output : 'changes',
    command : [
        'sh', '-c',
        'tar xf @INPUT@ --strip-components=2 && ' +
        'setfattr -n trusted.overlay.opaque -v y changes/removed_tests/replaced_by_opaque && ' +
        'setfattr -n trusted.overlay.opaque -v y changes/normal_merged_dir/more_test/by_opaque && ' +
        'setfattr -n trusted.overlay.metacopy -v y changes/permission/file && ' +
        'truncate -s 0 changes/permission/file && ' +
        'truncate -s 95 changes/permission/file && ' +
        'mkdir changes/renamed_dir && ' +
        'setfattr -n trusted.overlay.redirect -v "/removed_tests/removed_dir" changes/renamed_dir'
    ]
)

diff_out = custom_target('diff.out',
    output : 'diff.out',
    command : [
        'sh', '-c',
        'sudo ' + overlay.full_path() + ' -l permanent -u changes diff | LC_ALL=C sort -u > @OUTPUT@'
    ]
)

verbose_out = custom_target('verbose.out',
    output : 'verbose.out',
    command : [
        'sh', '-c',
        'sudo ' + overlay.full_path() + ' -l permanent -u changes diff -v | LC_ALL=C sort -u > @OUTPUT@'
    ]
)

brief_out = custom_target('brief.out',
    output : 'brief.out',
    command : [
        'sh', '-c',
        'sudo ' + overlay.full_path() + ' -l permanent -u changes diff -b | sed "s|$(pwd -P)|@WORKDIR@|g" | LC_ALL=C sort -u > @OUTPUT@'
    ]
)

test('run_tests', find_program('test_cases/run_tests.py'))

mountable = custom_target('mountable',
    output : 'mountable',
    command : [
        'sh', '-c',
        'sudo rm -rf changes/renamed_dir && ' +
        'sudo ' + overlay.full_path() + ' -l permanent -u changes merge -f && ' +
        'mkdir -p work lower mnt && ' +
        'sudo mount -t overlay -o lowerdir=lower,upperdir=permanent,workdir=work overlay mnt &&' +
        'trap "sudo umount mnt" EXIT && ' +
        'find mnt -type f -print -exec cat {} + >/dev/null'
    ]
)

custom_target('clean.tests',
    output : 'clean.tests',
    command : [
        'sh', '-c',
        'sudo umount mnt >/dev/null 2>&1 ; ' +
        'sudo rm -rf permanent changes brief.out diff.out verbose.out lower work mnt'
    ]
)
endif
